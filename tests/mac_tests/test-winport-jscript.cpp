/*
	test-winport-jscript.cpp — JavaScriptCore による JScript エンジンのテスト（mac 版独自）

	上流の CWSHClient と同じ手順でエンジンを使う。

	SPDX-License-Identifier: Zlib
*/
#include "pch.h"

#include <activscp.h>
#include <oleauto.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace {

enum { kEcho, kAdd, kFail, kSeen };

//! CIfObj と同じ、名前を大文字小文字を区別せず解決する IDispatch
class FakeDispatch final : public IDispatch {
public:
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override
	{
		if (IsEqualIID(riid, __uuidof(IDispatch)) || IsEqualIID(riid, __uuidof(IUnknown))) {
			*out = this;
			AddRef();
			return S_OK;
		}
		*out = nullptr;
		return E_NOINTERFACE;
	}
	ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
	ULONG STDMETHODCALLTYPE Release() override { return --refs; }
	HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT*) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo**) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR* names, UINT, LCID, DISPID* id) override
	{
		static const wchar_t* const kNames[] = {L"Echo", L"Add", L"Fail", L"Seen"};
		for (int i = 0; i < 4; ++i) {
			if (_wcsicmp(names[0], kNames[i]) == 0) {
				*id = i;
				return S_OK;
			}
		}
		return DISP_E_UNKNOWNNAME;
	}
	HRESULT STDMETHODCALLTYPE Invoke(DISPID id, REFIID, LCID, WORD, DISPPARAMS* params, VARIANT* result, EXCEPINFO*, UINT*) override
	{
		switch (id) {
		case kEcho:
			return ::VariantCopy(result, &params->rgvarg[0]);
		case kAdd:	// DISPPARAMS は逆順
			last_first = params->rgvarg[1].lVal;
			result->vt = VT_I4;
			result->lVal = params->rgvarg[1].lVal + params->rgvarg[0].lVal;
			return S_OK;
		case kFail:
			return E_FAIL;
		default:
			seen.push_back(params->rgvarg[0]);
			if (params->rgvarg[0].vt == VT_BSTR) {
				seen_text.assign(params->rgvarg[0].bstrVal, ::SysStringLen(params->rgvarg[0].bstrVal));
			}
			return S_OK;
		}
	}
	std::atomic<ULONG> refs{1};
	int last_first = 0;
	std::vector<VARIANT> seen;
	std::wstring seen_text;
};

class FakeSite final : public IActiveScriptSite {
public:
	explicit FakeSite(FakeDispatch* d) : dispatch(d) {}
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** out) override { *out = nullptr; return E_NOINTERFACE; }
	ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
	ULONG STDMETHODCALLTYPE Release() override { return --refs; }
	HRESULT STDMETHODCALLTYPE GetLCID(LCID*) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE GetItemInfo(LPCOLESTR, DWORD, IUnknown** unknown, ITypeInfo**) override
	{
		*unknown = dispatch;
		dispatch->AddRef();
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetDocVersionString(BSTR*) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE OnScriptTerminate(const VARIANT*, const EXCEPINFO*) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE OnStateChange(SCRIPTSTATE) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE OnScriptError(IActiveScriptError* error) override
	{
		EXCEPINFO info;
		error->GetExceptionInfo(&info);
		message.assign(info.bstrDescription, ::SysStringLen(info.bstrDescription));
		::SysFreeString(info.bstrDescription);
		::SysFreeString(info.bstrSource);
		DWORD context;
		LONG position;
		error->GetSourcePosition(&context, &line, &position);
		++errors;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE OnEnterScript() override { return S_OK; }
	HRESULT STDMETHODCALLTYPE OnLeaveScript() override { return S_OK; }

	std::atomic<ULONG> refs{1};
	FakeDispatch* dispatch;
	std::wstring message;
	ULONG line = 0;
	int errors = 0;
};

struct Engine {
	FakeDispatch dispatch;
	FakeSite site{&dispatch};
	IActiveScript* script = nullptr;
	IActiveScriptParse* parser = nullptr;

	Engine()
	{
		CLSID clsid;
		EXPECT_EQ(S_OK, ::CLSIDFromProgID(L"JScript", &clsid));
		EXPECT_EQ(S_OK, ::CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IActiveScript), reinterpret_cast<void**>(&script)));
		EXPECT_EQ(S_OK, script->SetScriptSite(&site));
		EXPECT_EQ(S_OK, script->QueryInterface(__uuidof(IActiveScriptParse), reinterpret_cast<void**>(&parser)));
		EXPECT_EQ(S_OK, parser->InitNew());
		EXPECT_EQ(S_OK, script->AddNamedItem(L"Editor", SCRIPTITEM_ISVISIBLE | SCRIPTITEM_GLOBALMEMBERS));
		EXPECT_EQ(S_OK, script->SetScriptState(SCRIPTSTATE_STARTED));
	}
	~Engine()
	{
		script->Close();
		parser->Release();
		script->Release();
		for (VARIANT& v : dispatch.seen) (void)v;
	}
	HRESULT Run(const wchar_t* code)
	{
		return parser->ParseScriptText(code, nullptr, nullptr, nullptr, 0, 0, SCRIPTTEXT_ISVISIBLE, nullptr, nullptr);
	}
};

}  // namespace

