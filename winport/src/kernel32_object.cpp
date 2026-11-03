/*
	kernel32_object.cpp — カーネルオブジェクト（HANDLE）と同期・メモリ管理

	HANDLE は KernelObject へのポインタ。CloseHandle で参照を 1 つ減らし、0 で破棄する。
	イベント・ミューテックス（プロセス内のみ。名前付きはプロセス内で共有）、
	WaitForSingleObject / Sleep、Global* / Local* / Heap* を提供する。

	SPDX-License-Identifier: Zlib
*/
#include <windows.h>

#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>

#include "winport_internal.h"

namespace winport {

bool KernelObject::Wait(unsigned long) { return true; }

KernelObject* ObjectFromHandle(void* h)
{
	const auto v = reinterpret_cast<intptr_t>(h);
	if (!h || h == INVALID_HANDLE_VALUE || (v > -16 && v < 0x10000)) return nullptr;  // 擬似ハンドル
	return static_cast<KernelObject*>(h);
}

void SetLastErrorFromErrno(int err)
{
	DWORD code;
	switch (err) {
	case 0: code = ERROR_SUCCESS; break;
	case ENOENT: code = ERROR_FILE_NOT_FOUND; break;
	case ENOTDIR: code = ERROR_PATH_NOT_FOUND; break;
	case EACCES: case EPERM: case EROFS: code = ERROR_ACCESS_DENIED; break;
	case EEXIST: code = ERROR_ALREADY_EXISTS; break;
	case ENOTEMPTY: code = ERROR_DIR_NOT_EMPTY; break;
	case ENOSPC: code = ERROR_DISK_FULL; break;
	case EBADF: code = ERROR_INVALID_HANDLE; break;
	case ENOMEM: code = ERROR_NOT_ENOUGH_MEMORY; break;
	case EINVAL: code = ERROR_INVALID_PARAMETER; break;
	case ENAMETOOLONG: code = ERROR_FILENAME_EXCED_RANGE; break;
	case EBUSY: code = ERROR_BUSY; break;
	case EISDIR: code = ERROR_ACCESS_DENIED; break;
	default: code = ERROR_GEN_FAILURE; break;
	}
	::SetLastError(code);
}

}  // namespace winport

namespace {

using winport::KernelObject;

// 上流のグローバル変数の初期化中にも呼ばれるため、関数内 static で初回使用時に構築する
std::mutex& WaitMutex() { static std::mutex m; return m; }
std::condition_variable& WaitCv() { static std::condition_variable cv; return cv; }

bool WaitUntil(unsigned long timeoutMs, const std::function<bool()>& ready, std::unique_lock<std::mutex>& lock)
{
	if (timeoutMs == INFINITE) {
		WaitCv().wait(lock, ready);
		return true;
	}
	return WaitCv().wait_for(lock, std::chrono::milliseconds(timeoutMs), ready);
}

struct EventObject : KernelObject {
	bool manualReset = false;
	bool signaled = false;
	bool Wait(unsigned long timeoutMs) override
	{
		std::unique_lock lock(WaitMutex());
		if (!WaitUntil(timeoutMs, [this] { return signaled; }, lock)) return false;
		if (!manualReset) signaled = false;
		return true;
	}
};

struct MutexObject : KernelObject {
	std::thread::id owner;
	int recursion = 0;
	std::wstring name;
	bool Wait(unsigned long timeoutMs) override
	{
		const auto self = std::this_thread::get_id();
		std::unique_lock lock(WaitMutex());
		if (!WaitUntil(timeoutMs, [&] { return recursion == 0 || owner == self; }, lock)) return false;
		owner = self;
		++recursion;
		return true;
	}
	~MutexObject() override;
};

// 名前付きオブジェクト（プロセス内）
std::mutex& NamedMutex() { static std::mutex m; return m; }
std::map<std::wstring, MutexObject*>& NamedMutexes() { static std::map<std::wstring, MutexObject*> m; return m; }

MutexObject::~MutexObject()
{
	if (!name.empty()) {
		std::lock_guard lock(NamedMutex());
		NamedMutexes().erase(name);
	}
}

// Global / Local メモリ: ヘッダ付きブロック。HGLOBAL はデータ先頭を指す（GMEM_FIXED と同じ扱い）
struct GlobalHeader {
	size_t size;
	UINT flags;
	int lockCount;
	size_t pad;
};

GlobalHeader* HeaderOf(void* p) { return static_cast<GlobalHeader*>(p) - 1; }

void* AllocBlock(UINT flags, SIZE_T bytes)
{
	auto h = static_cast<GlobalHeader*>(std::malloc(sizeof(GlobalHeader) + (bytes ? bytes : 1)));
	if (!h) return nullptr;
	h->size = bytes;
	h->flags = flags;
	h->lockCount = 0;
	if (flags & GMEM_ZEROINIT) std::memset(h + 1, 0, bytes);
	return h + 1;
}

}  // namespace

