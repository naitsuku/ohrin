/*
	gdi_text.cpp — フォントと文字描画（CoreText）

	- LOGFONT の書体名は mac の書体に読み替える（ＭＳ ゴシック → 等幅の和文書体 等）
	- 文字幅は 1 文字ずつ整数に丸めて合計する（GDI と同じく、sakura の桁計算と一致させるため）
	- 基本フォントにグリフが無い文字は CoreText のフォールバックで描く
	- ExtTextOutW は lpDx で 1 文字ずつ位置を決める（sakura の描画はこれに依存する）

	SPDX-License-Identifier: Zlib
*/
#include "gdi_internal.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwchar>

#include "winport_internal.h"

namespace winport::gdi {

namespace {

CFStringRef ToCF(const std::wstring& s)
{
	std::vector<UniChar> u(s.begin(), s.end());
	return CFStringCreateWithCharacters(nullptr, u.data(), static_cast<CFIndex>(u.size()));
}

std::wstring FromCF(CFStringRef s)
{
	if (!s) return {};
	const CFIndex n = CFStringGetLength(s);
	std::vector<UniChar> u(n);
	CFStringGetCharacters(s, CFRangeMake(0, n), u.data());
	return std::wstring(u.begin(), u.end());
}

bool FontExists(const char* postscriptName)
{
	CFStringRef name = CFStringCreateWithCString(nullptr, postscriptName, kCFStringEncodingUTF8);
	CTFontRef f = CTFontCreateWithName(name, 12, nullptr);
	CFStringRef actual = CTFontCopyPostScriptName(f);
	const bool ok = CFStringCompare(actual, name, 0) == kCFCompareEqualTo;
	CFRelease(actual);
	CFRelease(f);
	CFRelease(name);
	return ok;
}

const char* MonospaceJapanese()
{
	static const char* name = [] {
		for (auto n : {"Osaka-Mono", "BIZUDGothic-Regular", "Menlo-Regular"}) {
			if (FontExists(n)) return n;
		}
		return "Menlo-Regular";
	}();
	return name;
}

//! Windows の書体名を mac の PostScript 名に読み替える。空なら名前をそのまま使う
std::string MapFace(const std::wstring& face, BYTE pitchAndFamily)
{
	static const struct { const wchar_t* win; int kind; } kMap[] = {
		// kind: 0 = 等幅和文, 1 = 和文ゴシック（プロポーショナル）, 2 = 明朝, 3 = 等幅欧文
		{L"ＭＳ ゴシック", 0}, {L"MS Gothic", 0}, {L"BIZ UDゴシック", 0}, {L"BIZ UDGothic", 0},
		// ＭＳ 明朝は等幅。mac に等幅の明朝は無く、桁が揃うことを優先して等幅のゴシックにする（印刷の既定フォント）
		{L"ＭＳ 明朝", 0}, {L"MS Mincho", 0}, {L"BIZ UD明朝", 0},
		{L"ＭＳ Ｐゴシック", 1}, {L"MS PGothic", 1}, {L"MS UI Gothic", 1}, {L"メイリオ", 1}, {L"Meiryo", 1},
		{L"Meiryo UI", 1}, {L"Yu Gothic", 1}, {L"游ゴシック", 1}, {L"Yu Gothic UI", 1}, {L"MS Shell Dlg", 1},
		{L"MS Shell Dlg 2", 1}, {L"Segoe UI", 1}, {L"Tahoma", 1}, {L"System", 1}, {L"Microsoft Sans Serif", 1},
		{L"Terminal", 3}, {L"Courier New", 3}, {L"Courier", 3}, {L"Consolas", 3}, {L"Fixedsys", 3}, {L"Lucida Console", 3},
		{L"ＭＳ Ｐ明朝", 2}, {L"MS PMincho", 2},
	};
	int kind = -1;
	for (auto& m : kMap) {
		if (_wcsicmp(m.win, face.c_str()) == 0) { kind = m.kind; break; }
	}
	if (kind < 0 && face.empty()) kind = ((pitchAndFamily & 3) == FIXED_PITCH) ? 0 : 1;
	switch (kind) {
	case 0: return MonospaceJapanese();
	case 1: return "HiraginoSans-W3";
	case 2: return "HiraMinProN-W3";
	case 3: return "Menlo-Regular";
	default: return {};
	}
}

CTFontRef CreateCTFont(const LOGFONTW& lf)
{
	const std::wstring face = lf.lfFaceName;
	std::string ps = MapFace(face, lf.lfPitchAndFamily);
	CFStringRef name = ps.empty() ? ToCF(face) : CFStringCreateWithCString(nullptr, ps.c_str(), kCFStringEncodingUTF8);
	CTFontRef base = CTFontCreateWithName(name, 12, nullptr);
	CFRelease(name);
	// 存在しない名前のときは CoreText が代替を返す。ファミリー名が一致しなければピッチで選び直す
	if (ps.empty()) {
		CFStringRef fam = CTFontCopyFamilyName(base);
		CFStringRef full = CTFontCopyFullName(base);
		const std::wstring f1 = FromCF(fam), f2 = FromCF(full);
		CFRelease(fam);
		CFRelease(full);
		if (_wcsicmp(f1.c_str(), face.c_str()) != 0 && _wcsicmp(f2.c_str(), face.c_str()) != 0) {
			CFRelease(base);
			ps = MapFace(L"", lf.lfPitchAndFamily);
			name = CFStringCreateWithCString(nullptr, ps.c_str(), kCFStringEncodingUTF8);
			base = CTFontCreateWithName(name, 12, nullptr);
			CFRelease(name);
		}
	}
	// 大きさ: lfHeight < 0 は文字の高さ（em）、> 0 はセルの高さ
	double em = 12;
	if (lf.lfHeight < 0) em = -lf.lfHeight;
	else if (lf.lfHeight > 0) {
		const double cell = CTFontGetAscent(base) + CTFontGetDescent(base);
		em = lf.lfHeight * 12.0 / (cell > 0 ? cell : 12.0);
	}
	CTFontRef sized = CTFontCreateCopyWithAttributes(base, em, nullptr, nullptr);
	CFRelease(base);
	CTFontSymbolicTraits traits = 0;
	if (lf.lfWeight >= FW_SEMIBOLD) traits |= kCTFontBoldTrait;
	if (lf.lfItalic) traits |= kCTFontItalicTrait;
	if (traits) {
		if (CTFontRef styled = CTFontCreateCopyWithSymbolicTraits(sized, em, nullptr, traits, traits)) {
			CFRelease(sized);
			sized = styled;
		}
	}
	return sized;
}

int RoundAdvance(double a) { return static_cast<int>(std::lround(a)); }

//! UTF-16 単位列を (コードポイント, 単位数) の列に分ける
template <typename F>
void ForEachCodepoint(const wchar_t* s, int n, F f)
{
	for (int i = 0; i < n;) {
		char32_t c = static_cast<char16_t>(s[i]);
		int units = 1;
		if (0xD800 <= c && c <= 0xDBFF && i + 1 < n) {
			const char32_t lo = static_cast<char16_t>(s[i + 1]);
			if (0xDC00 <= lo && lo <= 0xDFFF) { c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00); units = 2; }
		}
		f(c, i, units);
		i += units;
	}
}

}  // namespace