TEST(WinportJScript, OnlyJScriptIsRegistered)
{
	CLSID clsid;
	EXPECT_EQ(REGDB_E_CLASSNOTREG, ::CLSIDFromProgID(L"VBScript", &clsid));
}

// CWSHMacroManager::Creator の引き方
TEST(WinportJScript, RegistryMapsJsToJScript)
{
	HKEY key = nullptr;
	ASSERT_EQ(ERROR_SUCCESS, ::RegOpenKeyExW(HKEY_CLASSES_ROOT, L".js", 0, KEY_READ, &key));
	wchar_t type[32] = {};
	DWORD size = sizeof(type);
	DWORD regType = 0;
	EXPECT_EQ(ERROR_SUCCESS, ::RegQueryValueExW(key, nullptr, nullptr, &regType, reinterpret_cast<LPBYTE>(type), &size));
	EXPECT_STREQ(L"JSFile", type);
	EXPECT_EQ(ERROR_SUCCESS, ::RegCloseKey(key));

	ASSERT_EQ(ERROR_SUCCESS, ::RegOpenKeyExW(HKEY_CLASSES_ROOT, L"JSFile\\ScriptEngine", 0, KEY_READ, &key));
	size = sizeof(type);
	EXPECT_EQ(ERROR_SUCCESS, ::RegQueryValueExW(key, nullptr, nullptr, &regType, reinterpret_cast<LPBYTE>(type), &size));
	EXPECT_STREQ(L"JScript", type);
	::RegCloseKey(key);

	EXPECT_NE(ERROR_SUCCESS, ::RegOpenKeyExW(HKEY_CLASSES_ROOT, L".vbs", 0, KEY_READ, &key));
}

TEST(WinportJScript, CallsThroughNamedItem)
{
	Engine e;
	EXPECT_EQ(S_OK, e.Run(L"Editor.Seen(Editor.Add(40, 2));"));
	ASSERT_EQ(1u, e.dispatch.seen.size());
	EXPECT_EQ(VT_I4, e.dispatch.seen[0].vt);
	EXPECT_EQ(42, e.dispatch.seen[0].lVal);
	EXPECT_EQ(40, e.dispatch.last_first);	// 引数の順序
}

TEST(WinportJScript, GlobalMembersWithoutObjectName)
{
	Engine e;
	EXPECT_EQ(S_OK, e.Run(L"Seen(Add(1, 2));"));
	ASSERT_EQ(1u, e.dispatch.seen.size());
	EXPECT_EQ(3, e.dispatch.seen[0].lVal);
}