extern "C" {

/* ---- ハンドル ---- */
WINBOOL WINAPI CloseHandle(HANDLE hObject)
{
	auto obj = winport::ObjectFromHandle(hObject);
	if (!obj) {
		if (hObject == INVALID_HANDLE_VALUE || !hObject) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
		return TRUE;  // 擬似ハンドル
	}
	if (--obj->refs == 0) delete obj;
	return TRUE;
}

WINBOOL WINAPI DuplicateHandle(HANDLE, HANDLE hSourceHandle, HANDLE, LPHANDLE lpTargetHandle, DWORD, WINBOOL, DWORD dwOptions)
{
	auto obj = winport::ObjectFromHandle(hSourceHandle);
	if (obj) ++obj->refs;
	*lpTargetHandle = hSourceHandle;
	if (dwOptions & DUPLICATE_CLOSE_SOURCE) CloseHandle(hSourceHandle);
	return TRUE;
}

HANDLE WINAPI GetCurrentProcess(void) { return reinterpret_cast<HANDLE>(intptr_t{-1}); }
HANDLE WINAPI GetCurrentThread(void) { return reinterpret_cast<HANDLE>(intptr_t{-2}); }

/* ---- 待機 ---- */
DWORD WINAPI WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds)
{
	auto obj = winport::ObjectFromHandle(hHandle);
	if (!obj) { SetLastError(ERROR_INVALID_HANDLE); return WAIT_FAILED; }
	return obj->Wait(dwMilliseconds) ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
}

void WINAPI Sleep(DWORD dwMilliseconds)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(dwMilliseconds));
}

/* ---- イベント ---- */
HANDLE WINAPI CreateEventW(LPSECURITY_ATTRIBUTES, WINBOOL bManualReset, WINBOOL bInitialState, LPCWSTR lpName)
{
	if (lpName) winport::ReportUnimplemented("kernel32", "CreateEventW(named)");
	auto e = new EventObject;
	e->manualReset = bManualReset != FALSE;
	e->signaled = bInitialState != FALSE;
	return e;
}

WINBOOL WINAPI SetEvent(HANDLE hEvent)
{
	auto e = dynamic_cast<EventObject*>(winport::ObjectFromHandle(hEvent));
	if (!e) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
	{
		std::lock_guard lock(WaitMutex());
		e->signaled = true;
	}
	WaitCv().notify_all();
	return TRUE;
}

WINBOOL WINAPI ResetEvent(HANDLE hEvent)
{
	auto e = dynamic_cast<EventObject*>(winport::ObjectFromHandle(hEvent));
	if (!e) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
	std::lock_guard lock(WaitMutex());
	e->signaled = false;
	return TRUE;
}