Glyph Font::GlyphFor(char32_t cp)
{
	std::lock_guard lock(cacheMutex);
	if (auto it = cache.find(cp); it != cache.end()) return it->second;
	UniChar u[2];
	CFIndex nu = 1;
	if (cp >= 0x10000) {
		const char32_t v = cp - 0x10000;
		u[0] = static_cast<UniChar>(0xD800 + (v >> 10));
		u[1] = static_cast<UniChar>(0xDC00 + (v & 0x3FF));
		nu = 2;
	} else {
		u[0] = static_cast<UniChar>(cp);
	}
	CGGlyph g[2] = {};
	CTFontRef use = ct;
	if (!CTFontGetGlyphsForCharacters(ct, u, g, nu) || g[0] == 0) {
		CFStringRef str = CFStringCreateWithCharacters(nullptr, u, nu);
		CTFontRef fb = CTFontCreateForString(ct, str, CFRangeMake(0, nu));
		CFRelease(str);
		if (fb && CTFontGetGlyphsForCharacters(fb, u, g, nu) && g[0]) {
			fallbacks.push_back(fb);
			use = fb;
		} else {
			if (fb) CFRelease(fb);
			g[0] = 0;
		}
	}
	CGSize adv{};
	CTFontGetAdvancesForGlyphs(use, kCTFontOrientationHorizontal, g, &adv, 1);
	Glyph r{use, g[0], adv.width};
	if (cp < 0x20) r.advance = 0;
	cache[cp] = r;
	return r;
}

