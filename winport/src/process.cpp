/*
	process.cpp — 仮想プロセス（1 スレッド = 1 sakura プロセス）とスレッド

	sakura は自分自身を -NOWIN 付きで起動してコントロールプロセスを作り、エディタごとに
	プロセスを分ける。winport では自分自身の起動を「新しいスレッドで wWinMain を走らせる」
	ことで実現する（user32_internal.h のスレッドモデル参照）。
	- GetCurrentProcessId は仮想プロセスの ID（その仮想プロセスの最初のスレッドの ID）
	- 仮想プロセスがすべて終了したらアプリを終了する
	外部コマンドの起動（タグジャンプ、外部コマンド実行）は TODO（posix_spawn）。

	SPDX-License-Identifier: Zlib
*/
#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cwchar>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "backend.h"
#include "winport_internal.h"

extern "C" int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int);

namespace winport {

namespace {

thread_local DWORD t_processId = 0;
thread_local std::wstring t_commandLine;
std::atomic<int> g_liveProcesses{0};

struct ThreadObject : KernelObject {
	std::mutex m;
	std::condition_variable cv;
	bool finished = false;
	DWORD exitCode = STILL_ACTIVE;
	DWORD threadId = 0;
	DWORD suspendCount = 0;	//!< CREATE_SUSPENDED。0 になるまで本体を始めない
	bool Wait(unsigned long timeoutMs) override
	{
		std::unique_lock lock(m);
		if (timeoutMs == INFINITE) { cv.wait(lock, [this] { return finished; }); return true; }
		return cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] { return finished; });
	}
	void Finish(DWORD code)
	{
		{
			std::lock_guard lock(m);
			finished = true;
			exitCode = code;
		}
		cv.notify_all();
	}
};

//! スレッドを起動する。obj は 2 参照（呼び出し元とスレッド）
//! 仮想プロセスごとの記憶域。上流の「プロセスに 1 つ」のもの（design_template.h）はここに置く
struct ProcessData {
	std::unordered_map<const void*, void*> slots;
	std::vector<std::pair<void*, void (*)(void*)>> owned;	// 作成順
	std::vector<std::pair<void*, void (*)(void*)>> containers;	// owned の破棄が全部終わってから破棄する
	std::vector<void*> storage;	// 実体の記憶域。全部のデストラクタが終わってから解放する
};

std::recursive_mutex& ProcessDataMutex()
{
	static auto* m = new std::recursive_mutex;
	return *m;
}

std::unordered_map<DWORD, ProcessData>& ProcessTable()
{
	static auto* table = new std::unordered_map<DWORD, ProcessData>;
	return *table;
}

//! 仮想プロセスの実体を破棄する。デストラクタが他の実体を参照するので、終わるまで表から消さない
void DestroyProcessData(DWORD key)
{
	using Entry = std::pair<void*, void (*)(void*)>;
	auto next = [key](std::vector<Entry> ProcessData::*list, Entry* entry) {
		std::lock_guard lock(ProcessDataMutex());
		auto it = ProcessTable().find(key);
		if (it == ProcessTable().end() || (it->second.*list).empty()) return false;
		*entry = (it->second.*list).back();
		(it->second.*list).pop_back();
		return true;
	};
	Entry entry;
	while (next(&ProcessData::owned, &entry)) entry.second(entry.first);
	// TInstanceHolder の一覧は、その要素のデストラクタが触るので最後
	while (next(&ProcessData::containers, &entry)) entry.second(entry.first);
	// 上流の関数内 static は、デストラクタの後もプロセスの終わりまで記憶域が残る。デストラクタが破棄済みの
	// シングルトンを触ることがある（~CEditDoc → CDiffManager）ので、記憶域は最後にまとめて解放する
	std::vector<void*> storage;
	{
		std::lock_guard lock(ProcessDataMutex());
		auto it = ProcessTable().find(key);
		if (it != ProcessTable().end()) storage.swap(it->second.storage);
		ProcessTable().erase(key);
	}
	for (void* p : storage) ::operator delete(p);
}

