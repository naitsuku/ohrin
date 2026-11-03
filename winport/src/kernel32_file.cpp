/*
	kernel32_file.cpp — ファイル・ディレクトリ・パス・時刻（kernel32）と shlwapi のパス関数

	- パスは '\\' も区切りとして受け付け、返すパスは '/' 区切り（ToNativePath 参照）
	- ファイルハンドルは FileObject（POSIX の fd を保持する KernelObject）
	- FILETIME は 1601-01-01 からの 100ns 単位（Windows と同じ）

	SPDX-License-Identifier: Zlib
*/
#include <windows.h>
#include <shlwapi.h>

#include <dirent.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <strings.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "winport_internal.h"

using winport::ToNativePath;
using winport::Utf8ToWide;

namespace {

constexpr long long kEpochDiff = 116444736000000000LL;  // 1601→1970 の 100ns 数

FILETIME ToFileTime(const struct timespec& ts)
{
	const long long v = static_cast<long long>(ts.tv_sec) * 10000000LL + ts.tv_nsec / 100 + kEpochDiff;
	return FILETIME{static_cast<DWORD>(v & 0xFFFFFFFF), static_cast<DWORD>(v >> 32)};
}

long long FileTimeValue(const FILETIME& ft) { return (static_cast<long long>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime; }

struct timespec ToTimespec(const FILETIME& ft)
{
	const long long v = FileTimeValue(ft) - kEpochDiff;
	struct timespec ts {};
	ts.tv_sec = static_cast<time_t>(v / 10000000LL);
	ts.tv_nsec = static_cast<long>((v % 10000000LL) * 100);
	return ts;
}

DWORD AttributesOf(const struct stat& st, const std::string& path)
{
	DWORD a = 0;
	if (S_ISDIR(st.st_mode)) a |= FILE_ATTRIBUTE_DIRECTORY;
	if (!(st.st_mode & S_IWUSR)) a |= FILE_ATTRIBUTE_READONLY;
	const auto slash = path.find_last_of('/');
	const auto name = slash == std::string::npos ? path : path.substr(slash + 1);
	if (!name.empty() && name[0] == '.' && name != "." && name != "..") a |= FILE_ATTRIBUTE_HIDDEN;
	return a ? a : FILE_ATTRIBUTE_NORMAL;
}

void FillFindData(WIN32_FIND_DATAW* fd, const std::string& dir, const std::string& name)
{
	*fd = {};
	const std::string full = dir + "/" + name;
	struct stat st {};
	if (stat(full.c_str(), &st) == 0) {
		fd->dwFileAttributes = AttributesOf(st, full);
		fd->ftCreationTime = ToFileTime(st.st_birthtimespec);
		fd->ftLastAccessTime = ToFileTime(st.st_atimespec);
		fd->ftLastWriteTime = ToFileTime(st.st_mtimespec);
		fd->nFileSizeHigh = static_cast<DWORD>(static_cast<unsigned long long>(st.st_size) >> 32);
		fd->nFileSizeLow = static_cast<DWORD>(st.st_size & 0xFFFFFFFF);
	}
	const std::wstring w = Utf8ToWide(name);
	const size_t n = w.size() < MAX_PATH - 1 ? w.size() : MAX_PATH - 1;
	std::wmemcpy(fd->cFileName, w.data(), n);
	fd->cFileName[n] = 0;
}

struct FileObject : winport::KernelObject {
	int fd = -1;
	~FileObject() override { if (fd >= 0) ::close(fd); }
};

struct FindObject : winport::KernelObject {
	std::string dir;
	std::string pattern;
	DIR* d = nullptr;
	~FindObject() override { if (d) closedir(d); }
	bool Next(WIN32_FIND_DATAW* fd)
	{
		while (auto e = readdir(d)) {
			if (fnmatch(pattern.c_str(), e->d_name, FNM_CASEFOLD) == 0) {
				FillFindData(fd, dir, e->d_name);
				return true;
			}
		}
		return false;
	}
};

FileObject* FileFromHandle(HANDLE h) { return dynamic_cast<FileObject*>(winport::ObjectFromHandle(h)); }

}  // namespace

namespace winport {
//! ファイルハンドルの fd（ファイルでなければ -1）。CreateFileMappingW が使う
int FdFromHandle(HANDLE h)
{
	auto f = FileFromHandle(h);
	return f ? f->fd : -1;
}
}  // namespace winport

namespace {

DWORD CopyPathOut(const std::wstring& s, LPWSTR buf, DWORD n)
{
	if (!buf || n <= s.size()) return static_cast<DWORD>(s.size() + 1);
	std::wmemcpy(buf, s.c_str(), s.size() + 1);
	return static_cast<DWORD>(s.size());
}

}  // namespace

extern "C" {

//! sakura.exe.ini（マルチユーザー設定）の読み取りにだけ使われる。sakura.ini 本体は CProfile が読む
UINT WINAPI GetPrivateProfileIntW(LPCWSTR lpAppName, LPCWSTR lpKeyName, INT nDefault, LPCWSTR lpFileName)
{
	FILE* f = std::fopen(winport::WideToUtf8(lpFileName).c_str(), "r");
	if (!f) return nDefault;
	const std::string section = "[" + winport::WideToUtf8(lpAppName) + "]", key = winport::WideToUtf8(lpKeyName) + "=";
	char line[1024];
	bool inSection = false;
	UINT result = nDefault;
	while (std::fgets(line, sizeof(line), f)) {
		std::string l(line);
		while (!l.empty() && std::isspace(static_cast<unsigned char>(l.back()))) l.pop_back();
		if (!l.empty() && l[0] == '[') inSection = strcasecmp(l.c_str(), section.c_str()) == 0;
		else if (inSection && strncasecmp(l.c_str(), key.c_str(), key.size()) == 0) { result = static_cast<UINT>(std::atoi(l.c_str() + key.size())); break; }
	}
	std::fclose(f);
	return result;
}

/* ---- ファイルハンドル ---- */
HANDLE WINAPI CreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD, LPSECURITY_ATTRIBUTES,
                          DWORD dwCreationDisposition, DWORD, HANDLE)
{
	const std::string path = ToNativePath(lpFileName);
	int flags = 0;
	const bool r = (dwDesiredAccess & (GENERIC_READ | FILE_READ_DATA | GENERIC_ALL)) != 0;
	const bool w = (dwDesiredAccess & (GENERIC_WRITE | FILE_WRITE_DATA | FILE_APPEND_DATA | GENERIC_ALL)) != 0;
	flags |= (r && w) ? O_RDWR : w ? O_WRONLY : O_RDONLY;
	switch (dwCreationDisposition) {
	case CREATE_NEW: flags |= O_CREAT | O_EXCL; break;
	case CREATE_ALWAYS: flags |= O_CREAT | O_TRUNC; break;
	case OPEN_ALWAYS: flags |= O_CREAT; break;
	case TRUNCATE_EXISTING: flags |= O_TRUNC; break;
	default: break;  // OPEN_EXISTING
	}
	const bool existed = ::access(path.c_str(), F_OK) == 0;
	struct stat st {};
	if (existed && stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
		SetLastError(ERROR_ACCESS_DENIED);
		return INVALID_HANDLE_VALUE;
	}
	const int fd = ::open(path.c_str(), flags | O_CLOEXEC, 0644);
	if (fd < 0) {
		winport::SetLastErrorFromErrno(errno);
		if (errno == EEXIST) SetLastError(ERROR_FILE_EXISTS);
		return INVALID_HANDLE_VALUE;
	}
	auto f = new FileObject;
	f->fd = fd;
	// CREATE_ALWAYS / OPEN_ALWAYS で既存ファイルを開いたときは ERROR_ALREADY_EXISTS を立てる
	SetLastError((existed && (dwCreationDisposition == CREATE_ALWAYS || dwCreationDisposition == OPEN_ALWAYS))
	             ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS);
	return f;
}

WINBOOL WINAPI ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED)
{
	auto f = FileFromHandle(hFile);
	if (!f) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
	const ssize_t n = ::read(f->fd, lpBuffer, nNumberOfBytesToRead);
	if (n < 0) { winport::SetLastErrorFromErrno(errno); return FALSE; }
	if (lpNumberOfBytesRead) *lpNumberOfBytesRead = static_cast<DWORD>(n);
	return TRUE;
}