// Windows でも CIfObj::GetIDsOfNames が大文字小文字を区別しない
TEST(WinportJScript, NamesAreCaseInsensitive)
{
	Engine e;
	EXPECT_EQ(S_OK, e.Run(L"Editor.seen(ADD(1, 1));"));
	ASSERT_EQ(1u, e.dispatch.seen.size());
	EXPECT_EQ(2, e.dispatch.seen[0].lVal);
}

TEST(WinportJScript, ScriptGlobalsWinOverMacroFunctions)
{
	Engine e;
	EXPECT_EQ(S_OK, e.Run(L"function Add(a, b) { return 100; } Seen(Add(1, 2)); Seen(Math.max(1, 2));"));
	ASSERT_EQ(2u, e.dispatch.seen.size());
	EXPECT_EQ(100, e.dispatch.seen[0].lVal);
	EXPECT_EQ(2, e.dispatch.seen[1].lVal);
}

TEST(WinportJScript, StringsKeepSurrogatesAndNul)
{
	Engine e;
	EXPECT_EQ(S_OK, e.Run(L"Seen(Echo('あ\\ud83d\\ude00\\u0000b'));"));
	ASSERT_EQ(5u, e.dispatch.seen_text.size());
	EXPECT_EQ(wchar_t(0x3042), e.dispatch.seen_text[0]);
	EXPECT_EQ(wchar_t(0xD83D), e.dispatch.seen_text[1]);
	EXPECT_EQ(wchar_t(0xDE00), e.dispatch.seen_text[2]);
	EXPECT_EQ(L'\0', e.dispatch.seen_text[3]);
}

TEST(WinportJScript, ValueTypes)
{
	Engine e;
	EXPECT_EQ(S_OK, e.Run(L"Seen(1.5); Seen(true); Seen(null); Seen(undefined); Seen([1, 2]);"));
	ASSERT_EQ(5u, e.dispatch.seen.size());
	EXPECT_EQ(VT_R8, e.dispatch.seen[0].vt);
	EXPECT_EQ(VT_BOOL, e.dispatch.seen[1].vt);
	EXPECT_EQ(VT_NULL, e.dispatch.seen[2].vt);
	EXPECT_EQ(VT_EMPTY, e.dispatch.seen[3].vt);
	EXPECT_EQ(VT_BSTR, e.dispatch.seen[4].vt);
	EXPECT_EQ(L"1,2", e.dispatch.seen_text);
}

TEST(WinportJScript, RuntimeErrorReportsLine)
{
	Engine e;
	EXPECT_EQ(SCRIPT_E_REPORTED, e.Run(L"var a = 1;\nvar b = 2;\nnoSuchFunction();"));
	EXPECT_EQ(1, e.site.errors);
	EXPECT_EQ(2u, e.site.line);	// 0 始まり
	EXPECT_NE(std::wstring::npos, e.site.message.find(L"noSuchFunction"));
}

TEST(WinportJScript, SyntaxErrorIsReported)
{
	Engine e;
	EXPECT_EQ(SCRIPT_E_REPORTED, e.Run(L"\nif ("));
	EXPECT_EQ(1, e.site.errors);
}

TEST(WinportJScript, FailedFunctionThrowsCatchableError)
{
	Engine e;
	EXPECT_EQ(S_OK, e.Run(L"try { Fail(); } catch (x) { Seen('caught'); }"));
	EXPECT_EQ(L"caught", e.dispatch.seen_text);
	EXPECT_EQ(0, e.site.errors);
}

// CWSH.cpp の中止ダイアログは別スレッドから InterruptScriptThread を呼ぶ
TEST(WinportJScript, InterruptStopsInfiniteLoopSilently)
{
	Engine e;
	std::thread stopper([&] {
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
		e.script->InterruptScriptThread(SCRIPTTHREADID_BASE, nullptr, 0);
	});
	EXPECT_EQ(SCRIPT_E_REPORTED, e.Run(L"while (true) {}"));
	stopper.join();
	EXPECT_EQ(0, e.site.errors);
}