ThreadObject* Spawn(std::function<DWORD()> body, DWORD processId, const std::wstring& commandLine, bool newProcess,
                    bool suspended = false)
{
	auto obj = new ThreadObject;
	obj->refs = 2;
	obj->suspendCount = suspended ? 1 : 0;
	std::mutex started;
	std::condition_variable startedCv;
	bool ready = false;
	std::thread([=, &started, &startedCv, &ready] {
		const DWORD tid = GetCurrentThreadId();
		t_processId = newProcess ? tid : processId;
		t_commandLine = commandLine;
		obj->threadId = tid;
		{
			std::lock_guard lock(started);
			ready = true;
		}
		startedCv.notify_all();
		{
			std::unique_lock lock(obj->m);
			obj->cv.wait(lock, [obj] { return obj->suspendCount == 0; });
		}
		const DWORD code = body();
		if (newProcess) DestroyProcessData(tid);
		obj->Finish(code);
		if (--obj->refs == 0) delete obj;
		if (newProcess && --g_liveProcesses == 0) backend::Quit();
	}).detach();
	std::unique_lock lock(started);
	startedCv.wait(lock, [&] { return ready; });
	return obj;
}

std::wstring ExecutablePath()
{
	wchar_t buf[MAX_PATH];
	const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
	return std::wstring(buf, n);
}

//! コマンドラインの先頭（実行ファイル名）を取り除く
std::wstring StripProgramName(const std::wstring& cmd)
{
	size_t i = 0;
	while (i < cmd.size() && cmd[i] == L' ') ++i;
	if (i < cmd.size() && cmd[i] == L'"') {
		const size_t end = cmd.find(L'"', i + 1);
		i = end == std::wstring::npos ? cmd.size() : end + 1;
	} else {
		while (i < cmd.size() && cmd[i] != L' ') ++i;
	}
	while (i < cmd.size() && cmd[i] == L' ') ++i;
	return cmd.substr(i);
}

bool IsSelf(LPCWSTR app, const std::wstring& cmd)
{
	const std::wstring self = ExecutablePath();
	if (app && *app) return _wcsicmp(app, self.c_str()) == 0;
	return cmd.find(self) != std::wstring::npos;
}

}  // namespace

DWORD CurrentProcessId()
{
	if (!t_processId) t_processId = GetCurrentThreadId();  // メインスレッド等
	return t_processId;
}

//! 仮想プロセスを起動する（アプリ起動時と CreateProcessW から使う）
uint32_t CurrentProcessKey() { return CurrentProcessId(); }

void AdoptProcess(uint32_t key) { t_processId = key; }

void* ProcessSlot(const void* tag)
{
	std::lock_guard lock(ProcessDataMutex());
	auto& slots = ProcessTable()[CurrentProcessId()].slots;
	const auto it = slots.find(tag);
	return it == slots.end() ? nullptr : it->second;
}

void SetProcessSlot(const void* tag, void* value)
{
	std::lock_guard lock(ProcessDataMutex());
	ProcessTable()[CurrentProcessId()].slots[tag] = value;
}

void ReleaseProcess(uint32_t key) { DestroyProcessData(key); }

void* ProcessSingleton(const void* tag, void* (*create)(), void (*finalize)(void*), bool container)
{
	// create の中から別のシングルトンを作ることがあるので再帰可能なロックのまま呼ぶ
	std::lock_guard lock(ProcessDataMutex());
	const DWORD key = CurrentProcessId();
	{
		auto& slots = ProcessTable()[key].slots;
		const auto it = slots.find(tag);
		if (it != slots.end()) return it->second;
	}
	void* value = create();
	auto& data = ProcessTable()[key];	// create の間に表が再配置されることがある
	data.slots[tag] = value;
	(container ? data.containers : data.owned).emplace_back(value, finalize);
	data.storage.push_back(value);
	return value;
}

HANDLE StartVirtualProcess(const std::wstring& arguments, bool suspended = false)
{
	++g_liveProcesses;
	const std::wstring cmdLine = L"\"" + ExecutablePath() + L"\" " + arguments;
	return Spawn([arguments]() -> DWORD {
		std::wstring args = arguments;
		return static_cast<DWORD>(wWinMain(GetModuleHandleW(nullptr), nullptr, args.data(), SW_SHOWDEFAULT));
	}, 0, cmdLine, true, suspended);
}

namespace {

void AppendArgument(std::wstring& args, const std::wstring& a)
{
	if (!args.empty()) args += L' ';
	args += (a.find(L' ') != std::wstring::npos) ? L"\"" + a + L"\"" : a;
}

}  // namespace

void LaunchFirstProcess(int argc, char** argv, const std::vector<std::string>& documents)
{
	std::wstring args;
	for (int i = 1; i < argc; ++i) {
		// Finder から起動したときの -psn_ 引数は捨てる
		if (std::string(argv[i]).rfind("-psn_", 0) == 0) continue;
		AppendArgument(args, Utf8ToWide(argv[i]));
	}
	for (const std::string& path : documents) AppendArgument(args, Utf8ToWide(path));
	CloseHandle(StartVirtualProcess(args));
}

