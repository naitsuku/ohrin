/*
	user32_message.cpp — メッセージキュー、Send/Post/Get/Peek/Dispatch、タイマー、キーボード状態

	- スレッドごとに ThreadQueue を持つ。別スレッドのウィンドウへの SendMessage は相手のキューに
	  「送信メッセージ」を積んで完了を待つ（待つ間も自分宛ての送信メッセージは処理する。Win32 と同じ）
	- WM_PAINT はキューが空のときに、無効領域を持つウィンドウについて生成する
	- WM_TIMER は期限が来たタイマーから生成する
	- メインスレッド（Cocoa）は PostToQueue / PostKeyMessage で入力を積むだけで、待たない

	SPDX-License-Identifier: Zlib
*/
#include "user32_internal.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cwchar>

#include "backend.h"
#include "winport_internal.h"

namespace winport {

LRESULT CallWindowProcChain(Window* w, HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
bool IsInternalMessage(UINT msg);          // bridge.cpp
void HandleInternalMessage(const MSG& m);  // bridge.cpp

namespace {

std::mutex& QueuesMutex() { static auto* m = new std::mutex; return *m; }
std::map<DWORD, std::unique_ptr<ThreadQueue>>& Queues() { static auto* m = new std::map<DWORD, std::unique_ptr<ThreadQueue>>; return *m; }

std::atomic<DWORD> g_messageTime{1};
std::atomic<int> g_keyState[256];

unsigned long long NowMs() { return GetTickCount64(); }

bool MatchFilter(const MSG& m, HWND hwnd, UINT minMsg, UINT maxMsg)
{
	if (hwnd && m.hwnd != hwnd && !IsChild(hwnd, m.hwnd)) return false;
	if (minMsg == 0 && maxMsg == 0) return true;
	return minMsg <= m.message && m.message <= maxMsg;
}

//! 自分宛ての送信メッセージを 1 つ処理する（q.mutex は持たずに呼ぶ）
bool ProcessOneSent(ThreadQueue& q)
{
	SentMessage* sm = nullptr;
	{
		std::lock_guard lock(q.mutex);
		if (q.sent.empty()) return false;
		sm = q.sent.front();
		q.sent.pop_front();
	}
	LRESULT r = 0;
	if (auto w = GetWindowObject(sm->hwnd)) r = CallWindowProcChain(w, sm->hwnd, sm->msg, sm->wParam, sm->lParam);
	{
		// 送信元は done を短い間隔で確認している（SendInternal）
		std::lock_guard lock(q.mutex);
		sm->result = r;
		sm->done = true;
	}
	return true;
}

//! 次の WM_PAINT 対象（無効領域を持つ自スレッドの可視ウィンドウ）
HWND FindPaintWindow(DWORD tid, HWND filter)
{
	std::lock_guard lock(UserMutex());
	for (auto h : AllWindows()) {
		auto w = reinterpret_cast<Window*>(h);
		if (w->threadId != tid || !w->updatePending) continue;
		if (!IsReallyVisible(w)) continue;
		if (filter && h != filter && !IsChild(filter, h)) continue;
		return h;
	}
	return nullptr;
}

//! 期限切れタイマーを MSG にする
bool TakeTimer(ThreadQueue& q, HWND filter, MSG* out)
{
	const auto now = NowMs();
	for (auto& t : q.timers) {
		if (t.due > now) continue;
		if (filter && t.hwnd != filter) continue;
		t.due = now + std::max<UINT>(t.elapse, 10);
		*out = MSG{t.hwnd, WM_TIMER, t.id, reinterpret_cast<LPARAM>(t.proc), static_cast<DWORD>(now), {}};
		return true;
	}
	return false;
}

unsigned long long NextTimerDue(ThreadQueue& q)
{
	unsigned long long due = ~0ULL;
	for (auto& t : q.timers) due = std::min(due, t.due);
	return due;
}

enum class Fetch { got, none, quit };

//! キューから 1 つ取り出す。remove=false なら覗くだけ
Fetch FetchMessage(ThreadQueue& q, MSG* msg, HWND hwnd, UINT minMsg, UINT maxMsg, bool remove)
{
	while (ProcessOneSent(q)) {}
	// 表示系からの内部メッセージ（ウィンドウの移動・アクティブ化等）はここで処理して返さない
	for (;;) {
		MSG internal{};
		{
			std::lock_guard lock(q.mutex);
			auto it = std::find_if(q.posted.begin(), q.posted.end(), [](const MSG& m) { return IsInternalMessage(m.message); });
			if (it == q.posted.end()) break;
			internal = *it;
			q.posted.erase(it);
		}
		HandleInternalMessage(internal);
	}
	{
		std::lock_guard lock(q.mutex);
		for (auto it = q.posted.begin(); it != q.posted.end(); ++it) {
			if (!MatchFilter(*it, hwnd, minMsg, maxMsg)) continue;
			*msg = *it;
			if (remove) q.posted.erase(it);
			return Fetch::got;
		}
		if (q.quit && (minMsg == 0 && maxMsg == 0 || (minMsg <= WM_QUIT && WM_QUIT <= maxMsg))) {
			*msg = MSG{nullptr, WM_QUIT, static_cast<WPARAM>(q.quitCode), 0, g_messageTime.load(), {}};
			if (remove) q.quit = false;
			return Fetch::quit;
		}
		if ((minMsg == 0 && maxMsg == 0) || (minMsg <= WM_TIMER && WM_TIMER <= maxMsg)) {
			if (TakeTimer(q, hwnd, msg)) return Fetch::got;
		}
	}
	if ((minMsg == 0 && maxMsg == 0) || (minMsg <= WM_PAINT && WM_PAINT <= maxMsg)) {
		if (const HWND ph = FindPaintWindow(q.threadId, hwnd)) {
			*msg = MSG{ph, WM_PAINT, 0, 0, g_messageTime.load(), {}};
			return Fetch::got;
		}
	}
	return Fetch::none;
}

void WaitForWork(ThreadQueue& q, unsigned long long maxWaitMs)
{
	std::unique_lock lock(q.mutex);
	if (!q.posted.empty() || !q.sent.empty() || q.quit) return;
	auto due = NextTimerDue(q);
	const auto now = NowMs();
	unsigned long long wait = maxWaitMs;
	if (due != ~0ULL) wait = std::min(wait, due > now ? due - now : 0);
	if (wait == 0) return;
	q.cv.wait_for(lock, std::chrono::milliseconds(std::min<unsigned long long>(wait, 24ULL * 3600 * 1000)));
}

LRESULT SendInternal(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, DWORD timeoutMs, bool* timedOut)
{
	auto w = GetWindowObject(hwnd);
	if (!w) return 0;
	const DWORD self = GetCurrentThreadId();
	if (w->threadId == self) return CallWindowProcChain(w, hwnd, msg, wParam, lParam);

	// 別スレッド: 相手のキューに積んで完了を待つ。待つ間も自分宛ての送信メッセージは処理する
	auto& target = QueueOf(w->threadId);
	auto& mine = QueueOf(self);
	SentMessage sm{hwnd, msg, wParam, lParam};
	{
		std::lock_guard lock(target.mutex);
		target.sent.push_back(&sm);
	}
	target.cv.notify_all();
	const auto start = NowMs();
	for (;;) {
		{
			std::lock_guard lock(target.mutex);
			if (sm.done) return sm.result;
		}
		ProcessOneSent(mine);
		if (timeoutMs != INFINITE && NowMs() - start >= timeoutMs) {
			std::lock_guard lock(target.mutex);
			if (sm.done) return sm.result;
			target.sent.erase(std::remove(target.sent.begin(), target.sent.end(), &sm), target.sent.end());
			if (timedOut) *timedOut = true;
			return 0;
		}
		if (!GetWindowObject(hwnd)) {
			std::lock_guard lock(target.mutex);
			target.sent.erase(std::remove(target.sent.begin(), target.sent.end(), &sm), target.sent.end());
			return sm.done ? sm.result : 0;
		}
		std::unique_lock lock(mine.mutex);
		mine.cv.wait_for(lock, std::chrono::milliseconds(2));
	}
}

}  // namespace

ThreadQueue& QueueOf(DWORD threadId)
{
	std::lock_guard lock(QueuesMutex());
	auto& p = Queues()[threadId];
	if (!p) {
		p = std::make_unique<ThreadQueue>();
		p->threadId = threadId;
	}
	return *p;
}

ThreadQueue& CurrentQueue() { return QueueOf(GetCurrentThreadId()); }

void WakeQueue(DWORD threadId) { QueueOf(threadId).cv.notify_all(); }

void PostToQueue(DWORD threadId, const MSG& msg)
{
	auto& q = QueueOf(threadId);
	{
		std::lock_guard lock(q.mutex);
		q.posted.push_back(msg);
	}
	q.cv.notify_all();
}

void PostKeyMessage(HWND hwnd, UINT msg, WPARAM vk, LPARAM lParam, const std::wstring& chars)
{
	auto w = GetWindowObject(hwnd);
	if (!w) return;
	const DWORD time = g_messageTime.fetch_add(1) + 1;
	auto& q = QueueOf(w->threadId);
	{
		std::lock_guard lock(q.mutex);
		if (!chars.empty()) q.keyChars[time] = chars;
		MSG m{hwnd, msg, vk, lParam, time, {}};
		q.posted.push_back(m);
	}
	q.cv.notify_all();
}

void SetKeyDown(int vk, bool down)
{
	if (vk < 0 || vk >= 256) return;
	int v = g_keyState[vk].load();
	if (down && !(v & 0x8000)) v ^= 1;  // トグル（CapsLock 等）
	g_keyState[vk] = down ? (v | 0x8000) : (v & 1);
}

bool IsKeyDown(int vk) { return vk >= 0 && vk < 256 && (g_keyState[vk].load() & 0x8000); }

}  // namespace winport

using namespace winport;

extern "C" {

LRESULT WINAPI SendMessageW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	if (hWnd == HWND_BROADCAST) {
		std::vector<HWND> tops;
		{
			std::lock_guard lock(UserMutex());
			for (auto h : AllWindows()) if (!reinterpret_cast<Window*>(h)->parent) tops.push_back(h);
		}
		for (auto h : tops) SendInternal(h, Msg, wParam, lParam, 1000, nullptr);
		return 0;
	}
	return SendInternal(hWnd, Msg, wParam, lParam, INFINITE, nullptr);
}

LRESULT WINAPI SendMessageTimeoutW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam, UINT, UINT uTimeout, PDWORD_PTR lpdwResult)
{
	bool timedOut = false;
	if (!GetWindowObject(hWnd)) { SetLastError(ERROR_INVALID_WINDOW_HANDLE); return 0; }
	const LRESULT r = SendInternal(hWnd, Msg, wParam, lParam, uTimeout ? uTimeout : INFINITE, &timedOut);
	if (lpdwResult) *lpdwResult = static_cast<DWORD_PTR>(r);
	if (timedOut) { SetLastError(ERROR_TIMEOUT); return 0; }
	return 1;
}