WINBOOL WINAPI WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED)
{
	int fd;
	const auto v = reinterpret_cast<intptr_t>(hFile);
	if (v == 0x11) fd = STDOUT_FILENO;
	else if (v == 0x12) fd = STDERR_FILENO;
	else {
		auto f = FileFromHandle(hFile);
		if (!f) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
		fd = f->fd;
	}
	const ssize_t n = ::write(fd, lpBuffer, nNumberOfBytesToWrite);
	if (n < 0) { winport::SetLastErrorFromErrno(errno); return FALSE; }
	if (lpNumberOfBytesWritten) *lpNumberOfBytesWritten = static_cast<DWORD>(n);
	return TRUE;
}

WINBOOL WINAPI FlushFileBuffers(HANDLE hFile)
{
	auto f = FileFromHandle(hFile);
	return f && ::fsync(f->fd) == 0;
}

WINBOOL WINAPI GetFileSizeEx(HANDLE hFile, PLARGE_INTEGER lpFileSize)
{
	auto f = FileFromHandle(hFile);
	struct stat st {};
	if (!f || fstat(f->fd, &st) != 0) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
	lpFileSize->QuadPart = st.st_size;
	return TRUE;
}

DWORD WINAPI GetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh)
{
	LARGE_INTEGER li{};
	if (!GetFileSizeEx(hFile, &li)) return INVALID_FILE_SIZE;
	if (lpFileSizeHigh) *lpFileSizeHigh = static_cast<DWORD>(li.QuadPart >> 32);
	return static_cast<DWORD>(li.QuadPart & 0xFFFFFFFF);
}