Font* FontOf(HDC h)
{
	auto dc = DCOf(h);
	if (!dc) return static_cast<Font*>(ObjectOf(GetStockObject(SYSTEM_FONT)));
	auto f = static_cast<Font*>(ObjectOf(dc->font));
	return f ? f : static_cast<Font*>(ObjectOf(GetStockObject(SYSTEM_FONT)));
}

}  // namespace winport::gdi

using namespace winport;
using namespace winport::gdi;

namespace {

int TextWidth(Font* f, const wchar_t* s, int n, std::vector<int>* perUnit)
{
	int total = 0;
	if (perUnit) perUnit->assign(n, 0);
	ForEachCodepoint(s, n, [&](char32_t cp, int i, int units) {
		const int a = RoundAdvance(f->GlyphFor(cp).advance);
		total += a;
		if (perUnit) {
			(*perUnit)[i] = a;
			if (units == 2) (*perUnit)[i + 1] = 0;
		}
	});
	return total;
}

}  // namespace

extern "C" {

HFONT WINAPI CreateFontIndirectW(const LOGFONTW* lplf)
{
	auto f = new Font;
	f->lf = *lplf;
	f->ct = CreateCTFont(*lplf);
	f->ascent = std::ceil(CTFontGetAscent(f->ct));
	f->descent = std::ceil(CTFontGetDescent(f->ct));
	f->leading = std::round(CTFontGetLeading(f->ct));
	auto& tm = f->tm;
	tm.tmAscent = static_cast<LONG>(f->ascent);
	tm.tmDescent = static_cast<LONG>(f->descent);
	tm.tmHeight = tm.tmAscent + tm.tmDescent;
	const double em = CTFontGetSize(f->ct);
	tm.tmInternalLeading = std::max<LONG>(0, tm.tmHeight - static_cast<LONG>(std::lround(em)));
	tm.tmExternalLeading = static_cast<LONG>(f->leading);
	tm.tmAveCharWidth = RoundAdvance(f->GlyphFor(U'x').advance);
	const CGRect bbox = CTFontGetBoundingBox(f->ct);
	tm.tmMaxCharWidth = static_cast<LONG>(std::ceil(bbox.size.width));
	tm.tmWeight = lplf->lfWeight ? lplf->lfWeight : FW_NORMAL;
	tm.tmItalic = lplf->lfItalic;
	tm.tmUnderlined = lplf->lfUnderline;
	tm.tmStruckOut = lplf->lfStrikeOut;
	tm.tmFirstChar = 0x20;
	tm.tmLastChar = 0xFFFC;
	tm.tmDefaultChar = 0x30FB;
	tm.tmBreakChar = 0x20;
	const bool fixed = (CTFontGetSymbolicTraits(f->ct) & kCTFontMonoSpaceTrait) != 0 ||
	                   RoundAdvance(f->GlyphFor(U'i').advance) == RoundAdvance(f->GlyphFor(U'W').advance);
	// Windows の TMPF_FIXED_PITCH は「可変ピッチなら立つ」逆の意味
	tm.tmPitchAndFamily = static_cast<BYTE>((fixed ? 0 : TMPF_FIXED_PITCH) | TMPF_VECTOR | TMPF_TRUETYPE | (fixed ? FF_MODERN : FF_SWISS));
	tm.tmCharSet = lplf->lfCharSet;
	RegisterObject(f);
	return reinterpret_cast<HFONT>(f);
}

HFONT WINAPI CreateFontW(int cHeight, int cWidth, int cEscapement, int cOrientation, int cWeight, DWORD bItalic,
                         DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision,
                         DWORD iQuality, DWORD iPitchAndFamily, LPCWSTR pszFaceName)
{
	LOGFONTW lf{cHeight, cWidth, cEscapement, cOrientation, cWeight, static_cast<BYTE>(bItalic), static_cast<BYTE>(bUnderline),
	            static_cast<BYTE>(bStrikeOut), static_cast<BYTE>(iCharSet), static_cast<BYTE>(iOutPrecision),
	            static_cast<BYTE>(iClipPrecision), static_cast<BYTE>(iQuality), static_cast<BYTE>(iPitchAndFamily), {}};
	if (pszFaceName) wcsncpy_s(lf.lfFaceName, LF_FACESIZE, pszFaceName, _TRUNCATE);
	return CreateFontIndirectW(&lf);
}

WINBOOL WINAPI GetTextMetricsW(HDC hdc, LPTEXTMETRICW lptm)
{
	*lptm = FontOf(hdc)->tm;
	return TRUE;
}

int WINAPI GetTextFaceW(HDC hdc, int c, LPWSTR lpName)
{
	const auto& face = FontOf(hdc)->lf.lfFaceName;
	const int n = static_cast<int>(std::wcslen(face));
	if (!lpName) return n + 1;
	wcsncpy_s(lpName, c, face, _TRUNCATE);
	return std::min(n, c - 1);
}

WINBOOL WINAPI GetTextExtentPoint32W(HDC hdc, LPCWSTR lpString, int c, LPSIZE psizl)
{
	Font* f = FontOf(hdc);
	psizl->cx = c > 0 ? TextWidth(f, lpString, c, nullptr) : 0;
	psizl->cy = f->tm.tmHeight;
	return TRUE;
}

WINBOOL WINAPI GetTextExtentPointW(HDC hdc, LPCWSTR lpString, int c, LPSIZE lpsz) { return GetTextExtentPoint32W(hdc, lpString, c, lpsz); }

WINBOOL WINAPI GetTextExtentExPointW(HDC hdc, LPCWSTR lpszString, int cchString, int nMaxExtent, LPINT lpnFit, LPINT lpnDx, LPSIZE lpSize)
{
	Font* f = FontOf(hdc);
	std::vector<int> w;
	TextWidth(f, lpszString, cchString, &w);
	int sum = 0, fit = 0;
	for (int i = 0; i < cchString; ++i) {
		sum += w[i];
		if (lpnDx) lpnDx[i] = sum;
		if (sum <= nMaxExtent) fit = i + 1;
	}
	if (lpnFit) *lpnFit = fit;
	lpSize->cx = sum;
	lpSize->cy = f->tm.tmHeight;
	return TRUE;
}

WINBOOL WINAPI GetCharWidth32W(HDC hdc, UINT iFirst, UINT iLast, LPINT lpBuffer)
{
	Font* f = FontOf(hdc);
	for (UINT c = iFirst; c <= iLast; ++c) *lpBuffer++ = RoundAdvance(f->GlyphFor(c).advance);
	return TRUE;
}

WINBOOL WINAPI GetCharWidthW(HDC hdc, UINT iFirst, UINT iLast, LPINT lpBuffer) { return GetCharWidth32W(hdc, iFirst, iLast, lpBuffer); }

DWORD WINAPI GetFontLanguageInfo(HDC) { return 0; }

DWORD WINAPI GetCharacterPlacementW(HDC hdc, LPCWSTR lpString, int nCount, int, LPGCP_RESULTSW lpResults, DWORD)
{
	Font* f = FontOf(hdc);
	std::vector<int> w;
	const int total = TextWidth(f, lpString, nCount, &w);
	if (lpResults) {
		if (lpResults->lpDx) for (int i = 0; i < nCount && i < static_cast<int>(lpResults->nGlyphs); ++i) lpResults->lpDx[i] = w[i];
		if (lpResults->lpOutString) std::wmemcpy(lpResults->lpOutString, lpString, nCount);
		lpResults->nGlyphs = nCount;
		lpResults->nMaxFit = nCount;
	}
	return MAKELONG(total, f->tm.tmHeight);
}

WINBOOL WINAPI ExtTextOutW(HDC hdc, int x, int y, UINT options, const RECT* lprect, LPCWSTR lpString, UINT c, const INT* lpDx)
{
	auto dc = DCOf(hdc);
	if (!dc) return FALSE;
	CGContextRef ctx = dc->Target();
	if (!ctx) return TRUE;
	Font* f = FontOf(hdc);
	std::unique_lock<std::mutex> lk;
	if (dc->surface) lk = std::unique_lock(dc->surface->mutex);

	const int n = static_cast<int>(c);
	std::vector<int> adv;
	int total = 0;
	if (n > 0) {
		if (lpDx) {
			adv.assign(lpDx, lpDx + n);
			if (options & ETO_PDY) {
				std::vector<int> xs(n);
				for (int i = 0; i < n; ++i) xs[i] = lpDx[i * 2];
				adv = xs;
			}
			for (int a : adv) total += a;
		} else {
			total = TextWidth(f, lpString, n, &adv);
		}
	}
	if (dc->textAlign & TA_UPDATECP) { x = dc->pos.x; y = dc->pos.y; }
	const UINT halign = dc->textAlign & (TA_LEFT | TA_RIGHT | TA_CENTER);
	if (halign == TA_RIGHT) x -= total;
	else if (halign == TA_CENTER) x -= total / 2;
	double baseline;
	const UINT valign = dc->textAlign & (TA_TOP | TA_BASELINE | TA_BOTTOM);
	// MM_LOMETRIC などの論理座標は y が上向き。ydir はそのときの行の進む向き
	const double ydir = dc->Mapped() && dc->ScaleY() < 0 ? -1 : 1;
	if (valign == TA_BASELINE) baseline = y;
	else if (valign == TA_BOTTOM) baseline = y - ydir * f->descent;
	else baseline = y + ydir * f->ascent;

	CGContextSaveGState(ctx);
	if (dc->paintDC) CGContextClipToRect(ctx, CGRectMake(dc->paintClip.left, dc->paintClip.top, dc->paintClip.right - dc->paintClip.left, dc->paintClip.bottom - dc->paintClip.top));
	if (dc->hasClip) {
		std::vector<CGRect> rs;
		for (auto& r : dc->clip) rs.push_back(CGRectMake(r.left, r.top, r.right - r.left, r.bottom - r.top));
		if (rs.empty()) CGContextClipToRect(ctx, CGRectZero);
		else CGContextClipToRects(ctx, rs.data(), rs.size());
	}
	CGContextTranslateCTM(ctx, dc->origin.x, dc->origin.y);
	if (dc->Mapped()) CGContextScaleCTM(ctx, dc->ScaleX(), dc->ScaleY());

	RECT dirty{x, y, x + total, y + f->tm.tmHeight};
	if (lprect && (options & ETO_OPAQUE)) {
		CGContextSetRGBFillColor(ctx, R(dc->bkColor), G(dc->bkColor), B(dc->bkColor), 1);
		CGContextFillRect(ctx, CGRectMake(lprect->left, lprect->top, lprect->right - lprect->left, lprect->bottom - lprect->top));
		UnionRect(&dirty, &dirty, lprect);
	} else if (dc->bkMode == OPAQUE && n > 0) {
		CGContextSetRGBFillColor(ctx, R(dc->bkColor), G(dc->bkColor), B(dc->bkColor), 1);
		CGContextFillRect(ctx, CGRectMake(x, baseline - ydir * f->ascent, total, ydir * (f->ascent + f->descent)));
	}
	if (lprect && (options & ETO_CLIPPED)) {
		CGContextClipToRect(ctx, CGRectMake(lprect->left, lprect->top, lprect->right - lprect->left, lprect->bottom - lprect->top));
	}

	if (n > 0) {
		CGContextSetShouldAntialias(ctx, true);
		CGContextSetShouldSmoothFonts(ctx, true);
		CGContextSetRGBFillColor(ctx, R(dc->textColor), G(dc->textColor), B(dc->textColor), 1);
		CGContextSetTextMatrix(ctx, CGAffineTransformMakeScale(1, -ydir));
		// 同じフォントが続く範囲をまとめて描く
		std::vector<CGGlyph> glyphs;
		std::vector<CGPoint> points;
		CTFontRef runFont = nullptr;
		auto flush = [&] {
			if (runFont && !glyphs.empty()) CTFontDrawGlyphs(runFont, glyphs.data(), points.data(), glyphs.size(), ctx);
			glyphs.clear();
			points.clear();
		};
		double pen = x;
		ForEachCodepoint(lpString, n, [&](char32_t cp, int i, int units) {
			const Glyph g = f->GlyphFor(cp);
			if (g.glyph && cp >= 0x20) {
				if (g.font != runFont) { flush(); runFont = g.font; }
				glyphs.push_back(g.glyph);
				// 位置はテキスト空間（テキスト行列で上下反転済み）なので y の符号を反転して渡す
				points.push_back(CGPointMake(pen, -ydir * baseline));
			}
			pen += adv[i] + (units == 2 ? adv[i + 1] : 0);
		});
		flush();
		if (f->lf.lfUnderline || f->lf.lfStrikeOut) {
			const double thick = std::max(1.0, std::round(CTFontGetUnderlineThickness(f->ct)));
			if (f->lf.lfUnderline) CGContextFillRect(ctx, CGRectMake(x, baseline + ydir * std::max(1.0, std::round(-CTFontGetUnderlinePosition(f->ct))), total, ydir * thick));
			if (f->lf.lfStrikeOut) CGContextFillRect(ctx, CGRectMake(x, baseline - ydir * f->ascent / 3, total, ydir * thick));
		}
	}
	CGContextRestoreGState(ctx);
	if (dc->textAlign & TA_UPDATECP) dc->pos.x = x + total;
	OffsetRect(&dirty, dc->origin.x, dc->origin.y);
	if (dc->hwnd) {
		if (dc->dirtyAny) UnionRect(&dc->dirty, &dc->dirty, &dirty);
		else { dc->dirty = dirty; dc->dirtyAny = true; }
	}
	return TRUE;
}

WINBOOL WINAPI TextOutW(HDC hdc, int x, int y, LPCWSTR lpString, int c)
{
	return ExtTextOutW(hdc, x, y, 0, nullptr, lpString, static_cast<UINT>(c), nullptr);
}

int WINAPI DrawTextExW(HDC hdc, LPWSTR lpchText, int cchText, LPRECT lprc, UINT format, LPDRAWTEXTPARAMS)
{
	if (cchText < 0) cchText = static_cast<int>(std::wcslen(lpchText));
	Font* f = FontOf(hdc);
	// 行に分ける（DT_SINGLELINE なら 1 行）。&（ニーモニック）は DT_NOPREFIX でなければ取り除く
	std::vector<std::wstring> lines(1);
	for (int i = 0; i < cchText; ++i) {
		const wchar_t ch = lpchText[i];
		if (!(format & DT_SINGLELINE) && (ch == L'\n' || ch == L'\r')) {
			if (ch == L'\r' && i + 1 < cchText && lpchText[i + 1] == L'\n') ++i;
			lines.emplace_back();
			continue;
		}
		if (!(format & DT_NOPREFIX) && ch == L'&') {
			if (i + 1 < cchText && lpchText[i + 1] == L'&') { lines.back() += L'&'; ++i; }
			continue;
		}
		lines.back() += ch;
	}
	const int lineHeight = f->tm.tmHeight;
	int maxWidth = 0;
	for (auto& l : lines) maxWidth = std::max(maxWidth, TextWidth(f, l.c_str(), static_cast<int>(l.size()), nullptr));
	const int height = lineHeight * static_cast<int>(lines.size());
	if (format & DT_CALCRECT) {
		lprc->right = lprc->left + maxWidth;
		lprc->bottom = lprc->top + height;
		return height;
	}
	int y = lprc->top;
	if (format & DT_SINGLELINE) {
		if (format & DT_VCENTER) y = lprc->top + (lprc->bottom - lprc->top - lineHeight) / 2;
		else if (format & DT_BOTTOM) y = lprc->bottom - lineHeight;
	}
	for (auto& l : lines) {
		const int w = TextWidth(f, l.c_str(), static_cast<int>(l.size()), nullptr);
		int x = lprc->left;
		if (format & DT_CENTER) x = lprc->left + (lprc->right - lprc->left - w) / 2;
		else if (format & DT_RIGHT) x = lprc->right - w;
		ExtTextOutW(hdc, x, y, (format & DT_NOCLIP) ? 0 : ETO_CLIPPED, lprc, l.c_str(), static_cast<UINT>(l.size()), nullptr);
		y += lineHeight;
	}
	return height;
}

int WINAPI DrawTextW(HDC hdc, LPCWSTR lpchText, int cchText, LPRECT lprc, UINT format)
{
	return DrawTextExW(hdc, const_cast<LPWSTR>(lpchText), cchText, lprc, format, nullptr);
}

int WINAPI EnumFontFamiliesExW(HDC hdc, LPLOGFONTW lpLogfont, FONTENUMPROCW lpProc, LPARAM lParam, DWORD)
{
	CFArrayRef names = CTFontManagerCopyAvailableFontFamilyNames();
	int result = 1;
	const CFIndex n = CFArrayGetCount(names);
	for (CFIndex i = 0; i < n && result; ++i) {
		const std::wstring name = FromCF(static_cast<CFStringRef>(CFArrayGetValueAtIndex(names, i)));
		if (name.empty() || name[0] == L'.') continue;
		if (lpLogfont && lpLogfont->lfFaceName[0] && _wcsicmp(lpLogfont->lfFaceName, name.c_str()) != 0) continue;
		ENUMLOGFONTEXW elf{};
		elf.elfLogFont.lfHeight = -12;
		elf.elfLogFont.lfWeight = FW_NORMAL;
		elf.elfLogFont.lfCharSet = SHIFTJIS_CHARSET;
		wcsncpy_s(elf.elfLogFont.lfFaceName, LF_FACESIZE, name.c_str(), _TRUNCATE);
		wcsncpy_s(elf.elfFullName, LF_FULLFACESIZE, name.c_str(), _TRUNCATE);
		HFONT hf = CreateFontIndirectW(&elf.elfLogFont);
		auto fo = static_cast<Font*>(ObjectOf(hf));
		NEWTEXTMETRICEXW ntm{};
		std::memcpy(&ntm.ntmTm, &fo->tm, sizeof(TEXTMETRICW));
		elf.elfLogFont.lfPitchAndFamily = (fo->tm.tmPitchAndFamily & TMPF_FIXED_PITCH) ? VARIABLE_PITCH : FIXED_PITCH;
		DeleteObject(hf);
		result = lpProc(&elf.elfLogFont, reinterpret_cast<TEXTMETRICW*>(&ntm), TRUETYPE_FONTTYPE, lParam);
	}
	CFRelease(names);
	(void)hdc;
	return result;
}

int WINAPI EnumFontFamiliesW(HDC hdc, LPCWSTR lpLogfont, FONTENUMPROCW lpProc, LPARAM lParam)
{
	LOGFONTW lf{};
	lf.lfCharSet = DEFAULT_CHARSET;
	if (lpLogfont) wcsncpy_s(lf.lfFaceName, LF_FACESIZE, lpLogfont, _TRUNCATE);
	return EnumFontFamiliesExW(hdc, &lf, lpProc, lParam, 0);
}

}  // extern "C"