WINBOOL WINAPI SendNotifyMessageW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	auto w = GetWindowObject(hWnd);
	if (!w) return FALSE;
	if (w->threadId == GetCurrentThreadId()) SendMessageW(hWnd, Msg, wParam, lParam);
	else PostMessageW(hWnd, Msg, wParam, lParam);
	return TRUE;
}

WINBOOL WINAPI PostMessageW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	if (!hWnd) {
		PostToQueue(GetCurrentThreadId(), MSG{nullptr, Msg, wParam, lParam, g_messageTime.load(), {}});
		return TRUE;
	}
	if (hWnd == HWND_BROADCAST) {
		std::vector<HWND> tops;
		{
			std::lock_guard lock(UserMutex());
			for (auto h : AllWindows()) if (!reinterpret_cast<Window*>(h)->parent) tops.push_back(h);
		}
		for (auto h : tops) PostMessageW(h, Msg, wParam, lParam);
		return TRUE;
	}
	auto w = GetWindowObject(hWnd);
	if (!w) { SetLastError(ERROR_INVALID_WINDOW_HANDLE); return FALSE; }
	PostToQueue(w->threadId, MSG{hWnd, Msg, wParam, lParam, g_messageTime.load(), {}});
	return TRUE;
}

WINBOOL WINAPI PostMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) { return PostMessageW(hWnd, Msg, wParam, lParam); }