WINBOOL WINAPI SetFilePointerEx(HANDLE hFile, LARGE_INTEGER liDistanceToMove, PLARGE_INTEGER lpNewFilePointer, DWORD dwMoveMethod)
{
	auto f = FileFromHandle(hFile);
	if (!f) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
	const int whence = dwMoveMethod == FILE_CURRENT ? SEEK_CUR : dwMoveMethod == FILE_END ? SEEK_END : SEEK_SET;
	const off_t r = ::lseek(f->fd, liDistanceToMove.QuadPart, whence);
	if (r < 0) { winport::SetLastErrorFromErrno(errno); return FALSE; }
	if (lpNewFilePointer) lpNewFilePointer->QuadPart = r;
	return TRUE;
}

DWORD WINAPI SetFilePointer(HANDLE hFile, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod)
{
	LARGE_INTEGER li{};
	li.QuadPart = lpDistanceToMoveHigh ? (static_cast<long long>(*lpDistanceToMoveHigh) << 32) | static_cast<DWORD>(lDistanceToMove)
	                                   : lDistanceToMove;
	LARGE_INTEGER out{};
	if (!SetFilePointerEx(hFile, li, &out, dwMoveMethod)) return INVALID_SET_FILE_POINTER;
	if (lpDistanceToMoveHigh) *lpDistanceToMoveHigh = static_cast<LONG>(out.QuadPart >> 32);
	return static_cast<DWORD>(out.QuadPart & 0xFFFFFFFF);
}

WINBOOL WINAPI SetEndOfFile(HANDLE hFile)
{
	auto f = FileFromHandle(hFile);
	if (!f) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
	const off_t pos = ::lseek(f->fd, 0, SEEK_CUR);
	return ::ftruncate(f->fd, pos) == 0;
}

WINBOOL WINAPI GetFileTime(HANDLE hFile, LPFILETIME lpCreationTime, LPFILETIME lpLastAccessTime, LPFILETIME lpLastWriteTime)
{
	auto f = FileFromHandle(hFile);
	struct stat st {};
	if (!f || fstat(f->fd, &st) != 0) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
	if (lpCreationTime) *lpCreationTime = ToFileTime(st.st_birthtimespec);
	if (lpLastAccessTime) *lpLastAccessTime = ToFileTime(st.st_atimespec);
	if (lpLastWriteTime) *lpLastWriteTime = ToFileTime(st.st_mtimespec);
	return TRUE;
}

WINBOOL WINAPI SetFileTime(HANDLE hFile, const FILETIME*, const FILETIME* lpLastAccessTime, const FILETIME* lpLastWriteTime)
{
	auto f = FileFromHandle(hFile);
	if (!f) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
	struct timespec ts[2] = {{0, UTIME_OMIT}, {0, UTIME_OMIT}};
	if (lpLastAccessTime) ts[0] = ToTimespec(*lpLastAccessTime);
	if (lpLastWriteTime) ts[1] = ToTimespec(*lpLastWriteTime);
	return ::futimens(f->fd, ts) == 0;
}

