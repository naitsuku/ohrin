/*
	imm32.cpp — IMM API

	mac の IME は表示系の NSTextInputClient（cocoa_backend.mm）が受ける。未確定文字列は表示系が
	キャレット位置に描くので、Win32 側には確定文字列だけが届く。
	bridge::PostImeResult → WM_IME_COMPOSITION(GCS_RESULTSTR) → ImmGetCompositionStringW の順に渡す。

	SPDX-License-Identifier: Zlib
*/
#include "gdi_internal.h"

#include <imm.h>

#include <cstring>

namespace winport {

thread_local std::wstring t_imeResult;  //!< 確定文字列（WM_IME_COMPOSITION の間だけ有効）

}  // namespace winport

using namespace winport;

extern "C" {

// HIMC はウィンドウごとに区別しない（状態を持たない）
HIMC WINAPI ImmGetContext(HWND hWnd) { return reinterpret_cast<HIMC>(hWnd); }
WINBOOL WINAPI ImmReleaseContext(HWND, HIMC) { return TRUE; }
HIMC WINAPI ImmAssociateContext(HWND, HIMC hIMC) { return hIMC; }

LONG WINAPI ImmGetCompositionStringW(HIMC, DWORD dwIndex, LPVOID lpBuf, DWORD dwBufLen)
{
	if (dwIndex != GCS_RESULTSTR) return 0;
	const DWORD bytes = static_cast<DWORD>(t_imeResult.size() * sizeof(WCHAR));
	if (dwBufLen == 0) return static_cast<LONG>(bytes);
	const DWORD n = bytes < dwBufLen ? bytes : dwBufLen;
	std::memcpy(lpBuf, t_imeResult.data(), n);
	return static_cast<LONG>(n);
}

WINBOOL WINAPI ImmSetCompositionStringW(HIMC, DWORD, LPVOID, DWORD, LPVOID, DWORD) { return FALSE; }

// 入力モードは mac 側が管理する。sakura のキャレット色の切り替えには使わない
WINBOOL WINAPI ImmGetOpenStatus(HIMC) { return FALSE; }
WINBOOL WINAPI ImmSetOpenStatus(HIMC, WINBOOL) { return TRUE; }
WINBOOL WINAPI ImmGetConversionStatus(HIMC, LPDWORD lpfdwConversion, LPDWORD lpfdwSentence)
{
	if (lpfdwConversion) *lpfdwConversion = 0;
	if (lpfdwSentence) *lpfdwSentence = 0;
	return TRUE;
}
WINBOOL WINAPI ImmSetConversionStatus(HIMC, DWORD, DWORD) { return TRUE; }
DWORD WINAPI ImmGetProperty(HKL, DWORD) { return 0; }
WINBOOL WINAPI ImmSetCompositionWindow(HIMC, LPCOMPOSITIONFORM) { return TRUE; }

WINBOOL WINAPI ImmSetCompositionFontW(HIMC, LPLOGFONTW lplf)
{
	// 未確定文字列も編集フォントで描く
	HFONT h = CreateFontIndirectW(lplf);
	if (auto f = static_cast<gdi::Font*>(gdi::ObjectOf(h))) {
		CFStringRef name = CTFontCopyPostScriptName(f->ct);
		char buf[256] = {};
		CFStringGetCString(name, buf, sizeof(buf), kCFStringEncodingUTF8);
		CFRelease(name);
		backend::SetImeFont(buf, CTFontGetSize(f->ct));
	}
	DeleteObject(h);
	return TRUE;
}

}  // extern "C"