void OpenDocument(const std::string& path)
{
	std::wstring args;
	if (!path.empty()) AppendArgument(args, Utf8ToWide(path));
	CloseHandle(StartVirtualProcess(args));
}

}  // namespace winport

using namespace winport;

extern "C" {

LPWSTR WINAPI GetCommandLineW(void)
{
	return t_commandLine.empty() ? const_cast<LPWSTR>(L"sakura") : t_commandLine.data();
}

void WINAPI GetStartupInfoW(LPSTARTUPINFOW lpStartupInfo)
{
	*lpStartupInfo = {};
	lpStartupInfo->cb = sizeof(STARTUPINFOW);
	lpStartupInfo->wShowWindow = SW_SHOWDEFAULT;
}

WINBOOL WINAPI CreateProcessW(LPCWSTR lpApplicationName, LPWSTR lpCommandLine, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES,
                              WINBOOL, DWORD dwCreationFlags, LPVOID, LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION lpProcessInformation)
{
	const std::wstring cmd = lpCommandLine ? lpCommandLine : L"";
	if (!IsSelf(lpApplicationName, cmd)) {
		// TODO(winport): 外部コマンドは posix_spawn で起動する
		ReportUnimplemented("kernel32", "CreateProcessW(external)");
		SetLastError(ERROR_FILE_NOT_FOUND);
		return FALSE;
	}
	// CREATE_SUSPENDED: 呼び出し元が初期化完了イベントを作ってから ResumeThread で始める（v2.4.3 の OpenNewEditor）
	HANDLE h = StartVirtualProcess(StripProgramName(cmd), (dwCreationFlags & CREATE_SUSPENDED) != 0);
	auto obj = static_cast<ThreadObject*>(ObjectFromHandle(h));
	if (lpProcessInformation) {
		++obj->refs;  // hProcess と hThread の 2 つを返す
		lpProcessInformation->hProcess = h;
		lpProcessInformation->hThread = h;
		lpProcessInformation->dwProcessId = obj->threadId;
		lpProcessInformation->dwThreadId = obj->threadId;
	} else {
		CloseHandle(h);
	}
	return TRUE;
}

WINBOOL WINAPI GetExitCodeProcess(HANDLE hProcess, LPDWORD lpExitCode)
{
	auto obj = dynamic_cast<ThreadObject*>(ObjectFromHandle(hProcess));
	if (!obj) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
	std::lock_guard lock(obj->m);
	*lpExitCode = obj->exitCode;
	return TRUE;
}

DWORD WINAPI ResumeThread(HANDLE hThread)
{
	auto obj = dynamic_cast<ThreadObject*>(ObjectFromHandle(hThread));
	if (!obj) { SetLastError(ERROR_INVALID_HANDLE); return static_cast<DWORD>(-1); }
	DWORD previous;
	{
		std::lock_guard lock(obj->m);
		previous = obj->suspendCount;
		if (obj->suspendCount) --obj->suspendCount;
	}
	obj->cv.notify_all();
	return previous;
}

WINBOOL WINAPI GetExitCodeThread(HANDLE hThread, LPDWORD lpExitCode) { return GetExitCodeProcess(hThread, lpExitCode); }

WINBOOL WINAPI TerminateProcess(HANDLE, UINT)
{
	ReportUnimplemented("kernel32", "TerminateProcess");
	return FALSE;
}

DWORD WINAPI WaitForInputIdle(HANDLE, DWORD) { return 0; }

HANDLE WINAPI CreateThread(LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter,
                           DWORD, LPDWORD lpThreadId)
{
	auto obj = Spawn([=]() -> DWORD { return lpStartAddress(lpParameter); }, CurrentProcessId(), GetCommandLineW(), false);
	if (lpThreadId) *lpThreadId = obj->threadId;
	return obj;
}

void WINAPI ExitThread(DWORD) { ReportUnimplemented("kernel32", "ExitThread"); }

}  // extern "C"

// CRT の _beginthreadex（crt.cpp の暫定実装を置き換える）
extern "C" uintptr_t winport_beginthreadex(void*, unsigned, unsigned (*start)(void*), void* arg, unsigned, unsigned* tid)
{
	auto obj = Spawn([=]() -> DWORD { return start(arg); }, CurrentProcessId(), GetCommandLineW(), false);
	if (tid) *tid = obj->threadId;
	return reinterpret_cast<uintptr_t>(static_cast<HANDLE>(obj));
}