/* ---- ファイル属性・操作 ---- */
DWORD WINAPI GetFileAttributesW(LPCWSTR lpFileName)
{
	const std::string path = ToNativePath(lpFileName);
	struct stat st {};
	if (stat(path.c_str(), &st) != 0) {
		winport::SetLastErrorFromErrno(errno);
		return INVALID_FILE_ATTRIBUTES;
	}
	return AttributesOf(st, path);
}

WINBOOL WINAPI GetFileAttributesExW(LPCWSTR lpFileName, GET_FILEEX_INFO_LEVELS, LPVOID lpFileInformation)
{
	const std::string path = ToNativePath(lpFileName);
	struct stat st {};
	if (stat(path.c_str(), &st) != 0) { winport::SetLastErrorFromErrno(errno); return FALSE; }
	auto info = static_cast<WIN32_FILE_ATTRIBUTE_DATA*>(lpFileInformation);
	info->dwFileAttributes = AttributesOf(st, path);
	info->ftCreationTime = ToFileTime(st.st_birthtimespec);
	info->ftLastAccessTime = ToFileTime(st.st_atimespec);
	info->ftLastWriteTime = ToFileTime(st.st_mtimespec);
	info->nFileSizeHigh = static_cast<DWORD>(static_cast<unsigned long long>(st.st_size) >> 32);
	info->nFileSizeLow = static_cast<DWORD>(st.st_size & 0xFFFFFFFF);
	return TRUE;
}

WINBOOL WINAPI SetFileAttributesW(LPCWSTR lpFileName, DWORD dwFileAttributes)
{
	const std::string path = ToNativePath(lpFileName);
	struct stat st {};
	if (stat(path.c_str(), &st) != 0) { winport::SetLastErrorFromErrno(errno); return FALSE; }
	mode_t mode = st.st_mode & 07777;
	mode = (dwFileAttributes & FILE_ATTRIBUTE_READONLY) ? (mode & ~0222) : (mode | S_IWUSR);
	return ::chmod(path.c_str(), mode) == 0;
}

WINBOOL WINAPI DeleteFileW(LPCWSTR lpFileName)
{
	if (::unlink(ToNativePath(lpFileName).c_str()) == 0) return TRUE;
	winport::SetLastErrorFromErrno(errno);
	return FALSE;
}

WINBOOL WINAPI CreateDirectoryW(LPCWSTR lpPathName, LPSECURITY_ATTRIBUTES)
{
	if (::mkdir(ToNativePath(lpPathName).c_str(), 0755) == 0) return TRUE;
	winport::SetLastErrorFromErrno(errno);
	return FALSE;
}

WINBOOL WINAPI RemoveDirectoryW(LPCWSTR lpPathName)
{
	if (::rmdir(ToNativePath(lpPathName).c_str()) == 0) return TRUE;
	winport::SetLastErrorFromErrno(errno);
	return FALSE;
}

WINBOOL WINAPI MoveFileExW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, DWORD dwFlags)
{
	const std::string from = ToNativePath(lpExistingFileName);
	const std::string to = ToNativePath(lpNewFileName);
	if (!(dwFlags & MOVEFILE_REPLACE_EXISTING) && ::access(to.c_str(), F_OK) == 0) {
		SetLastError(ERROR_ALREADY_EXISTS);
		return FALSE;
	}
	if (::rename(from.c_str(), to.c_str()) == 0) return TRUE;
	winport::SetLastErrorFromErrno(errno);
	return FALSE;
}

WINBOOL WINAPI MoveFileW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName)
{
	return MoveFileExW(lpExistingFileName, lpNewFileName, 0);
}

WINBOOL WINAPI CopyFileW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, WINBOOL bFailIfExists)
{
	std::error_code ec;
	const auto opt = bFailIfExists ? std::filesystem::copy_options::none : std::filesystem::copy_options::overwrite_existing;
	if (std::filesystem::copy_file(ToNativePath(lpExistingFileName), ToNativePath(lpNewFileName), opt, ec)) return TRUE;
	winport::SetLastErrorFromErrno(ec.value());
	if (ec == std::errc::file_exists) SetLastError(ERROR_FILE_EXISTS);
	return FALSE;
}