WINBOOL WINAPI PostThreadMessageW(DWORD idThread, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	PostToQueue(idThread, MSG{nullptr, Msg, wParam, lParam, g_messageTime.load(), {}});
	return TRUE;
}

void WINAPI PostQuitMessage(int nExitCode)
{
	auto& q = CurrentQueue();
	{
		std::lock_guard lock(q.mutex);
		q.quit = true;
		q.quitCode = nExitCode;
	}
	q.cv.notify_all();
}

WINBOOL WINAPI GetMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax)
{
	auto& q = CurrentQueue();
	for (;;) {
		switch (FetchMessage(q, lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, true)) {
		case Fetch::got: return TRUE;
		case Fetch::quit: return FALSE;
		case Fetch::none: WaitForWork(q, 24ULL * 3600 * 1000); break;
		}
	}
}

WINBOOL WINAPI PeekMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)
{
	auto& q = CurrentQueue();
	return FetchMessage(q, lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, (wRemoveMsg & PM_REMOVE) != 0) != Fetch::none;
}

WINBOOL WINAPI WaitMessage(void)
{
	auto& q = CurrentQueue();
	MSG m;
	if (FetchMessage(q, &m, nullptr, 0, 0, false) != Fetch::none) return TRUE;
	WaitForWork(q, 24ULL * 3600 * 1000);
	return TRUE;
}