/* ---- ミューテックス ---- */
HANDLE WINAPI CreateMutexW(LPSECURITY_ATTRIBUTES, WINBOOL bInitialOwner, LPCWSTR lpName)
{
	if (lpName && *lpName) {
		std::lock_guard lock(NamedMutex());
		if (auto it = NamedMutexes().find(lpName); it != NamedMutexes().end()) {
			++it->second->refs;
			SetLastError(ERROR_ALREADY_EXISTS);
			return it->second;
		}
	}
	auto m = new MutexObject;
	if (lpName && *lpName) {
		m->name = lpName;
		std::lock_guard lock(NamedMutex());
		NamedMutexes()[m->name] = m;
	}
	if (bInitialOwner) {
		m->owner = std::this_thread::get_id();
		m->recursion = 1;
	}
	SetLastError(ERROR_SUCCESS);
	return m;
}

HANDLE WINAPI OpenMutexW(DWORD, WINBOOL, LPCWSTR lpName)
{
	std::lock_guard lock(NamedMutex());
	if (auto it = NamedMutexes().find(lpName ? lpName : L""); it != NamedMutexes().end()) {
		++it->second->refs;
		return it->second;
	}
	SetLastError(ERROR_FILE_NOT_FOUND);
	return nullptr;
}

WINBOOL WINAPI ReleaseMutex(HANDLE hMutex)
{
	auto m = dynamic_cast<MutexObject*>(winport::ObjectFromHandle(hMutex));
	if (!m) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
	{
		std::lock_guard lock(WaitMutex());
		if (m->recursion == 0 || m->owner != std::this_thread::get_id()) {
			SetLastError(ERROR_NOT_OWNER);
			return FALSE;
		}
		if (--m->recursion == 0) m->owner = {};
	}
	WaitCv().notify_all();
	return TRUE;
}

/* ---- Global / Local ---- */
HGLOBAL WINAPI GlobalAlloc(UINT uFlags, SIZE_T dwBytes) { return AllocBlock(uFlags, dwBytes); }
LPVOID WINAPI GlobalLock(HGLOBAL hMem) { if (hMem) ++HeaderOf(hMem)->lockCount; return hMem; }
WINBOOL WINAPI GlobalUnlock(HGLOBAL hMem)
{
	if (!hMem) return FALSE;
	auto h = HeaderOf(hMem);
	if (h->lockCount > 0) --h->lockCount;
	if (h->lockCount == 0) SetLastError(NO_ERROR);
	return h->lockCount > 0;
}
SIZE_T WINAPI GlobalSize(HGLOBAL hMem) { return hMem ? HeaderOf(hMem)->size : 0; }
UINT WINAPI GlobalFlags(HGLOBAL hMem) { return hMem ? static_cast<UINT>(HeaderOf(hMem)->lockCount & GMEM_LOCKCOUNT) : GMEM_INVALID_HANDLE; }
HGLOBAL WINAPI GlobalFree(HGLOBAL hMem) { if (hMem) std::free(HeaderOf(hMem)); return nullptr; }
HGLOBAL WINAPI GlobalReAlloc(HGLOBAL hMem, SIZE_T dwBytes, UINT uFlags)
{
	if (!hMem) return GlobalAlloc(uFlags, dwBytes);
	auto h = static_cast<GlobalHeader*>(std::realloc(HeaderOf(hMem), sizeof(GlobalHeader) + (dwBytes ? dwBytes : 1)));
	if (!h) return nullptr;
	if ((uFlags & GMEM_ZEROINIT) && dwBytes > h->size) std::memset(reinterpret_cast<char*>(h + 1) + h->size, 0, dwBytes - h->size);
	h->size = dwBytes;
	return h + 1;
}

HLOCAL WINAPI LocalAlloc(UINT uFlags, SIZE_T uBytes) { return AllocBlock(uFlags, uBytes); }
HLOCAL WINAPI LocalFree(HLOCAL hMem) { return GlobalFree(hMem); }
LPVOID WINAPI LocalLock(HLOCAL hMem) { return GlobalLock(hMem); }
WINBOOL WINAPI LocalUnlock(HLOCAL hMem) { return GlobalUnlock(hMem); }
SIZE_T WINAPI LocalSize(HLOCAL hMem) { return GlobalSize(hMem); }