WINBOOL WINAPI ReplaceFileW(LPCWSTR lpReplacedFileName, LPCWSTR lpReplacementFileName, LPCWSTR lpBackupFileName,
                            DWORD, LPVOID, LPVOID)
{
	const std::string replaced = ToNativePath(lpReplacedFileName);
	if (::access(replaced.c_str(), F_OK) != 0) { SetLastError(ERROR_FILE_NOT_FOUND); return FALSE; }
	if (lpBackupFileName && *lpBackupFileName) {
		std::error_code ec;
		std::filesystem::copy_file(replaced, ToNativePath(lpBackupFileName), std::filesystem::copy_options::overwrite_existing, ec);
	}
	if (::rename(ToNativePath(lpReplacementFileName).c_str(), replaced.c_str()) == 0) return TRUE;
	winport::SetLastErrorFromErrno(errno);
	return FALSE;
}

/* ---- 列挙 ---- */
HANDLE WINAPI FindFirstFileW(LPCWSTR lpFileName, LPWIN32_FIND_DATAW lpFindFileData)
{
	const std::string spec = ToNativePath(lpFileName);
	const auto slash = spec.find_last_of('/');
	auto find = new FindObject;
	find->dir = slash == std::string::npos ? "." : (slash == 0 ? "/" : spec.substr(0, slash));
	find->pattern = slash == std::string::npos ? spec : spec.substr(slash + 1);
	if (find->pattern == "*.*") find->pattern = "*";  // Windows では拡張子なしも一致する
	find->d = opendir(find->dir.c_str());
	if (!find->d || !find->Next(lpFindFileData)) {
		SetLastError(find->d ? ERROR_FILE_NOT_FOUND : ERROR_PATH_NOT_FOUND);
		delete find;
		return INVALID_HANDLE_VALUE;
	}
	return find;
}

WINBOOL WINAPI FindNextFileW(HANDLE hFindFile, LPWIN32_FIND_DATAW lpFindFileData)
{
	auto find = dynamic_cast<FindObject*>(winport::ObjectFromHandle(hFindFile));
	if (!find) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
	if (find->Next(lpFindFileData)) return TRUE;
	SetLastError(ERROR_NO_MORE_FILES);
	return FALSE;
}

WINBOOL WINAPI FindClose(HANDLE hFindFile) { return CloseHandle(hFindFile); }

/* ---- パス ---- */
DWORD WINAPI GetFullPathNameW(LPCWSTR lpFileName, DWORD nBufferLength, LPWSTR lpBuffer, LPWSTR* lpFilePart)
{
	std::error_code ec;
	const std::string native = ToNativePath(lpFileName);
	auto full = std::filesystem::absolute(native, ec).lexically_normal().string();
	if (full.size() > 1 && full.back() == '/' && !native.empty() && native.back() != '/') full.pop_back();
	const std::wstring w = Utf8ToWide(full);
	const DWORD r = CopyPathOut(w, lpBuffer, nBufferLength);
	if (r <= w.size() && lpFilePart) {
		const auto slash = w.find_last_of(L'/');
		*lpFilePart = (slash == std::wstring::npos || slash + 1 == w.size()) ? nullptr : lpBuffer + slash + 1;
	}
	return r;
}

DWORD WINAPI GetLongPathNameW(LPCWSTR lpszShortPath, LPWSTR lpszLongPath, DWORD cchBuffer)
{
	if (::access(ToNativePath(lpszShortPath).c_str(), F_OK) != 0) { SetLastError(ERROR_FILE_NOT_FOUND); return 0; }
	return CopyPathOut(lpszShortPath, lpszLongPath, cchBuffer);
}

DWORD WINAPI GetTempPathW(DWORD nBufferLength, LPWSTR lpBuffer)
{
	std::string tmp = std::filesystem::temp_directory_path().string();
	if (tmp.empty() || tmp.back() != '/') tmp += '/';
	return CopyPathOut(Utf8ToWide(tmp), lpBuffer, nBufferLength);
}