DWORD WINAPI MsgWaitForMultipleObjects(DWORD nCount, const HANDLE* pHandles, WINBOOL fWaitAll, DWORD dwMilliseconds, DWORD)
{
	auto& q = CurrentQueue();
	const auto start = GetTickCount64();
	for (;;) {
		bool all = true;
		for (DWORD i = 0; i < nCount; ++i) {
			const bool signaled = WaitForSingleObject(pHandles[i], 0) == WAIT_OBJECT_0;
			if (signaled && !fWaitAll) return WAIT_OBJECT_0 + i;
			all = all && signaled;
		}
		if (fWaitAll && nCount && all) return WAIT_OBJECT_0;
		MSG m;
		if (FetchMessage(q, &m, nullptr, 0, 0, false) != Fetch::none) return WAIT_OBJECT_0 + nCount;
		if (dwMilliseconds != INFINITE && GetTickCount64() - start >= dwMilliseconds) return WAIT_TIMEOUT;
		WaitForWork(q, 10);
	}
}

LRESULT WINAPI DispatchMessageW(const MSG* lpMsg)
{
	if (lpMsg->message == WM_TIMER && lpMsg->lParam) {
		reinterpret_cast<TIMERPROC>(lpMsg->lParam)(lpMsg->hwnd, WM_TIMER, lpMsg->wParam, GetTickCount());
		return 0;
	}
	if (!lpMsg->hwnd) return 0;
	auto w = GetWindowObject(lpMsg->hwnd);
	if (!w) return 0;
	const LRESULT r = CallWindowProcChain(w, lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam);
	if (lpMsg->message == WM_PAINT) {
		// WM_PAINT を処理しても BeginPaint しなかった場合に無限に再生成しないよう検証済みにする
		if (auto x = GetWindowObject(lpMsg->hwnd)) {
			std::lock_guard lock(UserMutex());
			x->updatePending = false;
		}
	}
	return r;
}

WINBOOL WINAPI TranslateMessage(const MSG* lpMsg)
{
	if (lpMsg->message != WM_KEYDOWN && lpMsg->message != WM_SYSKEYDOWN) return FALSE;
	auto& q = CurrentQueue();
	std::wstring chars;
	{
		std::lock_guard lock(q.mutex);
		auto it = q.keyChars.find(lpMsg->time);
		if (it == q.keyChars.end()) return FALSE;
		chars = it->second;
		q.keyChars.erase(it);
	}
	const UINT charMsg = lpMsg->message == WM_SYSKEYDOWN ? WM_SYSCHAR : WM_CHAR;
	{
		std::lock_guard lock(q.mutex);
		// WM_KEYDOWN の直後に処理されるよう先頭に入れる（順序を保つため逆順に）
		for (auto it = chars.rbegin(); it != chars.rend(); ++it) {
			q.posted.push_front(MSG{lpMsg->hwnd, charMsg, static_cast<WPARAM>(*it), lpMsg->lParam, lpMsg->time, {}});
		}
	}
	return TRUE;
}

