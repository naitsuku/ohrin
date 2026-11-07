/*
	jscript.cpp — JavaScriptCore で動く「JScript」スクリプトエンジン

	Windows の JScript と同じく IActiveScript / IActiveScriptParse を実装する。上流の CWSHClient は
	レジストリで .js のエンジン名を調べて CoCreateInstance するので、マクロとプラグインの処理はそのまま動く。

	名前付きアイテム（Editor など）は IDispatch のまま扱い、プロパティを読まれた時点で GetIDsOfNames で
	解決する。CIfObj の名前解決が大文字小文字を区別しないのも Windows と同じになる。
	SCRIPTITEM_GLOBALMEMBERS のアイテムは、グローバルオブジェクトのクラスのコールバックで解決する。
	スクリプトのグローバル変数と JS 標準の Math などが、マクロ関数より先に見つかる。

	対応しないもの: ActiveXObject、JScript 独自の条件付きコンパイル（@cc_on）

	SPDX-License-Identifier: Zlib
*/
#include <windows.h>
#include <activscp.h>

#include <JavaScriptCore/JavaScript.h>

#include <atomic>
#include <cwchar>
#include <string>
#include <vector>

#include "winport_internal.h"

// JavaScriptCore が公開しているが SDK のヘッダには無い関数。無限ループのマクロを止めるのに使う
extern "C" void JSContextGroupSetExecutionTimeLimit(JSContextGroupRef, double limit,
	bool (*callback)(JSContextRef, void*), void* context);