UINT WINAPI GetTempFileNameW(LPCWSTR lpPathName, LPCWSTR lpPrefixString, UINT uUnique, LPWSTR lpTempFileName)
{
	std::wstring dir = lpPathName ? lpPathName : L".";
	if (!dir.empty() && dir.back() != L'/' && dir.back() != L'\\') dir += L'/';
	const std::wstring prefix = std::wstring(lpPrefixString ? lpPrefixString : L"").substr(0, 3);
	static std::mt19937 rng{std::random_device{}()};
	for (int attempt = 0; attempt < 0x10000; ++attempt) {
		const UINT n = uUnique ? uUnique : static_cast<UINT>(rng() & 0xFFFF) + 1;
		wchar_t hex[8];
		swprintf_s(hex, 8, L"%X", n);
		const std::wstring name = dir + prefix + hex + L".tmp";
		if (uUnique) { std::wmemcpy(lpTempFileName, name.c_str(), name.size() + 1); return n; }
		const int fd = ::open(ToNativePath(name.c_str()).c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
		if (fd >= 0) {
			::close(fd);
			std::wmemcpy(lpTempFileName, name.c_str(), name.size() + 1);
			return n;
		}
		if (errno != EEXIST) { winport::SetLastErrorFromErrno(errno); return 0; }
	}
	SetLastError(ERROR_FILE_EXISTS);
	return 0;
}

UINT WINAPI GetDriveTypeW(LPCWSTR) { return DRIVE_FIXED; }

UINT WINAPI GetSystemDirectoryW(LPWSTR lpBuffer, UINT uSize) { return CopyPathOut(L"/usr/lib", lpBuffer, uSize); }
UINT WINAPI GetWindowsDirectoryW(LPWSTR lpBuffer, UINT uSize) { return CopyPathOut(L"/", lpBuffer, uSize); }

DWORD WINAPI GetEnvironmentVariableW(LPCWSTR lpName, LPWSTR lpBuffer, DWORD nSize)
{
	const char* v = std::getenv(winport::WideToUtf8(lpName ? lpName : L"").c_str());
	if (!v) { SetLastError(ERROR_ENVVAR_NOT_FOUND); return 0; }
	return CopyPathOut(Utf8ToWide(v), lpBuffer, nSize);
}

DWORD WINAPI SearchPathW(LPCWSTR lpPath, LPCWSTR lpFileName, LPCWSTR lpExtension, DWORD nBufferLength, LPWSTR lpBuffer, LPWSTR* lpFilePart)
{
	std::wstring name = lpFileName ? lpFileName : L"";
	if (lpExtension && name.find(L'.') == std::wstring::npos) name += lpExtension;
	std::vector<std::wstring> dirs;
	if (lpPath) dirs.emplace_back(lpPath);
	else {
		wchar_t cwd[MAX_PATH];
		if (GetCurrentDirectoryW(MAX_PATH, cwd)) dirs.emplace_back(cwd);
	}
	for (const auto& d : dirs) {
		const std::wstring cand = d + L"/" + name;
		if (::access(ToNativePath(cand.c_str()).c_str(), F_OK) == 0) return GetFullPathNameW(cand.c_str(), nBufferLength, lpBuffer, lpFilePart);
	}
	SetLastError(ERROR_FILE_NOT_FOUND);
	return 0;
}

/* ---- 時刻 ---- */
LONG WINAPI CompareFileTime(const FILETIME* a, const FILETIME* b)
{
	const long long x = FileTimeValue(*a), y = FileTimeValue(*b);
	return x < y ? -1 : x > y ? 1 : 0;
}

WINBOOL WINAPI FileTimeToSystemTime(const FILETIME* lpFileTime, LPSYSTEMTIME lpSystemTime)
{
	const long long v = FileTimeValue(*lpFileTime) - kEpochDiff;
	time_t t = static_cast<time_t>(v / 10000000LL);
	long long rem = v % 10000000LL;
	if (rem < 0) { --t; rem += 10000000LL; }
	struct tm tmv {};
	gmtime_r(&t, &tmv);
	*lpSystemTime = SYSTEMTIME{static_cast<WORD>(tmv.tm_year + 1900), static_cast<WORD>(tmv.tm_mon + 1), static_cast<WORD>(tmv.tm_wday),
	                           static_cast<WORD>(tmv.tm_mday), static_cast<WORD>(tmv.tm_hour), static_cast<WORD>(tmv.tm_min),
	                           static_cast<WORD>(tmv.tm_sec), static_cast<WORD>(rem / 10000)};
	return TRUE;
}

WINBOOL WINAPI SystemTimeToFileTime(const SYSTEMTIME* st, LPFILETIME lpFileTime)
{
	struct tm tmv {};
	tmv.tm_year = st->wYear - 1900;
	tmv.tm_mon = st->wMonth - 1;
	tmv.tm_mday = st->wDay;
	tmv.tm_hour = st->wHour;
	tmv.tm_min = st->wMinute;
	tmv.tm_sec = st->wSecond;
	const long long v = static_cast<long long>(timegm(&tmv)) * 10000000LL + st->wMilliseconds * 10000LL + kEpochDiff;
	*lpFileTime = FILETIME{static_cast<DWORD>(v & 0xFFFFFFFF), static_cast<DWORD>(v >> 32)};
	return TRUE;
}

WINBOOL WINAPI FileTimeToLocalFileTime(const FILETIME* lpFileTime, LPFILETIME lpLocalFileTime)
{
	const time_t t = static_cast<time_t>((FileTimeValue(*lpFileTime) - kEpochDiff) / 10000000LL);
	struct tm tmv {};
	localtime_r(&t, &tmv);
	const long long v = FileTimeValue(*lpFileTime) + static_cast<long long>(tmv.tm_gmtoff) * 10000000LL;
	*lpLocalFileTime = FILETIME{static_cast<DWORD>(v & 0xFFFFFFFF), static_cast<DWORD>(v >> 32)};
	return TRUE;
}

void WINAPI GetSystemTimeAsFileTime(LPFILETIME lpSystemTimeAsFileTime)
{
	struct timespec ts {};
	clock_gettime(CLOCK_REALTIME, &ts);
	*lpSystemTimeAsFileTime = ToFileTime(ts);
}

void WINAPI GetSystemTime(LPSYSTEMTIME lpSystemTime)
{
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	FileTimeToSystemTime(&ft, lpSystemTime);
}

void WINAPI GetLocalTime(LPSYSTEMTIME lpSystemTime)
{
	FILETIME ft, local;
	GetSystemTimeAsFileTime(&ft);
	FileTimeToLocalFileTime(&ft, &local);
	FileTimeToSystemTime(&local, lpSystemTime);
}

/* ---- shlwapi ---- */
LPWSTR WINAPI PathFindFileNameW(LPCWSTR pszPath)
{
	LPCWSTR last = pszPath;
	for (LPCWSTR p = pszPath; *p; ++p) {
		if ((*p == L'\\' || *p == L'/' || *p == L':') && p[1]) last = p + 1;
	}
	return const_cast<LPWSTR>(last);
}

WINBOOL WINAPI PathIsUNCW(LPCWSTR pszPath) { return pszPath && pszPath[0] == L'\\' && pszPath[1] == L'\\'; }

WINBOOL WINAPI PathIsDirectoryW(LPCWSTR pszPath)
{
	const DWORD a = GetFileAttributesW(pszPath);
	return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) ? FILE_ATTRIBUTE_DIRECTORY : FALSE;
}