UINT WINAPI RegisterWindowMessageW(LPCWSTR lpString)
{
	static auto& m = *new std::mutex;
	static auto& table = *new std::map<std::wstring, UINT>;
	static UINT next = 0xC000;
	std::lock_guard lock(m);
	auto [it, inserted] = table.try_emplace(lpString ? lpString : L"", next);
	if (inserted) ++next;
	return it->second;
}

LONG WINAPI GetMessageTime(void) { return static_cast<LONG>(GetTickCount()); }

DWORD WINAPI GetMessagePos(void)
{
	POINT p;
	GetCursorPos(&p);
	return MAKELONG(static_cast<WORD>(p.x), static_cast<WORD>(p.y));
}

WINBOOL WINAPI InSendMessage(void) { return FALSE; }

/* ---- タイマー ---- */
UINT_PTR WINAPI SetTimer(HWND hWnd, UINT_PTR nIDEvent, UINT uElapse, TIMERPROC lpTimerFunc)
{
	DWORD tid = GetCurrentThreadId();
	if (hWnd) {
		auto w = GetWindowObject(hWnd);
		if (!w) return 0;
		tid = w->threadId;
	}
	auto& q = QueueOf(tid);
	static std::atomic<UINT_PTR> nextId{0x7000};
	if (!hWnd) nIDEvent = nextId++;
	{
		std::lock_guard lock(q.mutex);
		const auto due = GetTickCount64() + std::max<UINT>(uElapse, 10);
		for (auto& t : q.timers) {
			if (t.hwnd == hWnd && t.id == nIDEvent) {
				t.elapse = uElapse;
				t.proc = lpTimerFunc;
				t.due = due;
				q.cv.notify_all();
				return nIDEvent ? nIDEvent : 1;
			}
		}
		q.timers.push_back(Timer{hWnd, nIDEvent, uElapse, lpTimerFunc, due});
	}
	q.cv.notify_all();
	return nIDEvent ? nIDEvent : 1;
}

WINBOOL WINAPI KillTimer(HWND hWnd, UINT_PTR uIDEvent)
{
	DWORD tid = GetCurrentThreadId();
	if (hWnd) {
		auto w = GetWindowObject(hWnd);
		if (!w) return FALSE;
		tid = w->threadId;
	}
	auto& q = QueueOf(tid);
	std::lock_guard lock(q.mutex);
	const auto before = q.timers.size();
	q.timers.erase(std::remove_if(q.timers.begin(), q.timers.end(), [&](const Timer& t) { return t.hwnd == hWnd && t.id == uIDEvent; }), q.timers.end());
	return q.timers.size() != before;
}

/* ---- キーボード状態 ---- */
SHORT WINAPI GetKeyState(int nVirtKey)
{
	const int v = (nVirtKey >= 0 && nVirtKey < 256) ? g_keyState[nVirtKey].load() : 0;
	return static_cast<SHORT>((v & 0x8000 ? 0x8000 : 0) | (v & 1));
}

SHORT WINAPI GetAsyncKeyState(int vKey) { return static_cast<SHORT>(IsKeyDown(vKey) ? 0x8000 : 0); }

WINBOOL WINAPI GetKeyboardState(PBYTE lpKeyState)
{
	for (int i = 0; i < 256; ++i) {
		const int v = g_keyState[i].load();
		lpKeyState[i] = static_cast<BYTE>((v & 0x8000 ? 0x80 : 0) | (v & 1));
	}
	return TRUE;
}

HKL WINAPI GetKeyboardLayout(DWORD) { return reinterpret_cast<HKL>(static_cast<ULONG_PTR>(0x04110411)); }

}  // extern "C"
