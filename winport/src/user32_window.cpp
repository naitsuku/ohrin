/*
	user32_window.cpp — ウィンドウクラスとウィンドウの管理、DefWindowProc

	HWND は Window オブジェクトへのポインタ。ウィンドウ木の変更は UserMutex() の下で行い、
	ウィンドウプロシージャ呼び出しの間はロックを外す。

	SPDX-License-Identifier: Zlib
*/
#include "user32_internal.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cwchar>

#include "winport_internal.h"

namespace winport {

LRESULT CallCbtHooks(int code, WPARAM wParam, LPARAM lParam);  // user32_misc.cpp

namespace {

std::map<std::wstring, std::unique_ptr<WindowClass>>& Classes()
{
	static auto* m = new std::map<std::wstring, std::unique_ptr<WindowClass>>;  // 終了時にも使われるので破棄しない
	return *m;
}

std::vector<HWND>& Windows()
{
	static auto* v = new std::vector<HWND>;
	return *v;
}

ATOM g_nextAtom = 0xC000;

std::wstring ClassKey(LPCWSTR name)
{
	if (IS_INTRESOURCE(name)) {
		// アトム指定。登録済みクラスから探す
		const ATOM atom = static_cast<ATOM>(reinterpret_cast<ULONG_PTR>(name));
		for (auto& [k, c] : Classes()) {
			if (c->atom == atom) return k;
		}
		return L"#" + std::to_wstring(atom);
	}
	std::wstring k = name;
	for (auto& ch : k) ch = static_cast<wchar_t>(std::towupper(ch));
	return k;
}

const WindowClass* FindClass(LPCWSTR name)
{
	auto& m = Classes();
	auto it = m.find(ClassKey(name));
	return it == m.end() ? nullptr : it->second.get();
}

// 未実装の組み込みコントロール等を「何もしないウィンドウ」として登録する
void EnsureSystemClasses()
{
	static bool done = false;
	if (done) return;
	done = true;
	static const wchar_t* const kNames[] = {
		L"#32770", L"BUTTON", L"EDIT", L"STATIC", L"LISTBOX", L"COMBOBOX", L"SCROLLBAR", L"COMBOLBOX",
		L"ToolbarWindow32", L"msctls_statusbar32", L"ReBarWindow32", L"SysTabControl32", L"tooltips_class32",
		L"SysListView32", L"SysTreeView32", L"msctls_updown32", L"msctls_progress32", L"msctls_trackbar32",
		L"msctls_hotkey32", L"SysHeader32", L"ComboBoxEx32", L"SysLink", L"RichEdit20W", L"Message",
	};
	for (auto n : kNames) {
		auto c = std::make_unique<WindowClass>();
		c->name = n;
		c->proc = DefWindowProcW;
		c->system = true;
		c->atom = g_nextAtom++;
		c->background = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
		Classes()[ClassKey(n)] = std::move(c);
	}
}

thread_local int t_subclassDepth = -1;  // DefSubclassProc がどこまで降りたか

}  // namespace

std::recursive_mutex& UserMutex()
{
	static auto* m = new std::recursive_mutex;  // 上流のグローバル変数の破棄中にも使われるので破棄しない
	return *m;
}

const std::vector<HWND>& AllWindows() { return Windows(); }

Window* GetWindowObject(HWND hwnd)
{
	if (!hwnd) return nullptr;
	std::lock_guard lock(UserMutex());
	auto& v = Windows();
	if (std::find(v.begin(), v.end(), hwnd) == v.end()) return nullptr;
	return reinterpret_cast<Window*>(hwnd);
}

RECT NonClientInsets(const Window* w)
{
	RECT r{};
	if (w->parent) {
		int b = 0;
		if (w->exStyle & WS_EX_CLIENTEDGE) b += 2;
		if (w->style & WS_BORDER) b += 1;
		r = {b, b, b, b};
	}
	// トップレベルの枠とタイトルバーは NSWindow が持つので、ウィンドウ矩形は「枠込み」で扱う
	if (!w->parent && !(w->style & WS_CHILD)) {
		if ((w->style & WS_CAPTION) == WS_CAPTION) r.top = backend::TitleBarHeight();
	}
	const int sb = GetSystemMetrics(SM_CXVSCROLL);
	if (w->scrollVisible[SB_VERT]) r.right += sb;
	if (w->scrollVisible[SB_HORZ]) r.bottom += sb;
	return r;
}

SIZE ClientSize(const Window* w)
{
	const RECT nc = NonClientInsets(w);
	SIZE s{(w->rect.right - w->rect.left) - nc.left - nc.right, (w->rect.bottom - w->rect.top) - nc.top - nc.bottom};
	if (s.cx < 0) s.cx = 0;
	if (s.cy < 0) s.cy = 0;
	return s;
}

POINT WindowOriginOnScreen(const Window* w)
{
	POINT p{w->rect.left, w->rect.top};
	if (w->parent) {
		const POINT c = ClientOriginOnScreen(reinterpret_cast<Window*>(w->parent));
		p.x += c.x;
		p.y += c.y;
	}
	return p;
}

POINT ClientOriginOnScreen(const Window* w)
{
	POINT p = WindowOriginOnScreen(w);
	const RECT nc = NonClientInsets(w);
	p.x += nc.left;
	p.y += nc.top;
	return p;
}

Window* TopLevelOf(Window* w)
{
	while (w && w->parent) w = reinterpret_cast<Window*>(w->parent);
	return w;
}

bool IsReallyVisible(const Window* w)
{
	for (; w; w = reinterpret_cast<const Window*>(w->parent)) {
		if (!(w->style & WS_VISIBLE)) return false;
	}
	return true;
}

std::shared_ptr<Surface> EnsureSurface(Window* w)
{
	std::lock_guard lock(UserMutex());
	const SIZE s = ClientSize(w);
	const double scale = backend::ScaleFactor();
	const int cx = std::max<int>(s.cx, 1), cy = std::max<int>(s.cy, 1);
	if (!w->surface || w->surface->width != cx || w->surface->height != cy || w->surface->scale != scale) {
		w->surface = CreateSurface(cx, cy, scale, w->surface.get());
	}
	return w->surface;
}

void AddUpdateRect(Window* w, const RECT* rc, bool erase)
{
	std::lock_guard lock(UserMutex());
	const SIZE s = ClientSize(w);
	RECT r = rc ? *rc : RECT{0, 0, s.cx, s.cy};
	RECT clip{0, 0, s.cx, s.cy};
	if (!IntersectRect(&r, &r, &clip)) return;
	if (w->updatePending) UnionRect(&w->updateRect, &w->updateRect, &r);
	else w->updateRect = r;
	w->updatePending = true;
	w->eraseBackground = w->eraseBackground || erase;
	{
		auto& q = QueueOf(w->threadId);
		std::lock_guard ql(q.mutex);
		q.paintPending = true;
	}
	WakeQueue(w->threadId);
}

LRESULT CallWindowProcChain(Window* w, HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (!w->subclasses.empty()) {
		const int saved = t_subclassDepth;
		t_subclassDepth = static_cast<int>(w->subclasses.size()) - 1;
		const Subclass sc = w->subclasses[t_subclassDepth];
		const LRESULT r = sc.proc(hwnd, msg, wParam, lParam, sc.id, sc.refData);
		t_subclassDepth = saved;
		return r;
	}
	return w->proc ? w->proc(hwnd, msg, wParam, lParam) : DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace winport

using namespace winport;

namespace {

void SendSizeMove(HWND hwnd, Window* w)
{
	SIZE s;
	RECT r;
	{
		std::lock_guard lock(UserMutex());
		s = ClientSize(w);
		r = w->rect;
	}
	SendMessageW(hwnd, WM_SIZE, IsIconic(hwnd) ? SIZE_MINIMIZED : (IsZoomed(hwnd) ? SIZE_MAXIMIZED : SIZE_RESTORED), MAKELPARAM(s.cx, s.cy));
	SendMessageW(hwnd, WM_MOVE, 0, MAKELPARAM(r.left, r.top));
}

void DestroyRecursive(HWND hwnd)
{
	auto w = GetWindowObject(hwnd);
	if (!w || w->destroying) return;
	w->destroying = true;
	SendMessageW(hwnd, WM_DESTROY, 0, 0);
	std::vector<HWND> kids;
	{
		std::lock_guard lock(UserMutex());
		kids = w->children;
	}
	for (auto k : kids) DestroyRecursive(k);
	// 所有されているポップアップも破棄する
	std::vector<HWND> owned;
	{
		std::lock_guard lock(UserMutex());
		for (auto h : Windows()) {
			auto o = reinterpret_cast<Window*>(h);
			if (o->owner == hwnd && !o->destroying) owned.push_back(h);
		}
	}
	for (auto o : owned) DestroyRecursive(o);
	SendMessageW(hwnd, WM_NCDESTROY, 0, 0);

	void* platform;
	{
		std::lock_guard lock(UserMutex());
		auto& all = Windows();
		all.erase(std::remove(all.begin(), all.end(), hwnd), all.end());
		if (w->parent) {
			auto p = reinterpret_cast<Window*>(w->parent);
			p->children.erase(std::remove(p->children.begin(), p->children.end(), hwnd), p->children.end());
		}
		auto& q = QueueOf(w->threadId);
		if (q.focus == hwnd) q.focus = nullptr;
		if (q.active == hwnd) q.active = nullptr;
		if (q.capture == hwnd) q.capture = nullptr;
		q.timers.erase(std::remove_if(q.timers.begin(), q.timers.end(), [hwnd](const Timer& t) { return t.hwnd == hwnd; }), q.timers.end());
		platform = w->platform;
		w->platform = nullptr;
		w->magic = 0;
	}
	backend::WindowDestroyed(hwnd, platform);
	delete w;
}

LONG_PTR* ExtraSlot(Window* w, int index)
{
	if (index < 0 || static_cast<size_t>(index) + sizeof(LONG_PTR) > w->extra.size()) return nullptr;
	return reinterpret_cast<LONG_PTR*>(w->extra.data() + index);
}

}  // namespace

extern "C" {

/* ---- クラス ---- */
ATOM WINAPI RegisterClassExW(const WNDCLASSEXW* wc)
{
	std::lock_guard lock(UserMutex());
	EnsureSystemClasses();
	const std::wstring key = ClassKey(wc->lpszClassName);
	if (auto it = Classes().find(key); it != Classes().end() && !it->second->system) {
		SetLastError(ERROR_CLASS_ALREADY_EXISTS);
		return 0;
	}
	auto c = std::make_unique<WindowClass>();
	c->name = IS_INTRESOURCE(wc->lpszClassName) ? key : wc->lpszClassName;
	c->style = wc->style;
	c->proc = wc->lpfnWndProc;
	c->cbClsExtra = wc->cbClsExtra;
	c->cbWndExtra = wc->cbWndExtra;
	c->instance = wc->hInstance;
	c->icon = wc->hIcon;
	c->cursor = wc->hCursor;
	c->background = wc->hbrBackground;
	c->atom = g_nextAtom++;
	const ATOM atom = c->atom;
	Classes()[key] = std::move(c);
	return atom;
}

ATOM WINAPI RegisterClassW(const WNDCLASSW* wc)
{
	WNDCLASSEXW ex{sizeof(ex), wc->style, wc->lpfnWndProc, wc->cbClsExtra, wc->cbWndExtra, wc->hInstance,
	               wc->hIcon, wc->hCursor, wc->hbrBackground, wc->lpszMenuName, wc->lpszClassName, nullptr};
	return RegisterClassExW(&ex);
}

WINBOOL WINAPI UnregisterClassW(LPCWSTR lpClassName, HINSTANCE)
{
	std::lock_guard lock(UserMutex());
	return Classes().erase(ClassKey(lpClassName)) ? TRUE : FALSE;
}

WINBOOL WINAPI GetClassInfoExW(HINSTANCE, LPCWSTR lpszClass, LPWNDCLASSEXW lpwcx)
{
	std::lock_guard lock(UserMutex());
	EnsureSystemClasses();
	auto c = FindClass(lpszClass);
	if (!c) { SetLastError(ERROR_CLASS_DOES_NOT_EXIST); return FALSE; }
	lpwcx->style = c->style;
	lpwcx->lpfnWndProc = c->proc;
	lpwcx->cbClsExtra = c->cbClsExtra;
	lpwcx->cbWndExtra = c->cbWndExtra;
	lpwcx->hInstance = c->instance;
	lpwcx->hIcon = c->icon;
	lpwcx->hCursor = c->cursor;
	lpwcx->hbrBackground = c->background;
	lpwcx->lpszClassName = c->name.c_str();
	return c->atom;
}

WINBOOL WINAPI GetClassInfoW(HINSTANCE hInstance, LPCWSTR lpClassName, LPWNDCLASSW lpWndClass)
{
	WNDCLASSEXW ex{sizeof(ex)};
	if (!GetClassInfoExW(hInstance, lpClassName, &ex)) return FALSE;
	*lpWndClass = WNDCLASSW{ex.style, ex.lpfnWndProc, ex.cbClsExtra, ex.cbWndExtra, ex.hInstance, ex.hIcon, ex.hCursor,
	                        ex.hbrBackground, ex.lpszMenuName, ex.lpszClassName};
	return TRUE;
}

int WINAPI GetClassNameW(HWND hWnd, LPWSTR lpClassName, int nMaxCount)
{
	auto w = GetWindowObject(hWnd);
	if (!w || nMaxCount <= 0) return 0;
	const int n = std::min<int>(static_cast<int>(w->cls->name.size()), nMaxCount - 1);
	std::wmemcpy(lpClassName, w->cls->name.c_str(), n);
	lpClassName[n] = 0;
	return n;
}

/* ---- 生成・破棄 ---- */
HWND WINAPI CreateWindowExW(DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle,
                            int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu,
                            HINSTANCE hInstance, LPVOID lpParam)
{
	Window* w = nullptr;
	{
		std::lock_guard lock(UserMutex());
		EnsureSystemClasses();
		auto cls = FindClass(lpClassName);
		if (!cls) {
			ReportUnimplemented("user32", (std::string("CreateWindowExW(class ") + WideToUtf8(IS_INTRESOURCE(lpClassName) ? L"#atom" : lpClassName) + ")").c_str());
			SetLastError(ERROR_CANNOT_FIND_WND_CLASS);
			return nullptr;
		}
		if (hWndParent == HWND_MESSAGE) hWndParent = nullptr;
		w = new Window;
		w->cls = cls;
		w->proc = cls->proc;
		w->style = dwStyle;
		w->exStyle = dwExStyle;
		w->text = lpWindowName ? lpWindowName : L"";
		w->instance = hInstance;
		w->extra.assign(cls->cbWndExtra, 0);
		w->threadId = GetCurrentThreadId();
		w->processId = GetCurrentProcessId();
		if ((dwStyle & WS_CHILD) && hWndParent) {
			w->parent = hWndParent;
			w->id = reinterpret_cast<UINT_PTR>(hMenu);
		} else {
			w->owner = hWndParent;
			w->menu = hMenu;
			if (!(dwStyle & WS_CHILD) && !(dwStyle & WS_POPUP)) w->style |= WS_CLIPSIBLINGS | WS_CAPTION;
		}
		if (X == CW_USEDEFAULT) { X = w->parent ? 0 : 120; Y = w->parent ? 0 : 80; }
		if (nWidth == CW_USEDEFAULT) { nWidth = w->parent ? 0 : 1000; nHeight = w->parent ? 0 : 720; }
		w->rect = {X, Y, X + nWidth, Y + nHeight};
		w->style &= ~WS_VISIBLE;  // 表示は WM_CREATE の後で行う
		Windows().push_back(ToHwnd(w));
		if (w->parent) reinterpret_cast<Window*>(w->parent)->children.insert(reinterpret_cast<Window*>(w->parent)->children.begin(), ToHwnd(w));
	}
	const HWND hwnd = ToHwnd(w);
	CREATESTRUCTW cs{lpParam, hInstance, hMenu, hWndParent, nHeight, nWidth, Y, X, static_cast<LONG>(dwStyle), lpWindowName, lpClassName, dwExStyle};
	CBT_CREATEWNDW cbt{&cs, HWND_TOP};
	if (CallCbtHooks(HCBT_CREATEWND, reinterpret_cast<WPARAM>(hwnd), reinterpret_cast<LPARAM>(&cbt))) {
		DestroyRecursive(hwnd);
		return nullptr;
	}
	if (!SendMessageW(hwnd, WM_NCCREATE, 0, reinterpret_cast<LPARAM>(&cs))) {
		DestroyRecursive(hwnd);
		return nullptr;
	}
	if (SendMessageW(hwnd, WM_CREATE, 0, reinterpret_cast<LPARAM>(&cs)) == -1) {
		DestroyRecursive(hwnd);
		return nullptr;
	}
	if (!GetWindowObject(hwnd)) return nullptr;
	SendSizeMove(hwnd, w);
	if (w->parent && !(dwExStyle & WS_EX_NOPARENTNOTIFY)) {
		SendMessageW(w->parent, WM_PARENTNOTIFY, MAKEWPARAM(WM_CREATE, w->id), reinterpret_cast<LPARAM>(hwnd));
	}
	if (dwStyle & WS_VISIBLE) ShowWindow(hwnd, w->parent ? SW_SHOWNA : SW_SHOW);
	return hwnd;
}

WINBOOL WINAPI DestroyWindow(HWND hWnd)
{
	if (!GetWindowObject(hWnd)) { SetLastError(ERROR_INVALID_WINDOW_HANDLE); return FALSE; }
	ShowWindow(hWnd, SW_HIDE);
	DestroyRecursive(hWnd);
	return TRUE;
}

WINBOOL WINAPI IsWindow(HWND hWnd) { return GetWindowObject(hWnd) != nullptr; }

/* ---- 木構造 ---- */
HWND WINAPI GetParent(HWND hWnd)
{
	auto w = GetWindowObject(hWnd);
	if (!w) return nullptr;
	return w->parent ? w->parent : w->owner;
}

HWND WINAPI GetAncestor(HWND hwnd, UINT gaFlags)
{
	auto w = GetWindowObject(hwnd);
	if (!w) return nullptr;
	if (gaFlags == GA_PARENT) return w->parent;
	auto top = TopLevelOf(w);
	if (gaFlags == GA_ROOTOWNER) {
		while (top->owner) {
			auto o = GetWindowObject(top->owner);
			if (!o) break;
			top = TopLevelOf(o);
		}
	}
	return ToHwnd(top);
}

HWND WINAPI SetParent(HWND hWndChild, HWND hWndNewParent)
{
	auto w = GetWindowObject(hWndChild);
	if (!w) return nullptr;
	HWND old;
	{
		std::lock_guard lock(UserMutex());
		old = w->parent;
		if (old) {
			auto p = reinterpret_cast<Window*>(old);
			p->children.erase(std::remove(p->children.begin(), p->children.end(), hWndChild), p->children.end());
		}
		w->parent = hWndNewParent;
		if (hWndNewParent) {
			reinterpret_cast<Window*>(hWndNewParent)->children.insert(reinterpret_cast<Window*>(hWndNewParent)->children.begin(), hWndChild);
		}
	}
	backend::FrameChanged(hWndChild);
	return old;
}

WINBOOL WINAPI IsChild(HWND hWndParent, HWND hWnd)
{
	auto w = GetWindowObject(hWnd);
	while (w && w->parent) {
		if (w->parent == hWndParent) return TRUE;
		w = GetWindowObject(w->parent);
	}
	return FALSE;
}

HWND WINAPI GetDesktopWindow(void)
{
	static HWND desktop = [] {
		WNDCLASSEXW wc{sizeof(wc)};
		wc.lpfnWndProc = DefWindowProcW;
		wc.lpszClassName = L"winport_desktop";
		RegisterClassExW(&wc);
		const RECT r = ToRECT(backend::ScreenRect());
		return CreateWindowExW(0, L"winport_desktop", L"", WS_POPUP, r.left, r.top, r.right - r.left, r.bottom - r.top, nullptr, nullptr, nullptr, nullptr);
	}();
	return desktop;
}

HWND WINAPI GetWindow(HWND hWnd, UINT uCmd)
{
	std::lock_guard lock(UserMutex());
	auto w = GetWindowObject(hWnd);
	if (!w) return nullptr;
	const std::vector<HWND>* siblings = nullptr;
	std::vector<HWND> tops;
	if (w->parent) siblings = &reinterpret_cast<Window*>(w->parent)->children;
	else {
		for (auto h : Windows()) if (!reinterpret_cast<Window*>(h)->parent) tops.push_back(h);
		siblings = &tops;
	}
	auto it = std::find(siblings->begin(), siblings->end(), hWnd);
	switch (uCmd) {
	case GW_CHILD: return w->children.empty() ? nullptr : w->children.front();
	case GW_OWNER: return w->owner;
	case GW_HWNDFIRST: return siblings->empty() ? nullptr : siblings->front();
	case GW_HWNDLAST: return siblings->empty() ? nullptr : siblings->back();
	case GW_HWNDNEXT: return (it != siblings->end() && it + 1 != siblings->end()) ? *(it + 1) : nullptr;
	case GW_HWNDPREV: return (it != siblings->end() && it != siblings->begin()) ? *(it - 1) : nullptr;
	case GW_ENABLEDPOPUP: return nullptr;
	default: return nullptr;
	}
}

WINBOOL WINAPI EnumWindows(WNDENUMPROC lpEnumFunc, LPARAM lParam)
{
	std::vector<HWND> tops;
	{
		std::lock_guard lock(UserMutex());
		for (auto h : Windows()) if (!reinterpret_cast<Window*>(h)->parent) tops.push_back(h);
	}
	for (auto h : tops) {
		if (IsWindow(h) && !lpEnumFunc(h, lParam)) return FALSE;
	}
	return TRUE;
}

WINBOOL WINAPI EnumChildWindows(HWND hWndParent, WNDENUMPROC lpEnumFunc, LPARAM lParam)
{
	std::vector<HWND> all;
	std::function<void(HWND)> collect = [&](HWND h) {
		auto w = GetWindowObject(h);
		if (!w) return;
		for (auto c : w->children) { all.push_back(c); collect(c); }
	};
	{
		std::lock_guard lock(UserMutex());
		collect(hWndParent);
	}
	for (auto h : all) {
		if (IsWindow(h) && !lpEnumFunc(h, lParam)) return FALSE;
	}
	return TRUE;
}

WINBOOL WINAPI EnumThreadWindows(DWORD dwThreadId, WNDENUMPROC lpfn, LPARAM lParam)
{
	std::vector<HWND> list;
	{
		std::lock_guard lock(UserMutex());
		for (auto h : Windows()) {
			auto w = reinterpret_cast<Window*>(h);
			if (!w->parent && w->threadId == dwThreadId) list.push_back(h);
		}
	}
	for (auto h : list) if (IsWindow(h) && !lpfn(h, lParam)) return FALSE;
	return TRUE;
}

HWND WINAPI FindWindowExW(HWND hWndParent, HWND hWndChildAfter, LPCWSTR lpszClass, LPCWSTR lpszWindow)
{
	std::lock_guard lock(UserMutex());
	std::vector<HWND> list;
	if (hWndParent && hWndParent != HWND_MESSAGE) {
		auto p = GetWindowObject(hWndParent);
		if (!p) return nullptr;
		list = p->children;
	} else {
		for (auto h : Windows()) if (!reinterpret_cast<Window*>(h)->parent) list.push_back(h);
	}
	bool started = hWndChildAfter == nullptr;
	const std::wstring key = lpszClass ? ClassKey(lpszClass) : L"";
	for (auto h : list) {
		if (!started) { started = h == hWndChildAfter; continue; }
		auto w = reinterpret_cast<Window*>(h);
		if (lpszClass && ClassKey(w->cls->name.c_str()) != key) continue;
		if (lpszWindow && w->text != lpszWindow) continue;
		return h;
	}
	return nullptr;
}

HWND WINAPI FindWindowW(LPCWSTR lpClassName, LPCWSTR lpWindowName)
{
	return FindWindowExW(nullptr, nullptr, lpClassName, lpWindowName);
}

DWORD WINAPI GetWindowThreadProcessId(HWND hWnd, LPDWORD lpdwProcessId)
{
	auto w = GetWindowObject(hWnd);
	if (!w) return 0;
	if (lpdwProcessId) *lpdwProcessId = w->processId;
	return w->threadId;
}

/* ---- 属性 ---- */
LONG_PTR WINAPI GetWindowLongPtrW(HWND hWnd, int nIndex)
{
	std::lock_guard lock(UserMutex());
	auto w = GetWindowObject(hWnd);
	if (!w) { SetLastError(ERROR_INVALID_WINDOW_HANDLE); return 0; }
	switch (nIndex) {
	case GWLP_WNDPROC: return reinterpret_cast<LONG_PTR>(w->proc);
	case GWLP_HINSTANCE: return reinterpret_cast<LONG_PTR>(w->instance);
	case GWLP_HWNDPARENT: return reinterpret_cast<LONG_PTR>(w->parent ? w->parent : w->owner);
	case GWLP_ID: return static_cast<LONG_PTR>(w->id);
	case GWL_STYLE: return static_cast<LONG>(w->style);
	case GWL_EXSTYLE: return static_cast<LONG>(w->exStyle);
	case GWLP_USERDATA: return w->userData;
	default:
		if (auto slot = ExtraSlot(w, nIndex)) return *slot;
		SetLastError(ERROR_INVALID_INDEX);
		return 0;
	}
}

LONG_PTR WINAPI SetWindowLongPtrW(HWND hWnd, int nIndex, LONG_PTR dwNewLong)
{
	LONG_PTR old = 0;
	bool styleChanged = false;
	{
		std::lock_guard lock(UserMutex());
		auto w = GetWindowObject(hWnd);
		if (!w) { SetLastError(ERROR_INVALID_WINDOW_HANDLE); return 0; }
		switch (nIndex) {
		case GWLP_WNDPROC: old = reinterpret_cast<LONG_PTR>(w->proc); w->proc = reinterpret_cast<WNDPROC>(dwNewLong); break;
		case GWLP_HINSTANCE: old = reinterpret_cast<LONG_PTR>(w->instance); w->instance = reinterpret_cast<HINSTANCE>(dwNewLong); break;
		case GWLP_HWNDPARENT: old = reinterpret_cast<LONG_PTR>(w->owner); w->owner = reinterpret_cast<HWND>(dwNewLong); break;
		case GWLP_ID: old = static_cast<LONG_PTR>(w->id); w->id = static_cast<UINT_PTR>(dwNewLong); break;
		case GWL_STYLE:
			old = static_cast<LONG>(w->style);
			// WS_VISIBLE の変更は ShowWindow を通す
			w->style = (static_cast<DWORD>(dwNewLong) & ~WS_VISIBLE) | (w->style & WS_VISIBLE);
			styleChanged = true;
			break;
		case GWL_EXSTYLE: old = static_cast<LONG>(w->exStyle); w->exStyle = static_cast<DWORD>(dwNewLong); styleChanged = true; break;
		case GWLP_USERDATA: old = w->userData; w->userData = dwNewLong; break;
		default:
			if (auto slot = ExtraSlot(w, nIndex)) { old = *slot; *slot = dwNewLong; break; }
			SetLastError(ERROR_INVALID_INDEX);
			return 0;
		}
	}
	if (styleChanged) backend::FrameChanged(hWnd);
	return old;
}

LONG WINAPI GetWindowLongW(HWND hWnd, int nIndex) { return static_cast<LONG>(GetWindowLongPtrW(hWnd, nIndex)); }
LONG WINAPI SetWindowLongW(HWND hWnd, int nIndex, LONG dwNewLong) { return static_cast<LONG>(SetWindowLongPtrW(hWnd, nIndex, dwNewLong)); }

int WINAPI GetDlgCtrlID(HWND hWnd)
{
	auto w = GetWindowObject(hWnd);
	return w ? static_cast<int>(w->id) : 0;
}

HWND WINAPI GetDlgItem(HWND hDlg, int nIDDlgItem)
{
	std::lock_guard lock(UserMutex());
	auto w = GetWindowObject(hDlg);
	if (!w) return nullptr;
	for (auto c : w->children) {
		if (static_cast<int>(reinterpret_cast<Window*>(c)->id) == nIDDlgItem) return c;
	}
	return nullptr;
}

/* ---- テキスト ---- */
WINBOOL WINAPI SetWindowTextW(HWND hWnd, LPCWSTR lpString)
{
	return static_cast<WINBOOL>(SendMessageW(hWnd, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(lpString)));
}

int WINAPI GetWindowTextW(HWND hWnd, LPWSTR lpString, int nMaxCount)
{
	if (nMaxCount <= 0) return 0;
	lpString[0] = 0;
	return static_cast<int>(SendMessageW(hWnd, WM_GETTEXT, nMaxCount, reinterpret_cast<LPARAM>(lpString)));
}

int WINAPI GetWindowTextLengthW(HWND hWnd) { return static_cast<int>(SendMessageW(hWnd, WM_GETTEXTLENGTH, 0, 0)); }

/* ---- 表示状態 ---- */
WINBOOL WINAPI ShowWindow(HWND hWnd, int nCmdShow)
{
	auto w = GetWindowObject(hWnd);
	if (!w) return FALSE;
	const bool wasVisible = (w->style & WS_VISIBLE) != 0;
	const bool show = nCmdShow != SW_HIDE;
	if (wasVisible != show) {
		SendMessageW(hWnd, WM_SHOWWINDOW, show, 0);
		{
			std::lock_guard lock(UserMutex());
			if (show) w->style |= WS_VISIBLE;
			else w->style &= ~WS_VISIBLE;
			if (nCmdShow == SW_MAXIMIZE || nCmdShow == SW_SHOWMAXIMIZED) w->style |= WS_MAXIMIZE;
			if (nCmdShow == SW_RESTORE || nCmdShow == SW_SHOWNORMAL) w->style &= ~(WS_MAXIMIZE | WS_MINIMIZE);
		}
		backend::WindowVisibilityChanged(hWnd);
		if (show) AddUpdateRect(w, nullptr, true);
	}
	if (show && !w->parent && nCmdShow != SW_SHOWNA && nCmdShow != SW_SHOWNOACTIVATE && nCmdShow != SW_SHOWMINNOACTIVE) {
		SetActiveWindow(hWnd);
	}
	return wasVisible;
}

WINBOOL WINAPI ShowWindowAsync(HWND hWnd, int nCmdShow) { return ShowWindow(hWnd, nCmdShow); }

WINBOOL WINAPI IsWindowVisible(HWND hWnd)
{
	std::lock_guard lock(UserMutex());
	auto w = GetWindowObject(hWnd);
	return w && IsReallyVisible(w);
}

WINBOOL WINAPI IsIconic(HWND hWnd)
{
	auto w = GetWindowObject(hWnd);
	return w && (w->style & WS_MINIMIZE);
}

WINBOOL WINAPI IsZoomed(HWND hWnd)
{
	auto w = GetWindowObject(hWnd);
	return w && (w->style & WS_MAXIMIZE);
}

WINBOOL WINAPI OpenIcon(HWND hWnd) { return ShowWindow(hWnd, SW_RESTORE); }

WINBOOL WINAPI EnableWindow(HWND hWnd, WINBOOL bEnable)
{
	auto w = GetWindowObject(hWnd);
	if (!w) return FALSE;
	const bool was = w->enabled;
	if (was != (bEnable != FALSE)) {
		w->enabled = bEnable != FALSE;
		if (bEnable) w->style &= ~WS_DISABLED;
		else w->style |= WS_DISABLED;
		SendMessageW(hWnd, WM_ENABLE, bEnable, 0);
	}
	return !was;
}

WINBOOL WINAPI IsWindowEnabled(HWND hWnd)
{
	auto w = GetWindowObject(hWnd);
	return w && w->enabled;
}

/* ---- 位置と大きさ ---- */
WINBOOL WINAPI SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags)
{
	auto w = GetWindowObject(hWnd);
	if (!w) return FALSE;
	WINDOWPOS wp{hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags};
	{
		std::lock_guard lock(UserMutex());
		if (uFlags & SWP_NOMOVE) { wp.x = w->rect.left; wp.y = w->rect.top; }
		if (uFlags & SWP_NOSIZE) { wp.cx = w->rect.right - w->rect.left; wp.cy = w->rect.bottom - w->rect.top; }
	}
	SendMessageW(hWnd, WM_WINDOWPOSCHANGING, 0, reinterpret_cast<LPARAM>(&wp));
	bool changed;
	{
		std::lock_guard lock(UserMutex());
		const RECT nr{wp.x, wp.y, wp.x + std::max(wp.cx, 0), wp.y + std::max(wp.cy, 0)};
		changed = !EqualRect(&nr, &w->rect);
		w->rect = nr;
		if (!(uFlags & SWP_NOZORDER) && w->parent) {
			auto& sib = reinterpret_cast<Window*>(w->parent)->children;
			sib.erase(std::remove(sib.begin(), sib.end(), hWnd), sib.end());
			if (hWndInsertAfter == HWND_BOTTOM) sib.push_back(hWnd);
			else if (hWndInsertAfter == HWND_TOP || hWndInsertAfter == HWND_TOPMOST || hWndInsertAfter == HWND_NOTOPMOST) sib.insert(sib.begin(), hWnd);
			else {
				auto it = std::find(sib.begin(), sib.end(), hWndInsertAfter);
				sib.insert(it == sib.end() ? sib.end() : it + 1, hWnd);
			}
		}
	}
	if (std::getenv("WINPORT_TRACE")) {
		std::fprintf(stderr, "SetWindowPos %p %s (%d,%d %dx%d) flags=%x changed=%d\n", static_cast<void*>(hWnd),
		             WideToUtf8(w->cls->name).c_str(), wp.x, wp.y, wp.cx, wp.cy, uFlags, changed);
	}
	if (changed || (uFlags & SWP_FRAMECHANGED)) {
		backend::FrameChanged(hWnd);
		if (!(uFlags & SWP_NOREDRAW)) AddUpdateRect(w, nullptr, true);
	}
	SendMessageW(hWnd, WM_WINDOWPOSCHANGED, 0, reinterpret_cast<LPARAM>(&wp));
	if (uFlags & SWP_SHOWWINDOW) ShowWindow(hWnd, (uFlags & SWP_NOACTIVATE) ? SW_SHOWNA : SW_SHOW);
	if (uFlags & SWP_HIDEWINDOW) ShowWindow(hWnd, SW_HIDE);
	return TRUE;
}

WINBOOL WINAPI MoveWindow(HWND hWnd, int X, int Y, int nWidth, int nHeight, WINBOOL bRepaint)
{
	return SetWindowPos(hWnd, nullptr, X, Y, nWidth, nHeight, SWP_NOZORDER | SWP_NOACTIVATE | (bRepaint ? 0 : SWP_NOREDRAW));
}

WINBOOL WINAPI BringWindowToTop(HWND hWnd)
{
	SetWindowPos(hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	if (auto w = GetWindowObject(hWnd); w && !w->parent) backend::Activate(hWnd);
	return TRUE;
}

WINBOOL WINAPI GetWindowRect(HWND hWnd, LPRECT lpRect)
{
	std::lock_guard lock(UserMutex());
	auto w = GetWindowObject(hWnd);
	if (!w) return FALSE;
	const POINT p = WindowOriginOnScreen(w);
	*lpRect = {p.x, p.y, p.x + (w->rect.right - w->rect.left), p.y + (w->rect.bottom - w->rect.top)};
	return TRUE;
}

WINBOOL WINAPI GetClientRect(HWND hWnd, LPRECT lpRect)
{
	std::lock_guard lock(UserMutex());
	auto w = GetWindowObject(hWnd);
	if (!w) return FALSE;
	const SIZE s = ClientSize(w);
	*lpRect = {0, 0, s.cx, s.cy};
	return TRUE;
}

WINBOOL WINAPI ClientToScreen(HWND hWnd, LPPOINT lpPoint)
{
	std::lock_guard lock(UserMutex());
	auto w = GetWindowObject(hWnd);
	if (!w) return FALSE;
	const POINT o = ClientOriginOnScreen(w);
	lpPoint->x += o.x;
	lpPoint->y += o.y;
	return TRUE;
}

WINBOOL WINAPI ScreenToClient(HWND hWnd, LPPOINT lpPoint)
{
	std::lock_guard lock(UserMutex());
	auto w = GetWindowObject(hWnd);
	if (!w) return FALSE;
	const POINT o = ClientOriginOnScreen(w);
	lpPoint->x -= o.x;
	lpPoint->y -= o.y;
	return TRUE;
}

int WINAPI MapWindowPoints(HWND hWndFrom, HWND hWndTo, LPPOINT lpPoints, UINT cPoints)
{
	POINT from{0, 0}, to{0, 0};
	if (hWndFrom) ClientToScreen(hWndFrom, &from);
	if (hWndTo) ClientToScreen(hWndTo, &to);
	const LONG dx = from.x - to.x, dy = from.y - to.y;
	for (UINT i = 0; i < cPoints; ++i) {
		lpPoints[i].x += dx;
		lpPoints[i].y += dy;
	}
	return MAKELONG(static_cast<WORD>(dx), static_cast<WORD>(dy));
}

HWND WINAPI ChildWindowFromPointEx(HWND hwnd, POINT pt, UINT)
{
	std::lock_guard lock(UserMutex());
	auto w = GetWindowObject(hwnd);
	if (!w) return nullptr;
	for (auto c : w->children) {
		auto cw = reinterpret_cast<Window*>(c);
		if ((cw->style & WS_VISIBLE) && PtInRect(&cw->rect, pt)) return c;
	}
	return hwnd;
}

HWND WINAPI ChildWindowFromPoint(HWND hWndParent, POINT Point) { return ChildWindowFromPointEx(hWndParent, Point, 0); }

HWND WINAPI WindowFromPoint(POINT Point)
{
	std::lock_guard lock(UserMutex());
	// 最前面のトップレベルから、点を含む最も深い子を探す（Z 順はおおよそ）
	for (auto it = Windows().rbegin(); it != Windows().rend(); ++it) {
		auto w = reinterpret_cast<Window*>(*it);
		if (w->parent || !(w->style & WS_VISIBLE)) continue;
		RECT r;
		GetWindowRect(*it, &r);
		if (!PtInRect(&r, Point)) continue;
		HWND cur = *it;
		for (;;) {
			POINT local = Point;
			ScreenToClient(cur, &local);
			const HWND child = ChildWindowFromPoint(cur, local);
			if (!child || child == cur) return cur;
			cur = child;
		}
	}
	return nullptr;
}

WINBOOL WINAPI GetWindowPlacement(HWND hWnd, WINDOWPLACEMENT* lpwndpl)
{
	auto w = GetWindowObject(hWnd);
	if (!w) return FALSE;
	lpwndpl->flags = 0;
	lpwndpl->showCmd = IsZoomed(hWnd) ? SW_SHOWMAXIMIZED : IsIconic(hWnd) ? SW_SHOWMINIMIZED : SW_SHOWNORMAL;
	lpwndpl->ptMinPosition = {-1, -1};
	lpwndpl->ptMaxPosition = {-1, -1};
	GetWindowRect(hWnd, &lpwndpl->rcNormalPosition);
	return TRUE;
}

WINBOOL WINAPI SetWindowPlacement(HWND hWnd, const WINDOWPLACEMENT* lpwndpl)
{
	const RECT& r = lpwndpl->rcNormalPosition;
	SetWindowPos(hWnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
	if (IsWindowVisible(hWnd) || lpwndpl->showCmd != SW_HIDE) ShowWindow(hWnd, static_cast<int>(lpwndpl->showCmd));
	return TRUE;
}

/* ---- アクティブ・フォーカス・キャプチャ ---- */
HWND WINAPI GetActiveWindow(void) { return CurrentQueue().active; }
HWND WINAPI GetFocus(void) { return CurrentQueue().focus; }
HWND WINAPI GetCapture(void) { return CurrentQueue().capture; }

HWND WINAPI SetActiveWindow(HWND hWnd)
{
	auto& q = CurrentQueue();
	const HWND old = q.active;
	if (old == hWnd) return old;
	if (old && IsWindow(old)) SendMessageW(old, WM_ACTIVATE, MAKEWPARAM(WA_INACTIVE, 0), reinterpret_cast<LPARAM>(hWnd));
	q.active = hWnd;
	if (hWnd) {
		SendMessageW(hWnd, WM_ACTIVATE, MAKEWPARAM(WA_ACTIVE, 0), reinterpret_cast<LPARAM>(old));
		backend::Activate(hWnd);
	}
	return old;
}

WINBOOL WINAPI SetForegroundWindow(HWND hWnd)
{
	auto w = GetWindowObject(hWnd);
	if (!w) return FALSE;
	if (w->threadId == GetCurrentThreadId()) SetActiveWindow(ToHwnd(TopLevelOf(w)));
	else backend::Activate(ToHwnd(TopLevelOf(w)));
	return TRUE;
}

HWND WINAPI GetForegroundWindow(void) { return CurrentQueue().active; }
HWND WINAPI GetLastActivePopup(HWND hWnd) { return hWnd; }
WINBOOL WINAPI AllowSetForegroundWindow(DWORD) { return TRUE; }
WINBOOL WINAPI AttachThreadInput(DWORD, DWORD, WINBOOL) { return TRUE; }
WINBOOL WINAPI LockWindowUpdate(HWND) { return TRUE; }
WINBOOL WINAPI ShowOwnedPopups(HWND, WINBOOL) { return TRUE; }

HWND WINAPI SetFocus(HWND hWnd)
{
	auto& q = CurrentQueue();
	const HWND old = q.focus;
	if (old == hWnd) return old;
	if (hWnd && !IsWindow(hWnd)) return nullptr;
	if (old && IsWindow(old)) SendMessageW(old, WM_KILLFOCUS, reinterpret_cast<WPARAM>(hWnd), 0);
	q.focus = hWnd;
	if (hWnd) {
		if (auto w = GetWindowObject(hWnd)) {
			const HWND top = ToHwnd(TopLevelOf(w));
			if (q.active != top) SetActiveWindow(top);
		}
		SendMessageW(hWnd, WM_SETFOCUS, reinterpret_cast<WPARAM>(old), 0);
	}
	return old;
}

HWND WINAPI SetCapture(HWND hWnd)
{
	auto& q = CurrentQueue();
	const HWND old = q.capture;
	q.capture = hWnd;
	return old;
}

WINBOOL WINAPI ReleaseCapture(void)
{
	auto& q = CurrentQueue();
	const HWND old = q.capture;
	q.capture = nullptr;
	if (old && IsWindow(old)) SendMessageW(old, WM_CAPTURECHANGED, 0, 0);
	return TRUE;
}

/* ---- 再描画 ---- */
WINBOOL WINAPI InvalidateRect(HWND hWnd, const RECT* lpRect, WINBOOL bErase)
{
	auto w = GetWindowObject(hWnd);
	if (!w) {
		if (!hWnd) {
			// 全ウィンドウ
			std::vector<HWND> all;
			{
				std::lock_guard lock(UserMutex());
				all = Windows();
			}
			for (auto h : all) if (auto x = GetWindowObject(h)) AddUpdateRect(x, nullptr, bErase);
			return TRUE;
		}
		return FALSE;
	}
	AddUpdateRect(w, lpRect, bErase != FALSE);
	return TRUE;
}

WINBOOL WINAPI InvalidateRgn(HWND hWnd, HRGN, WINBOOL bErase) { return InvalidateRect(hWnd, nullptr, bErase); }

WINBOOL WINAPI ValidateRect(HWND hWnd, const RECT*)
{
	auto w = GetWindowObject(hWnd);
	if (!w) return FALSE;
	std::lock_guard lock(UserMutex());
	w->updatePending = false;
	w->eraseBackground = false;
	return TRUE;
}

WINBOOL WINAPI GetUpdateRect(HWND hWnd, LPRECT lpRect, WINBOOL)
{
	auto w = GetWindowObject(hWnd);
	if (!w) return FALSE;
	std::lock_guard lock(UserMutex());
	if (lpRect) *lpRect = w->updatePending ? w->updateRect : RECT{};
	return w->updatePending;
}

WINBOOL WINAPI UpdateWindow(HWND hWnd)
{
	auto w = GetWindowObject(hWnd);
	if (!w) return FALSE;
	bool pending;
	{
		std::lock_guard lock(UserMutex());
		pending = w->updatePending && IsReallyVisible(w);
	}
	if (pending) SendMessageW(hWnd, WM_PAINT, 0, 0);
	std::vector<HWND> kids;
	{
		std::lock_guard lock(UserMutex());
		kids = w->children;
	}
	for (auto k : kids) UpdateWindow(k);
	return TRUE;
}

WINBOOL WINAPI RedrawWindow(HWND hWnd, const RECT* lprcUpdate, HRGN, UINT flags)
{
	if (flags & (RDW_INVALIDATE | RDW_ERASE | RDW_FRAME)) InvalidateRect(hWnd, lprcUpdate, (flags & RDW_ERASE) != 0);
	if (flags & RDW_ALLCHILDREN) {
		if (auto w = GetWindowObject(hWnd)) {
			std::vector<HWND> kids;
			{
				std::lock_guard lock(UserMutex());
				kids = w->children;
			}
			for (auto k : kids) RedrawWindow(k, nullptr, nullptr, flags);
		}
	}
	if (flags & (RDW_UPDATENOW | RDW_ERASENOW)) UpdateWindow(hWnd);
	return TRUE;
}

/* ---- プロシージャ ---- */
LRESULT WINAPI CallWindowProcW(WNDPROC lpPrevWndFunc, HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	return lpPrevWndFunc ? lpPrevWndFunc(hWnd, Msg, wParam, lParam) : 0;
}

WINBOOL WINAPI SetWindowSubclass(HWND hWnd, SUBCLASSPROC pfnSubclass, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	std::lock_guard lock(UserMutex());
	auto w = GetWindowObject(hWnd);
	if (!w) return FALSE;
	for (auto& s : w->subclasses) {
		if (s.proc == pfnSubclass && s.id == uIdSubclass) { s.refData = dwRefData; return TRUE; }
	}
	w->subclasses.push_back({pfnSubclass, uIdSubclass, dwRefData});
	return TRUE;
}

WINBOOL WINAPI GetWindowSubclass(HWND hWnd, SUBCLASSPROC pfnSubclass, UINT_PTR uIdSubclass, DWORD_PTR* pdwRefData)
{
	std::lock_guard lock(UserMutex());
	auto w = GetWindowObject(hWnd);
	if (!w) return FALSE;
	for (auto& s : w->subclasses) {
		if (s.proc == pfnSubclass && s.id == uIdSubclass) { if (pdwRefData) *pdwRefData = s.refData; return TRUE; }
	}
	return FALSE;
}

WINBOOL WINAPI RemoveWindowSubclass(HWND hWnd, SUBCLASSPROC pfnSubclass, UINT_PTR uIdSubclass)
{
	std::lock_guard lock(UserMutex());
	auto w = GetWindowObject(hWnd);
	if (!w) return FALSE;
	auto& v = w->subclasses;
	const auto before = v.size();
	v.erase(std::remove_if(v.begin(), v.end(), [&](const Subclass& s) { return s.proc == pfnSubclass && s.id == uIdSubclass; }), v.end());
	return v.size() != before;
}

LRESULT WINAPI DefSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	auto w = GetWindowObject(hWnd);
	if (!w) return 0;
	const int saved = t_subclassDepth;
	const int next = saved - 1;
	LRESULT r;
	if (next >= 0 && next < static_cast<int>(w->subclasses.size())) {
		t_subclassDepth = next;
		const Subclass sc = w->subclasses[next];
		r = sc.proc(hWnd, uMsg, wParam, lParam, sc.id, sc.refData);
	} else {
		t_subclassDepth = -1;
		r = w->proc ? w->proc(hWnd, uMsg, wParam, lParam) : DefWindowProcW(hWnd, uMsg, wParam, lParam);
	}
	t_subclassDepth = saved;
	return r;
}

LRESULT WINAPI DefWindowProcW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	auto w = GetWindowObject(hWnd);
	if (!w) return 0;
	switch (Msg) {
	case WM_NCCREATE: {
		auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
		if (cs && cs->lpszName) w->text = cs->lpszName;
		return TRUE;
	}
	case WM_SETTEXT:
		{
			std::lock_guard lock(UserMutex());
			w->text = lParam ? reinterpret_cast<LPCWSTR>(lParam) : L"";
		}
		backend::TextChanged(hWnd);
		return TRUE;
	case WM_GETTEXT: {
		std::lock_guard lock(UserMutex());
		const int max = static_cast<int>(wParam);
		if (max <= 0) return 0;
		const int n = std::min<int>(static_cast<int>(w->text.size()), max - 1);
		std::wmemcpy(reinterpret_cast<LPWSTR>(lParam), w->text.c_str(), n);
		reinterpret_cast<LPWSTR>(lParam)[n] = 0;
		return n;
	}
	case WM_GETTEXTLENGTH: {
		std::lock_guard lock(UserMutex());
		return static_cast<LRESULT>(w->text.size());
	}
	case WM_CLOSE:
		DestroyWindow(hWnd);
		return 0;
	case WM_PAINT: {
		PAINTSTRUCT ps;
		BeginPaint(hWnd, &ps);
		EndPaint(hWnd, &ps);
		return 0;
	}
	case WM_ERASEBKGND: {
		HBRUSH br = w->cls->background;
		if (!br) return 0;
		if (reinterpret_cast<ULONG_PTR>(br) <= COLOR_MENUBAR + 1) br = GetSysColorBrush(static_cast<int>(reinterpret_cast<ULONG_PTR>(br)) - 1);
		RECT rc;
		GetClientRect(hWnd, &rc);
		FillRect(reinterpret_cast<HDC>(wParam), &rc, br);
		return 1;
	}
	case WM_SETCURSOR:
		if (LOWORD(lParam) == HTCLIENT && w->cls->cursor) {
			SetCursor(w->cls->cursor);
			return TRUE;
		}
		if (w->parent) return SendMessageW(w->parent, WM_SETCURSOR, wParam, lParam);
		return FALSE;
	case WM_MOUSEWHEEL:
	case WM_MOUSEHWHEEL:
		if (w->parent) return SendMessageW(w->parent, Msg, wParam, lParam);
		return 0;
	case WM_WINDOWPOSCHANGED: {
		auto wp = reinterpret_cast<const WINDOWPOS*>(lParam);
		if (!(wp->flags & SWP_NOSIZE) || (wp->flags & SWP_FRAMECHANGED)) {
			const SIZE s = ClientSize(w);
			SendMessageW(hWnd, WM_SIZE, IsZoomed(hWnd) ? SIZE_MAXIMIZED : SIZE_RESTORED, MAKELPARAM(s.cx, s.cy));
		}
		if (!(wp->flags & SWP_NOMOVE)) SendMessageW(hWnd, WM_MOVE, 0, MAKELPARAM(w->rect.left, w->rect.top));
		return 0;
	}
	case WM_ACTIVATE:
		if (LOWORD(wParam) != WA_INACTIVE && !IsIconic(hWnd)) SetFocus(hWnd);
		return 0;
	case WM_SYSCOMMAND:
		switch (wParam & 0xFFF0) {
		case SC_CLOSE: SendMessageW(hWnd, WM_CLOSE, 0, 0); return 0;
		case SC_MINIMIZE: ShowWindow(hWnd, SW_MINIMIZE); return 0;
		case SC_MAXIMIZE: ShowWindow(hWnd, SW_MAXIMIZE); return 0;
		case SC_RESTORE: ShowWindow(hWnd, SW_RESTORE); return 0;
		default: return 0;
		}
	case WM_SYSKEYDOWN:
		if (wParam == VK_F4 && (HIWORD(lParam) & KF_ALTDOWN)) SendMessageW(ToHwnd(TopLevelOf(w)), WM_SYSCOMMAND, SC_CLOSE, 0);
		return 0;
	case WM_NCHITTEST:
		return HTCLIENT;
	case WM_NCCALCSIZE:
	case WM_NCPAINT:
	case WM_NCACTIVATE:
		return TRUE;
	case WM_GETMINMAXINFO:
	case WM_QUERYENDSESSION:
		return Msg == WM_QUERYENDSESSION ? TRUE : 0;
	case WM_CTLCOLORMSGBOX: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: case WM_CTLCOLORBTN:
	case WM_CTLCOLORDLG: case WM_CTLCOLORSCROLLBAR: case WM_CTLCOLORSTATIC:
		return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
	case WM_NOTIFYFORMAT:
		return NFR_UNICODE;
	default:
		return 0;
	}
}

}  // extern "C"