namespace {

// JScript の本来の CLSID
constexpr CLSID kClsidJScript = {0xf414c260, 0x6ac0, 0x11cf, {0xb6, 0xd1, 0x00, 0xaa, 0x00, 0xbb, 0xbb, 0x58}};

//! マクロの中断要求を確認する間隔（秒）
constexpr double kInterruptPollSeconds = 0.1;

JSStringRef ToJSString(const wchar_t* s, size_t len)
{
	std::vector<JSChar> units(len);
	for (size_t i = 0; i < len; ++i) units[i] = static_cast<JSChar>(s[i]);
	return JSStringCreateWithCharacters(units.data(), len);
}

std::wstring FromJSString(JSStringRef s)
{
	const size_t len = JSStringGetLength(s);
	const JSChar* chars = JSStringGetCharactersPtr(s);
	std::wstring out(len, L'\0');
	for (size_t i = 0; i < len; ++i) out[i] = chars[i];
	return out;
}

std::wstring ValueToText(JSContextRef ctx, JSValueRef value)
{
	JSStringRef s = JSValueToStringCopy(ctx, value, nullptr);
	if (!s) return L"";
	std::wstring out = FromJSString(s);
	JSStringRelease(s);
	return out;
}

void ToVariant(JSContextRef ctx, JSValueRef value, VARIANT* out)
{
	VariantInit(out);
	switch (JSValueGetType(ctx, value)) {
	case kJSTypeUndefined:
		break;
	case kJSTypeNull:
		out->vt = VT_NULL;
		break;
	case kJSTypeBoolean:
		out->vt = VT_BOOL;
		out->boolVal = JSValueToBoolean(ctx, value) ? VARIANT_TRUE : VARIANT_FALSE;
		break;
	case kJSTypeNumber: {
		const double d = JSValueToNumber(ctx, value, nullptr);
		if (d == static_cast<double>(static_cast<LONG>(d))) {
			out->vt = VT_I4;
			out->lVal = static_cast<LONG>(d);
		} else {
			out->vt = VT_R8;
			out->dblVal = d;
		}
		break;
	}
	default: {	// 文字列。オブジェクトは文字列表現を渡す
		const std::wstring s = ValueToText(ctx, value);
		out->vt = VT_BSTR;
		out->bstrVal = SysAllocStringLen(s.data(), static_cast<UINT>(s.size()));
		break;
	}
	}
}

JSValueRef FromVariant(JSContextRef ctx, VARIANT* source)
{
	VARIANT v;
	VariantInit(&v);
	VariantCopyInd(&v, source);
	JSValueRef result;
	switch (v.vt) {
	case VT_NULL: result = JSValueMakeNull(ctx); break;
	case VT_BOOL: result = JSValueMakeBoolean(ctx, v.boolVal != VARIANT_FALSE); break;
	case VT_I2: result = JSValueMakeNumber(ctx, v.iVal); break;
	case VT_I4: result = JSValueMakeNumber(ctx, v.lVal); break;
	case VT_UI1: result = JSValueMakeNumber(ctx, v.bVal); break;
	case VT_UI2: result = JSValueMakeNumber(ctx, v.uiVal); break;
	case VT_UI4: result = JSValueMakeNumber(ctx, v.ulVal); break;
	case VT_R8: result = JSValueMakeNumber(ctx, v.dblVal); break;
	case VT_BSTR: {
		JSStringRef s = ToJSString(v.bstrVal, SysStringLen(v.bstrVal));
		result = JSValueMakeString(ctx, s);
		JSStringRelease(s);
		break;
	}
	default: result = JSValueMakeUndefined(ctx); break;
	}
	VariantClear(&v);
	return result;
}

JSValueRef MakeError(JSContextRef ctx, const std::wstring& message)
{
	JSStringRef s = ToJSString(message.data(), message.size());
	JSValueRef text = JSValueMakeString(ctx, s);
	JSStringRelease(s);
	return JSObjectMakeError(ctx, 1, &text, nullptr);
}

/* ---- JS から見える IDispatch ---- */
struct MethodData {
	IDispatch* dispatch;	// 所有はエンジンの名前付きアイテム
	DISPID id;
};

void FinalizeMethod(JSObjectRef object) { delete static_cast<MethodData*>(JSObjectGetPrivate(object)); }

JSValueRef CallMethod(JSContextRef ctx, JSObjectRef function, JSObjectRef, size_t argc, const JSValueRef argv[], JSValueRef* exception)
{
	auto* method = static_cast<MethodData*>(JSObjectGetPrivate(function));
	std::vector<VARIANT> args(argc);
	for (size_t i = 0; i < argc; ++i) ToVariant(ctx, argv[i], &args[argc - i - 1]);	// DISPPARAMS は逆順

	DISPPARAMS params{};
	params.rgvarg = args.data();
	params.cArgs = static_cast<UINT>(argc);
	VARIANT result;
	VariantInit(&result);
	const HRESULT hr = method->dispatch->Invoke(method->id, IID_NULL, 0, DISPATCH_METHOD, &params, &result, nullptr, nullptr);

	JSValueRef value = JSValueMakeUndefined(ctx);
	if (SUCCEEDED(hr)) {
		value = FromVariant(ctx, &result);
	} else if (exception) {
		*exception = MakeError(ctx, L"Macro function failed: " + std::to_wstring(static_cast<ULONG>(hr)));
	}
	VariantClear(&result);
	for (VARIANT& arg : args) VariantClear(&arg);
	return value;
}

JSClassRef MethodClass()
{
	static JSClassRef cls = [] {
		JSClassDefinition def = kJSClassDefinitionEmpty;
		def.className = "SakuraMethod";
		def.finalize = FinalizeMethod;
		def.callAsFunction = CallMethod;
		return JSClassCreate(&def);
	}();
	return cls;
}

//! 名前を解決できれば呼び出せる関数オブジェクトを返す
JSValueRef ResolveMethod(JSContextRef ctx, IDispatch* dispatch, JSStringRef name)
{
	std::wstring text = FromJSString(name);
	LPOLESTR names[] = {text.data()};
	DISPID id = 0;
	if (FAILED(dispatch->GetIDsOfNames(IID_NULL, names, 1, 0, &id))) return nullptr;
	return JSObjectMake(ctx, MethodClass(), new MethodData{dispatch, id});
}

JSValueRef GetItemProperty(JSContextRef ctx, JSObjectRef object, JSStringRef name, JSValueRef*)
{
	return ResolveMethod(ctx, static_cast<IDispatch*>(JSObjectGetPrivate(object)), name);
}

JSClassRef ItemClass()
{
	static JSClassRef cls = [] {
		JSClassDefinition def = kJSClassDefinitionEmpty;
		def.className = "SakuraObject";
		def.getProperty = GetItemProperty;
		return JSClassCreate(&def);
	}();
	return cls;
}

/* ---- エラー ---- */
class ScriptError final : public IActiveScriptError {
public:
	ScriptError(std::wstring description, ULONG line) : m_description(std::move(description)), m_line(line) {}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override
	{
		if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, __uuidof(IActiveScriptError))) {
			*out = static_cast<IActiveScriptError*>(this);
			AddRef();
			return S_OK;
		}
		*out = nullptr;
		return E_NOINTERFACE;
	}
	ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refs; }
	ULONG STDMETHODCALLTYPE Release() override
	{
		const ULONG n = --m_refs;
		if (n == 0) delete this;
		return n;
	}
	HRESULT STDMETHODCALLTYPE GetExceptionInfo(EXCEPINFO* info) override
	{
		*info = EXCEPINFO{};
		info->bstrSource = SysAllocString(L"JScript");
		info->bstrDescription = SysAllocStringLen(m_description.data(), static_cast<UINT>(m_description.size()));
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetSourcePosition(DWORD* context, ULONG* line, LONG* position) override
	{
		if (context) *context = 0;
		if (line) *line = m_line;
		if (position) *position = 0;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetSourceLineText(BSTR*) override { return E_NOTIMPL; }

private:
	std::atomic<ULONG> m_refs{1};
	std::wstring m_description;
	ULONG m_line;
};

/* ---- エンジン ---- */
class JScriptEngine final : public IActiveScript, public IActiveScriptParse {
public:
	/* IUnknown */
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override
	{
		if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, __uuidof(IActiveScript))) {
			*out = static_cast<IActiveScript*>(this);
		} else if (IsEqualIID(riid, __uuidof(IActiveScriptParse))) {
			*out = static_cast<IActiveScriptParse*>(this);
		} else {
			*out = nullptr;
			return E_NOINTERFACE;
		}
		AddRef();
		return S_OK;
	}
	ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refs; }
	ULONG STDMETHODCALLTYPE Release() override
	{
		const ULONG n = --m_refs;
		if (n == 0) delete this;
		return n;
	}

	/* IActiveScript */
	HRESULT STDMETHODCALLTYPE SetScriptSite(IActiveScriptSite* site) override
	{
		if (m_site) return E_UNEXPECTED;
		m_site = site;
		m_site->AddRef();
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetScriptSite(REFIID riid, void** out) override
	{
		return m_site ? m_site->QueryInterface(riid, out) : S_FALSE;
	}
	HRESULT STDMETHODCALLTYPE SetScriptState(SCRIPTSTATE state) override { m_state = state; return S_OK; }
	HRESULT STDMETHODCALLTYPE GetScriptState(SCRIPTSTATE* state) override { *state = m_state; return S_OK; }
	HRESULT STDMETHODCALLTYPE Close() override
	{
		if (m_context) {
			JSGlobalContextRelease(m_context);
			m_context = nullptr;
		}
		if (m_group) {
			JSContextGroupRelease(m_group);
			m_group = nullptr;
		}
		for (Item& item : m_items) {
			if (item.dispatch) item.dispatch->Release();
		}
		m_items.clear();
		if (m_site) {
			m_site->Release();
			m_site = nullptr;
		}
		m_state = SCRIPTSTATE_CLOSED;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE AddNamedItem(LPCOLESTR name, DWORD flags) override
	{
		m_items.push_back(Item{name, flags, nullptr});
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE AddTypeLib(REFGUID, DWORD, DWORD, DWORD) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE GetScriptDispatch(LPCOLESTR, IDispatch**) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE GetCurrentScriptThreadID(SCRIPTTHREADID*) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE GetScriptThreadID(DWORD, SCRIPTTHREADID*) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE GetScriptThreadState(SCRIPTTHREADID, SCRIPTTHREADSTATE*) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE InterruptScriptThread(SCRIPTTHREADID, const EXCEPINFO*, DWORD) override
	{
		m_interrupted = true;	// 別スレッドから呼ばれる
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE Clone(IActiveScript**) override { return E_NOTIMPL; }

	/* IActiveScriptParse */
	HRESULT STDMETHODCALLTYPE InitNew() override
	{
		m_state = SCRIPTSTATE_INITIALIZED;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE AddScriptlet(LPCOLESTR, LPCOLESTR, LPCOLESTR, LPCOLESTR, LPCOLESTR, LPCOLESTR, DWORDLONG, ULONG, DWORD, BSTR*, EXCEPINFO*) override
	{
		return E_NOTIMPL;
	}
	HRESULT STDMETHODCALLTYPE ParseScriptText(LPCOLESTR code, LPCOLESTR, IUnknown*, LPCOLESTR, DWORDLONG, ULONG, DWORD, VARIANT* result, EXCEPINFO*) override
	{
		if (!m_context && !CreateContext()) return E_FAIL;
		JSStringRef source = ToJSString(code, std::wcslen(code));
		JSValueRef exception = nullptr;
		m_site->OnEnterScript();
		JSValueRef value = JSEvaluateScript(m_context, source, nullptr, nullptr, 1, &exception);
		m_site->OnLeaveScript();
		JSStringRelease(source);

		if (m_interrupted) return SCRIPT_E_REPORTED;	// 利用者が止めた。エラーとしては知らせない
		if (exception) {
			ReportError(exception);
			return SCRIPT_E_REPORTED;
		}
		if (result && value) {
			VariantClear(result);
			ToVariant(m_context, value, result);
		}
		return S_OK;
	}

private:
	struct Item {
		std::wstring name;
		DWORD flags;
		IDispatch* dispatch;
	};

	~JScriptEngine() { Close(); }

	static bool ShouldTerminate(JSContextRef, void* self)
	{
		auto* engine = static_cast<JScriptEngine*>(self);
		if (engine->m_interrupted) return true;
		// false を返しても次は呼ばれないので、続けて確認するには設定し直す
		JSContextGroupSetExecutionTimeLimit(engine->m_group, kInterruptPollSeconds, ShouldTerminate, engine);
		return false;
	}

	static JSValueRef GetGlobalMember(JSContextRef ctx, JSObjectRef object, JSStringRef name, JSValueRef*)
	{
		auto* self = static_cast<JScriptEngine*>(JSObjectGetPrivate(object));
		// このコールバックは通常の検索より先に呼ばれる。スクリプトの変数と JS 標準のものを優先するため、
		// 先に通常の検索をさせて、見つからないときだけマクロ関数を探す
		if (self->m_inGlobalLookup) return nullptr;
		self->m_inGlobalLookup = true;
		const bool found = JSObjectHasProperty(ctx, object, name);
		self->m_inGlobalLookup = false;
		if (found) return nullptr;
		for (const Item& item : self->m_items) {
			if (!(item.flags & SCRIPTITEM_GLOBALMEMBERS)) continue;
			if (JSValueRef method = ResolveMethod(ctx, item.dispatch, name)) return method;
		}
		return nullptr;
	}

	static JSClassRef GlobalMemberClass()
	{
		static JSClassRef cls = [] {
			JSClassDefinition def = kJSClassDefinitionEmpty;
			def.className = "SakuraGlobalMembers";
			def.getProperty = GetGlobalMember;
			return JSClassCreate(&def);
		}();
		return cls;
	}

	bool CreateContext()
	{
		for (Item& item : m_items) {
			IUnknown* unknown = nullptr;
			if (FAILED(m_site->GetItemInfo(item.name.c_str(), SCRIPTINFO_IUNKNOWN, &unknown, nullptr)) || !unknown) return false;
			const HRESULT hr = unknown->QueryInterface(__uuidof(IDispatch), reinterpret_cast<void**>(&item.dispatch));
			unknown->Release();
			if (FAILED(hr)) return false;
		}
		m_group = JSContextGroupCreate();
		JSContextGroupSetExecutionTimeLimit(m_group, kInterruptPollSeconds, ShouldTerminate, this);
		m_context = JSGlobalContextCreateInGroup(m_group, GlobalMemberClass());

		JSObjectRef global = JSContextGetGlobalObject(m_context);
		JSObjectSetPrivate(global, this);

		for (const Item& item : m_items) {
			if (!(item.flags & SCRIPTITEM_ISVISIBLE)) continue;
			JSStringRef name = ToJSString(item.name.data(), item.name.size());
			JSObjectSetProperty(m_context, global, name, JSObjectMake(m_context, ItemClass(), item.dispatch),
				kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete, nullptr);
			JSStringRelease(name);
		}
		return true;
	}

	void ReportError(JSValueRef exception)
	{
		ULONG line = 0;
		if (JSValueIsObject(m_context, exception)) {
			JSObjectRef object = JSValueToObject(m_context, exception, nullptr);
			JSStringRef key = JSStringCreateWithUTF8CString("line");
			JSValueRef value = JSObjectGetProperty(m_context, object, key, nullptr);
			JSStringRelease(key);
			const double n = value ? JSValueToNumber(m_context, value, nullptr) : 0;
			if (n >= 1) line = static_cast<ULONG>(n) - 1;
		}
		ScriptError* error = new ScriptError(ValueToText(m_context, exception), line);
		m_site->OnScriptError(error);
		error->Release();
	}

	std::atomic<ULONG> m_refs{1};
	std::atomic<bool> m_interrupted{false};
	bool m_inGlobalLookup = false;
	IActiveScriptSite* m_site = nullptr;
	SCRIPTSTATE m_state = SCRIPTSTATE_UNINITIALIZED;
	std::vector<Item> m_items;
	JSContextGroupRef m_group = nullptr;
	JSGlobalContextRef m_context = nullptr;
};

}  // namespace

HRESULT WINAPI CLSIDFromProgID(LPCOLESTR progId, LPCLSID clsid)
{
	if (std::wcscmp(progId, L"JScript") != 0) return REGDB_E_CLASSNOTREG;
	*clsid = kClsidJScript;
	return S_OK;
}

HRESULT WINAPI CoCreateInstance(REFCLSID clsid, LPUNKNOWN, DWORD, REFIID riid, LPVOID* out)
{
	*out = nullptr;
	if (!IsEqualCLSID(clsid, kClsidJScript)) return REGDB_E_CLASSNOTREG;
	IActiveScript* engine = new JScriptEngine;
	const HRESULT hr = engine->QueryInterface(riid, out);
	engine->Release();
	return hr;
}