WINBOOL WINAPI PathFileExistsW(LPCWSTR pszPath) { return GetFileAttributesW(pszPath) != INVALID_FILE_ATTRIBUTES; }

LPWSTR WINAPI PathAddBackslashW(LPWSTR pszPath)
{
	const size_t n = std::wcslen(pszPath);
	if (n == 0 || pszPath[n - 1] == L'\\' || pszPath[n - 1] == L'/') return pszPath + n;
	// 既に '/' 区切りのパスなら '/' を足す（winport の API は '/' 区切りで返す）
	pszPath[n] = std::wcschr(pszPath, L'/') ? L'/' : L'\\';
	pszPath[n + 1] = 0;
	return pszPath + n + 1;
}

WINBOOL WINAPI PathMatchSpecW(LPCWSTR pszFile, LPCWSTR pszSpec)
{
	const std::string file = winport::WideToUtf8(PathFindFileNameW(pszFile));
	std::wstring specs = pszSpec ? pszSpec : L"";
	size_t pos = 0;
	while (pos <= specs.size()) {
		const size_t end = specs.find(L';', pos);
		std::wstring one = specs.substr(pos, end == std::wstring::npos ? std::wstring::npos : end - pos);
		while (!one.empty() && one.front() == L' ') one.erase(0, 1);
		if (one == L"*.*") one = L"*";
		if (!one.empty() && fnmatch(winport::WideToUtf8(one).c_str(), file.c_str(), FNM_CASEFOLD) == 0) return TRUE;
		if (end == std::wstring::npos) break;
		pos = end + 1;
	}
	return FALSE;
}

}  // extern "C"
