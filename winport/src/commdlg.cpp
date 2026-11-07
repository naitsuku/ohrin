/*
	commdlg.cpp — コモンダイアログ（GetOpenFileName / GetSaveFileName / ChooseFont / ChooseColor / IFileOpenDialog）

	画面は mac 標準のパネル。上流のフック（文字コード・改行・BOM の選択）は、Windows と同じテンプレートの
	ダイアログを非表示のまま動かし、その中のコンボボックスとチェックボックスをパネルの補助ビューへ写す。
	OK のときに値を書き戻して CDN_FILEOK を送るので、値の解釈は上流のコードのままになる。

	SPDX-License-Identifier: Zlib
*/
#include "user32_internal.h"

#include <commdlg.h>
#include <shobjidl.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cwchar>

#include <dlgs.h>

#include "backend.h"
#include "winport_internal.h"

using namespace winport;

namespace {

std::u16string U16(const std::wstring& s) { return std::u16string(s.begin(), s.end()); }
std::wstring W(const std::u16string& s) { return std::wstring(s.begin(), s.end()); }

//! フックが CommDlg_OpenSave_* で問い合わせる内容
struct PanelState {
	std::wstring path;
	std::wstring Folder() const { const auto p = path.rfind(L'/'); return p == std::wstring::npos ? L"" : path.substr(0, p); }
	std::wstring Spec() const { const auto p = path.rfind(L'/'); return p == std::wstring::npos ? path : path.substr(p + 1); }
};
thread_local PanelState* t_panel = nullptr;

LRESULT CopyOut(const std::wstring& s, WPARAM cch, LPARAM buf)
{
	if (buf && cch) wcsncpy_s(reinterpret_cast<LPWSTR>(buf), cch, s.c_str(), _TRUNCATE);
	return static_cast<LRESULT>(s.size()) + 1;
}

//! フックのダイアログの親（Windows の「開く」ダイアログ本体に当たる）。表示はしない
LRESULT CALLBACK FrameProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (t_panel) {
		switch (msg) {
		case CDM_GETSPEC: return CopyOut(t_panel->Spec(), wParam, lParam);
		case CDM_GETFILEPATH: return CopyOut(t_panel->path, wParam, lParam);
		case CDM_GETFOLDERPATH: return CopyOut(t_panel->Folder(), wParam, lParam);
		case CDM_SETCONTROLTEXT: case CDM_HIDECONTROL: case CDM_SETDEFEXT: return 0;
		default: break;
		}
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

HWND CreateFrame(HWND owner)
{
	// 仮想プロセスごとに登録が要る。同じプロセスからの 2 回目は失敗するが、登録済みなので問題ない
	WNDCLASSEXW wc{sizeof(wc)};
	wc.lpfnWndProc = FrameProc;
	wc.lpszClassName = L"winport_filedialog";
	RegisterClassExW(&wc);
	return CreateWindowExW(0, L"winport_filedialog", L"", WS_POPUP, 0, 0, 10, 10, owner, nullptr, nullptr, nullptr);
}

std::wstring TextOf(HWND h)
{
	const int n = GetWindowTextLengthW(h);
	std::wstring s(static_cast<size_t>(n) + 1, L'\0');
	GetWindowTextW(h, s.data(), n + 1);
	s.resize(static_cast<size_t>(n));
	std::wstring out;
	for (wchar_t c : s) if (c != L'&') out += c;
	while (!out.empty() && out.back() == L':') out.pop_back();
	return out;
}

void CollectCheck(HWND h, std::vector<backend::PanelControl>* out)
{
	backend::PanelControl c;
	c.id = GetDlgCtrlID(h);
	c.check = true;
	c.label = U16(TextOf(h));
	c.value = static_cast<int>(SendMessageW(h, BM_GETCHECK, 0, 0));
	c.enabled = IsWindowEnabled(h) != FALSE;
	out->push_back(std::move(c));
}

//! フックのダイアログのコントロールを、作成順に読み取る
std::vector<backend::PanelControl> CollectControls(HWND hook)
{
	std::vector<backend::PanelControl> out;
	std::vector<HWND> children;
	{
		std::lock_guard lock(UserMutex());
		children = GetWindowObject(hook)->children;
	}
	std::reverse(children.begin(), children.end());  // 子のリストは新しい順
	std::wstring label;
	for (HWND h : children) {
		const DWORD style = GetWindowLongW(h, GWL_STYLE);
		wchar_t cls[32] = {};
		GetClassNameW(h, cls, 32);
		if (!_wcsicmp(cls, L"STATIC")) {
			const std::wstring t = TextOf(h);
			if (!t.empty()) label = t;
			continue;
		}
		if (!(style & WS_VISIBLE)) continue;
		if (!_wcsicmp(cls, L"BUTTON")) {
			// CP は一覧を増やすボタンで、値としては使われない
			if ((style & BS_TYPEMASK) == BS_AUTOCHECKBOX && TextOf(h) != L"CP") CollectCheck(h, &out);
		} else if (!_wcsicmp(cls, L"COMBOBOX")) {
			backend::PanelControl c;
			c.id = GetDlgCtrlID(h);
			c.label = U16(label);
			const int n = static_cast<int>(SendMessageW(h, CB_GETCOUNT, 0, 0));
			for (int i = 0; i < n; ++i) {
				std::wstring item(static_cast<size_t>(SendMessageW(h, CB_GETLBTEXTLEN, i, 0)) + 1, L'\0');
				SendMessageW(h, CB_GETLBTEXT, i, reinterpret_cast<LPARAM>(item.data()));
				item.resize(item.size() - 1);
				c.items.push_back(U16(item));
			}
			c.value = static_cast<int>(SendMessageW(h, CB_GETCURSEL, 0, 0));
			c.enabled = IsWindowEnabled(h) != FALSE;
			if (n) out.push_back(std::move(c));
		}
	}
	return out;
}

void ApplyControls(HWND hook, const std::vector<backend::PanelControl>& controls, const std::vector<int>& values)
{
	for (size_t i = 0; i < controls.size() && i < values.size(); ++i) {
		HWND h = GetDlgItem(hook, controls[i].id);
		if (!h) h = GetDlgItem(GetParent(hook), controls[i].id);
		if (!h || values[i] == controls[i].value) continue;
		const HWND parent = GetParent(h);
		if (controls[i].check) {
			SendMessageW(h, BM_SETCHECK, values[i], 0);
			SendMessageW(parent, WM_COMMAND, MAKEWPARAM(controls[i].id, BN_CLICKED), reinterpret_cast<LPARAM>(h));
		} else {
			SendMessageW(h, CB_SETCURSEL, values[i], 0);
			SendMessageW(parent, WM_COMMAND, MAKEWPARAM(controls[i].id, CBN_SELCHANGE), reinterpret_cast<LPARAM>(h));
		}
	}
}

//! パネルが閉じるまで、再描画と送信メッセージを処理しながら待つ
template <typename Done>
void WaitModal(Done done)
{
	while (!done()) {
		MSG m;
		while (PeekMessageW(&m, nullptr, WM_PAINT, WM_PAINT, PM_REMOVE)) DispatchMessageW(&m);
		Sleep(10);
	}
}

void SendFileNotify(HWND hook, HWND frame, OPENFILENAMEW* ofn, UINT code)
{
	OFNOTIFYW n{};
	n.hdr = {frame, 0, code};
	n.lpOFN = ofn;
	n.pszFile = ofn->lpstrFile;
	SendMessageW(hook, WM_NOTIFY, 0, reinterpret_cast<LPARAM>(&n));
}

void StoreResult(OPENFILENAMEW* ofn, const std::vector<std::u16string>& paths)
{
	std::wstring out;
	const std::wstring first = W(paths[0]);
	const size_t slash = first.rfind(L'/');
	size_t offset = slash == std::wstring::npos ? 0 : slash + 1;
	if (paths.size() > 1) {
		// 複数選択: フォルダー \0 名前 \0 名前 \0 \0
		out = first.substr(0, slash);
		offset = out.size() + 1;
		out += L'\0';
		for (const auto& p : paths) {
			const std::wstring f = W(p);
			out += f.substr(f.rfind(L'/') + 1);
			out += L'\0';
		}
	} else {
		out = first;
		out += L'\0';
	}
	out += L'\0';
	const size_t n = std::min<size_t>(out.size(), ofn->nMaxFile);
	wmemcpy(ofn->lpstrFile, out.data(), n);
	if (n) { ofn->lpstrFile[n - 1] = 0; if (n > 1 && paths.size() > 1) ofn->lpstrFile[n - 2] = 0; }
	ofn->nFileOffset = static_cast<WORD>(offset);
	const size_t dot = first.rfind(L'.');
	ofn->nFileExtension = static_cast<WORD>(dot != std::wstring::npos && dot > offset ? dot + 1 : 0);
}

WINBOOL RunFilePanel(OPENFILENAMEW* ofn, bool save)
{
	if (!backend::Available()) return FALSE;
	const HWND owner = ofn->hwndOwner ? GetAncestor(ofn->hwndOwner, GA_ROOT) : nullptr;
	PanelState state;
	t_panel = &state;

	HWND frame = nullptr, hook = nullptr;
	if ((ofn->Flags & OFN_ENABLEHOOK) && ofn->lpfnHook) {
		frame = CreateFrame(owner);
		if (!(ofn->Flags & OFN_HIDEREADONLY)) {
			CreateWindowExW(0, L"BUTTON", L"読み取り専用", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0, 10, 10, frame,
			                reinterpret_cast<HMENU>(static_cast<INT_PTR>(chx1)), nullptr, nullptr);
		}
		if (ofn->Flags & OFN_ENABLETEMPLATE) {
			HRSRC res = FindResourceW(ofn->hInstance, ofn->lpTemplateName, reinterpret_cast<LPCWSTR>(RT_DIALOG));
			if (res) {
				hook = CreateDialogIndirectParamW(ofn->hInstance, static_cast<LPCDLGTEMPLATEW>(LockResource(LoadResource(ofn->hInstance, res))), frame,
				                                  reinterpret_cast<DLGPROC>(ofn->lpfnHook), reinterpret_cast<LPARAM>(ofn));
			}
		}
		if (hook) SendFileNotify(hook, frame, ofn, CDN_INITDONE);
	}

	backend::PanelRequest req;
	req.save = save;
	req.multiple = (ofn->Flags & OFN_ALLOWMULTISELECT) != 0;
	if (ofn->lpstrTitle) req.title = U16(ofn->lpstrTitle);
	std::wstring initial = ofn->lpstrFile ? ofn->lpstrFile : L"";
	if (const size_t slash = initial.rfind(L'/'); slash != std::wstring::npos) {
		req.directory = U16(initial.substr(0, slash));
		initial = initial.substr(slash + 1);
	} else if (ofn->lpstrInitialDir) {
		req.directory = U16(ofn->lpstrInitialDir);
	}
	req.name = U16(initial);
	if (hook) {
		req.controls = CollectControls(hook);
		if (HWND ro = GetDlgItem(frame, chx1)) CollectCheck(ro, &req.controls);
	}

	const bool ownerEnabled = owner && IsWindowEnabled(owner);
	if (ownerEnabled) EnableWindow(owner, FALSE);
	bool ok = false;
	for (;;) {
		backend::PanelResult result;
		backend::ShowFilePanel(&req, &result);
		WaitModal([&] { return result.done.load(); });
		if (!result.ok || result.paths.empty()) break;
		StoreResult(ofn, result.paths);
		state.path = W(result.paths[0]);
		if (!hook) { ok = true; break; }
		ApplyControls(hook, req.controls, result.values);
		SetWindowLongPtrW(hook, DWLP_MSGRESULT, 0);
		SendFileNotify(hook, frame, ofn, CDN_FILEOK);
		if (!GetWindowLongPtrW(hook, DWLP_MSGRESULT)) { ok = true; break; }
		// フックが拒否したらパネルへ戻る
		for (size_t i = 0; i < req.controls.size() && i < result.values.size(); ++i) req.controls[i].value = result.values[i];
	}
	if (ownerEnabled) EnableWindow(owner, TRUE);
	if (frame) DestroyWindow(frame);
	t_panel = nullptr;
	if (owner && IsWindowVisible(owner)) SetForegroundWindow(owner);
	return ok;
}

}  // namespace

extern "C" {

WINBOOL WINAPI GetOpenFileNameW(LPOPENFILENAMEW ofn) { return RunFilePanel(ofn, false); }
WINBOOL WINAPI GetSaveFileNameW(LPOPENFILENAMEW ofn) { return RunFilePanel(ofn, true); }
DWORD WINAPI CommDlgExtendedError(void) { return 0; }

WINBOOL WINAPI ChooseFontW(LPCHOOSEFONTW cf)
{
	if (!backend::Available() || !cf->lpLogFont) return FALSE;
	LOGFONTW& lf = *cf->lpLogFont;
	backend::FontChoice choice;
	choice.family = U16(lf.lfFaceName);
	choice.size = lf.lfHeight ? std::abs(lf.lfHeight) : 12;
	choice.bold = lf.lfWeight >= FW_BOLD;
	choice.italic = lf.lfItalic != 0;
	const HWND owner = cf->hwndOwner ? GetAncestor(cf->hwndOwner, GA_ROOT) : nullptr;
	const bool ownerEnabled = owner && IsWindowEnabled(owner);
	if (ownerEnabled) EnableWindow(owner, FALSE);
	std::atomic<int> result{0};
	backend::ShowFontPanel(&choice, &result);
	WaitModal([&] { return result.load() != 0; });
	if (ownerEnabled) EnableWindow(owner, TRUE);
	if (owner && IsWindowVisible(owner)) SetForegroundWindow(owner);
	if (result.load() != 1) return FALSE;
	const std::wstring family = W(choice.family);
	wcsncpy_s(lf.lfFaceName, LF_FACESIZE, family.c_str(), _TRUNCATE);
	lf.lfHeight = -choice.size;
	lf.lfWidth = 0;
	lf.lfWeight = choice.bold ? FW_BOLD : FW_NORMAL;
	lf.lfItalic = choice.italic;
	cf->iPointSize = choice.size * 10;
	return TRUE;
}

WINBOOL WINAPI ChooseColorW(LPCHOOSECOLORW cc)
{
	if (!backend::Available()) return FALSE;
	const COLORREF initial = (cc->Flags & CC_RGBINIT) ? cc->rgbResult : 0;
	unsigned rgb = GetRValue(initial) << 16 | GetGValue(initial) << 8 | GetBValue(initial);
	const HWND owner = cc->hwndOwner ? GetAncestor(cc->hwndOwner, GA_ROOT) : nullptr;
	const bool ownerEnabled = owner && IsWindowEnabled(owner);
	if (ownerEnabled) EnableWindow(owner, FALSE);
	std::atomic<int> result{0};
	backend::ShowColorPanel(&rgb, &result);
	WaitModal([&] { return result.load() != 0; });
	if (ownerEnabled) EnableWindow(owner, TRUE);
	if (owner && IsWindowVisible(owner)) SetForegroundWindow(owner);
	if (result.load() != 1) return FALSE;
	cc->rgbResult = RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
	return TRUE;
}

}  // extern "C"

// ---- IFileOpenDialog ----
// 上流の SelectDir がフォルダーの選択に使う。使わないメソッドは E_NOTIMPL を返す
namespace {

class WpShellItem final : public IShellItem {
public:
	explicit WpShellItem(std::wstring path) : path_(std::move(path)) {}
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override
	{
		*out = nullptr;
		if (!IsEqualIID(riid, IID_IUnknown) && !IsEqualIID(riid, IID_IShellItem)) return E_NOINTERFACE;
		AddRef();
		*out = this;
		return S_OK;
	}
	ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
	ULONG STDMETHODCALLTYPE Release() override
	{
		const ULONG n = --refs_;
		if (!n) delete this;
		return n;
	}
	HRESULT STDMETHODCALLTYPE BindToHandler(IBindCtx*, REFGUID, REFIID, void**) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE GetParent(IShellItem**) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE GetDisplayName(SIGDN, LPWSTR* name) override
	{
		*name = static_cast<LPWSTR>(CoTaskMemAlloc((path_.size() + 1) * sizeof(wchar_t)));
		if (!*name) return E_OUTOFMEMORY;
		std::wmemcpy(*name, path_.c_str(), path_.size() + 1);
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetAttributes(SFGAOF, SFGAOF*) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE Compare(IShellItem*, SICHINTF, int*) override { return E_NOTIMPL; }
	const std::wstring& Path() const { return path_; }

private:
	std::wstring path_;
	std::atomic<ULONG> refs_{1};
};

class WpFileOpenDialog final : public IFileOpenDialog {
public:
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override
	{
		*out = nullptr;
		if (!IsEqualIID(riid, IID_IUnknown) && !IsEqualIID(riid, IID_IModalWindow) && !IsEqualIID(riid, IID_IFileDialog) && !IsEqualIID(riid, IID_IFileOpenDialog)) return E_NOINTERFACE;
		AddRef();
		*out = this;
		return S_OK;
	}
	ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
	ULONG STDMETHODCALLTYPE Release() override
	{
		const ULONG n = --refs_;
		if (!n) delete this;
		return n;
	}
	HRESULT STDMETHODCALLTYPE Show(HWND parent) override
	{
		backend::PanelRequest request;
		request.folders = (options_ & FOS_PICKFOLDERS) != 0;
		request.title = U16(title_);
		request.directory = U16(folder_);
		backend::PanelResult result;
		const HWND owner = parent ? GetAncestor(parent, GA_ROOT) : nullptr;
		const bool ownerEnabled = owner && IsWindowEnabled(owner);
		if (ownerEnabled) EnableWindow(owner, FALSE);
		backend::ShowFilePanel(&request, &result);
		WaitModal([&] { return result.done.load(); });
		if (ownerEnabled) EnableWindow(owner, TRUE);
		if (owner && IsWindowVisible(owner)) SetForegroundWindow(owner);
		if (!result.ok || result.paths.empty()) return HRESULT_FROM_WIN32(ERROR_CANCELLED);
		result_ = W(result.paths[0]);
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE SetOptions(FILEOPENDIALOGOPTIONS options) override { options_ = options; return S_OK; }
	HRESULT STDMETHODCALLTYPE GetOptions(FILEOPENDIALOGOPTIONS* options) override { *options = options_; return S_OK; }
	HRESULT STDMETHODCALLTYPE SetFolder(IShellItem* item) override
	{
		if (item) folder_ = static_cast<WpShellItem*>(item)->Path();
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE SetDefaultFolder(IShellItem* item) override { return SetFolder(item); }
	HRESULT STDMETHODCALLTYPE SetTitle(LPCWSTR title) override { title_ = title ? title : L""; return S_OK; }
	HRESULT STDMETHODCALLTYPE GetResult(IShellItem** item) override
	{
		if (result_.empty()) return E_UNEXPECTED;
		*item = new WpShellItem(result_);
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE SetFileTypes(UINT, const COMDLG_FILTERSPEC*) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE SetFileTypeIndex(UINT) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE GetFileTypeIndex(UINT*) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE Advise(IFileDialogEvents*, DWORD*) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE Unadvise(DWORD) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE GetFolder(IShellItem**) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE GetCurrentSelection(IShellItem**) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE SetFileName(LPCWSTR) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE GetFileName(LPWSTR*) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE SetOkButtonLabel(LPCWSTR) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE SetFileNameLabel(LPCWSTR) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE AddPlace(IShellItem*, FDAP) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE SetDefaultExtension(LPCWSTR) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE Close(HRESULT) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE SetClientGuid(REFGUID) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE ClearClientData() override { return S_OK; }
	HRESULT STDMETHODCALLTYPE SetFilter(IShellItemFilter*) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE GetResults(IShellItemArray**) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE GetSelectedItems(IShellItemArray**) override { return E_NOTIMPL; }

private:
	FILEOPENDIALOGOPTIONS options_ = 0;
	std::wstring title_, folder_, result_;
	std::atomic<ULONG> refs_{1};
};

}  // namespace

namespace winport {

HRESULT CreateFileOpenDialog(REFIID riid, void** out)
{
	auto dialog = new WpFileOpenDialog;
	const HRESULT hr = dialog->QueryInterface(riid, out);
	dialog->Release();
	return hr;
}

}  // namespace winport

extern "C" HRESULT WINAPI SHCreateItemFromParsingName(PCWSTR path, IBindCtx*, REFIID riid, void** out)
{
	*out = nullptr;
	if (!path || !*path) return E_INVALIDARG;
	auto item = new WpShellItem(path);
	const HRESULT hr = item->QueryInterface(riid, out);
	item->Release();
	return hr;
}