/* ---- Heap（プロセスヒープのみ） ---- */
HANDLE WINAPI GetProcessHeap(void) { return reinterpret_cast<HANDLE>(intptr_t{0x20}); }
LPVOID WINAPI HeapAlloc(HANDLE, DWORD dwFlags, SIZE_T dwBytes)
{
	return (dwFlags & HEAP_ZERO_MEMORY) ? std::calloc(1, dwBytes ? dwBytes : 1) : std::malloc(dwBytes ? dwBytes : 1);
}
LPVOID WINAPI HeapReAlloc(HANDLE, DWORD, LPVOID lpMem, SIZE_T dwBytes) { return std::realloc(lpMem, dwBytes); }
WINBOOL WINAPI HeapFree(HANDLE, DWORD, LPVOID lpMem) { std::free(lpMem); return TRUE; }

/* ---- 文字列 ---- */
int WINAPI lstrlenW(LPCWSTR s) { return s ? static_cast<int>(std::wcslen(s)) : 0; }
int WINAPI lstrlenA(LPCSTR s) { return s ? static_cast<int>(std::strlen(s)) : 0; }
LPWSTR WINAPI lstrcpyW(LPWSTR d, LPCWSTR s) { return std::wcscpy(d, s); }
int WINAPI lstrcmpW(LPCWSTR a, LPCWSTR b) { const int r = std::wcscmp(a, b); return r < 0 ? -1 : r > 0 ? 1 : 0; }
int WINAPI lstrcmpiW(LPCWSTR a, LPCWSTR b) { const int r = _wcsicmp(a, b); return r < 0 ? -1 : r > 0 ? 1 : 0; }

int WINAPI MulDiv(int nNumber, int nNumerator, int nDenominator)
{
	if (nDenominator == 0) return -1;
	long long v = static_cast<long long>(nNumber) * nNumerator;
	// Windows は四捨五入する
	const long long half = (nDenominator > 0 ? nDenominator : -nDenominator) / 2;
	v += ((v < 0) != (nDenominator < 0)) ? -half : half;
	return static_cast<int>(v / nDenominator);
}

/* ---- デバッグ ---- */
void WINAPI OutputDebugStringW(LPCWSTR lpOutputString)
{
	if (std::getenv("WINPORT_DEBUG_OUTPUT")) std::fprintf(stderr, "%s", winport::WideToUtf8(lpOutputString ? lpOutputString : L"").c_str());
}

void WINAPI DebugBreak(void) { __builtin_debugtrap(); }

DWORD WINAPI FormatMessageW(DWORD dwFlags, LPCVOID, DWORD dwMessageId, DWORD, LPWSTR lpBuffer, DWORD nSize, va_list*)
{
	// システムメッセージ表は持たないので、エラーコードを含む定型文を返す
	if (!(dwFlags & FORMAT_MESSAGE_FROM_SYSTEM)) {
		winport::ReportUnimplemented("kernel32", "FormatMessageW(non-system)");
		return 0;
	}
	wchar_t text[64];
	const int n = swprintf_s(text, 64, L"Error %lu.\r\n", static_cast<unsigned long>(dwMessageId));
	if (dwFlags & FORMAT_MESSAGE_ALLOCATE_BUFFER) {
		auto p = static_cast<LPWSTR>(LocalAlloc(LMEM_FIXED, (n + 1) * sizeof(WCHAR)));
		std::wmemcpy(p, text, n + 1);
		*reinterpret_cast<LPWSTR*>(lpBuffer) = p;
		return n;
	}
	if (static_cast<DWORD>(n) >= nSize) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return 0; }
	std::wmemcpy(lpBuffer, text, n + 1);
	return n;
}

}  // extern "C"
