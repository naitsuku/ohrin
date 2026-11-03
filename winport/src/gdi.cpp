/*
	gdi.cpp — デバイスコンテキスト、ブラシ・ペン・ビットマップ・リージョン、図形描画と転送

	user32 の描画関連（BeginPaint / GetDC / FillRect / ScrollWindowEx / DrawEdge 等）もここに置く。
	文字関連は gdi_text.cpp。

	SPDX-License-Identifier: Zlib
*/
#include "gdi_internal.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

#include "winport_internal.h"

namespace winport {

Surface::~Surface()
{
	if (ctx) CGContextRelease(ctx);
}

std::shared_ptr<Surface> CreateSurface(int width, int height, double scale, const Surface* copyFrom)
{
	auto s = std::make_shared<Surface>();
	s->width = width;
	s->height = height;
	s->scale = scale;
	s->ctx = gdi::CreateFlippedBitmapContext(width, height, scale, nullptr);
	// 既定は白で塗っておく（WM_ERASEBKGND 前に一瞬黒く見えるのを避ける）
	CGContextSetRGBFillColor(s->ctx, 1, 1, 1, 1);
	CGContextFillRect(s->ctx, CGRectMake(0, 0, width, height));
	if (copyFrom && copyFrom->ctx) {
		std::lock_guard lock(const_cast<Surface*>(copyFrom)->mutex);
		if (CGImageRef img = CGBitmapContextCreateImage(copyFrom->ctx)) {
			CGContextSaveGState(s->ctx);
			CGContextTranslateCTM(s->ctx, 0, copyFrom->height);
			CGContextScaleCTM(s->ctx, 1, -1);
			CGContextDrawImage(s->ctx, CGRectMake(0, 0, copyFrom->width, copyFrom->height), img);
			CGContextRestoreGState(s->ctx);
			CGImageRelease(img);
		}
	}
	return s;
}

}  // namespace winport

namespace winport::gdi {

namespace {

std::mutex& ObjMutex() { static auto* m = new std::mutex; return *m; }
std::set<Object*>& Objects() { static auto* s = new std::set<Object*>; return *s; }

template <typename T>
T* Register(T* o)
{
	std::lock_guard lock(ObjMutex());
	Objects().insert(o);
	return o;
}

HGDIOBJ Stock(int idx);

void AddDirty(DC* dc, const RECT& r)
{
	if (!dc->hwnd) return;
	if (dc->dirtyAny) UnionRect(&dc->dirty, &dc->dirty, &r);
	else { dc->dirty = r; dc->dirtyAny = true; }
}

//! 描画の前処理: 状態保存・クリップ・原点。戻り値 false なら描画先なし
struct DrawScope {
	DC* dc;
	CGContextRef ctx;
	std::unique_lock<std::mutex> lock;
	explicit DrawScope(DC* d) : dc(d), ctx(d ? d->Target() : nullptr)
	{
		if (!ctx) return;
		if (dc->surface) lock = std::unique_lock(dc->surface->mutex);
		CGContextSaveGState(ctx);
		if (dc->paintDC) CGContextClipToRect(ctx, CGRectMake(dc->paintClip.left, dc->paintClip.top,
		                                                     dc->paintClip.right - dc->paintClip.left, dc->paintClip.bottom - dc->paintClip.top));
		if (dc->hasClip) {
			std::vector<CGRect> rs;
			for (auto& r : dc->clip) rs.push_back(CGRectMake(r.left, r.top, r.right - r.left, r.bottom - r.top));
			if (rs.empty()) CGContextClipToRect(ctx, CGRectZero);
			else CGContextClipToRects(ctx, rs.data(), rs.size());
		}
		CGContextTranslateCTM(ctx, dc->origin.x, dc->origin.y);
	}
	~DrawScope() { if (ctx) CGContextRestoreGState(ctx); }
	explicit operator bool() const { return ctx != nullptr; }
	void Dirty(const RECT& logical)
	{
		RECT r = logical;
		OffsetRect(&r, dc->origin.x, dc->origin.y);
		AddDirty(dc, r);
	}
};

void SetFill(CGContextRef ctx, COLORREF c) { CGContextSetRGBFillColor(ctx, R(c), G(c), B(c), 1); }
void SetStroke(CGContextRef ctx, COLORREF c) { CGContextSetRGBStrokeColor(ctx, R(c), G(c), B(c), 1); }

COLORREF BrushColor(DC* dc, HBRUSH h)
{
	auto b = static_cast<Brush*>(ObjectOf(h));
	if (h == GetStockObject(DC_BRUSH)) return dc->dcBrushColor;
	return b ? b->color : RGB(255, 255, 255);
}

bool BrushIsNull(HBRUSH h)
{
	auto b = static_cast<Brush*>(ObjectOf(h));
	return !b || b->null();
}

//! R2_* を CG の合成モードに近似する
void ApplyRop2(CGContextRef ctx, int rop2)
{
	switch (rop2) {
	case R2_XORPEN: case R2_NOTXORPEN: case R2_NOT:
		CGContextSetBlendMode(ctx, kCGBlendModeDifference);
		break;
	default:
		break;
	}
}

bool PenSetup(DC* dc, CGContextRef ctx, int* widthOut)
{
	auto p = static_cast<Pen*>(ObjectOf(dc->pen));
	if (!p || p->style == PS_NULL) return false;
	const COLORREF color = dc->pen == GetStockObject(DC_PEN) ? dc->dcPenColor : p->color;
	const bool xorLike = dc->rop2 == R2_XORPEN || dc->rop2 == R2_NOTXORPEN || dc->rop2 == R2_NOT;
	SetStroke(ctx, xorLike ? RGB(255, 255, 255) : color);
	const int w = std::max(p->width, 1);
	CGContextSetLineWidth(ctx, w);
	switch (p->style & PS_STYLE_MASK) {
	case PS_DOT: { const CGFloat d[] = {1, 1}; CGContextSetLineDash(ctx, 0, d, 2); break; }
	case PS_DASH: { const CGFloat d[] = {4, 2}; CGContextSetLineDash(ctx, 0, d, 2); break; }
	case PS_DASHDOT: { const CGFloat d[] = {4, 2, 1, 2}; CGContextSetLineDash(ctx, 0, d, 4); break; }
	default: break;
	}
	ApplyRop2(ctx, dc->rop2);
	CGContextSetLineCap(ctx, kCGLineCapButt);
	if (widthOut) *widthOut = w;
	return true;
}

void FillRectWith(DrawScope& s, const RECT& r, COLORREF color)
{
	SetFill(s.ctx, color);
	CGContextFillRect(s.ctx, CGRectMake(r.left, r.top, r.right - r.left, r.bottom - r.top));
	s.Dirty(r);
}

// ---- リージョン演算（矩形の集合） ----
std::vector<RECT> Subtract(const RECT& a, const RECT& b)
{
	RECT i;
	if (!IntersectRect(&i, &a, &b)) return {a};
	std::vector<RECT> out;
	if (a.top < i.top) out.push_back({a.left, a.top, a.right, i.top});
	if (i.bottom < a.bottom) out.push_back({a.left, i.bottom, a.right, a.bottom});
	if (a.left < i.left) out.push_back({a.left, i.top, i.left, i.bottom});
	if (i.right < a.right) out.push_back({i.right, i.top, a.right, i.bottom});
	return out;
}

std::vector<RECT> SubtractAll(std::vector<RECT> a, const std::vector<RECT>& b)
{
	for (auto& rb : b) {
		std::vector<RECT> next;
		for (auto& ra : a) for (auto& x : Subtract(ra, rb)) next.push_back(x);
		a.swap(next);
	}
	return a;
}

std::vector<RECT> IntersectAll(const std::vector<RECT>& a, const std::vector<RECT>& b)
{
	std::vector<RECT> out;
	for (auto& ra : a) for (auto& rb : b) { RECT i; if (IntersectRect(&i, &ra, &rb)) out.push_back(i); }
	return out;
}

std::vector<RECT> UnionAll(const std::vector<RECT>& a, const std::vector<RECT>& b)
{
	auto out = a;
	for (auto& x : SubtractAll(b, a)) out.push_back(x);
	return out;
}

}  // namespace

Font::~Font()
{
	if (ct) CFRelease(ct);
	for (auto f : fallbacks) CFRelease(f);
}

Bitmap::~Bitmap()
{
	if (ctx) CGContextRelease(ctx);
	if (ownsBits) std::free(bits);
}

RECT Region::Bounds() const
{
	RECT b{};
	for (auto& r : rects) {
		if (IsRectEmpty(&b)) b = r;
		else UnionRect(&b, &b, &r);
	}
	return b;
}

CGContextRef DC::Target() const
{
	if (surface) return surface->ctx;
	if (bitmap) return bitmap->ctx;
	return nullptr;
}

int DC::TargetWidth() const { return surface ? surface->width : bitmap ? bitmap->width : 0; }
int DC::TargetHeight() const { return surface ? surface->height : bitmap ? bitmap->height : 0; }

void RegisterObject(Object* o) { Register(o); }

Object* ObjectOf(HGDIOBJ h)
{
	if (!h) return nullptr;
	std::lock_guard lock(ObjMutex());
	auto o = static_cast<Object*>(h);
	return Objects().count(o) ? o : nullptr;
}

DC* DCOf(HDC h)
{
	auto o = ObjectOf(h);
	return o && o->type == ObjType::dc ? static_cast<DC*>(o) : nullptr;
}

CGContextRef CreateFlippedBitmapContext(int width, int height, double scale, void* data)
{
	const size_t pw = static_cast<size_t>(std::max(1.0, std::ceil(width * scale)));
	const size_t ph = static_cast<size_t>(std::max(1.0, std::ceil(height * scale)));
	CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
	// Windows の 32bpp DIB と同じ BGRA 並び。アルファは使わない
	CGContextRef ctx = CGBitmapContextCreate(data, pw, ph, 8, data ? static_cast<size_t>(width) * 4 : 0, cs,
	                                         static_cast<uint32_t>(kCGImageAlphaNoneSkipFirst) | static_cast<uint32_t>(kCGBitmapByteOrder32Little));
	CGColorSpaceRelease(cs);
	if (!ctx) return nullptr;
	CGContextTranslateCTM(ctx, 0, ph);
	CGContextScaleCTM(ctx, scale, -scale);
	CGContextSetShouldAntialias(ctx, false);
	CGContextSetShouldSmoothFonts(ctx, false);
	return ctx;
}

HDC CreateWindowDC(HWND hwnd, const RECT* paintClip)
{
	auto w = GetWindowObject(hwnd);
	auto dc = Register(new DC);
	dc->font = static_cast<HFONT>(GetStockObject(SYSTEM_FONT));
	dc->brush = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
	dc->pen = static_cast<HPEN>(GetStockObject(BLACK_PEN));
	if (w) {
		dc->hwnd = hwnd;
		dc->surface = EnsureSurface(w);
		if (paintClip) {
			dc->paintDC = true;
			dc->paintClip = *paintClip;
		}
	}
	return reinterpret_cast<HDC>(dc);
}

void ReleaseWindowDC(HDC hdc)
{
	auto dc = DCOf(hdc);
	if (!dc) return;
	if (dc->hwnd && dc->dirtyAny) backend::Present(dc->hwnd, ToB(dc->dirty));
	{
		std::lock_guard lock(ObjMutex());
		Objects().erase(dc);
	}
	delete dc;
}

namespace {

HGDIOBJ Stock(int idx)
{
	static HGDIOBJ table[STOCK_LAST + 1] = {};
	static std::once_flag once;
	std::call_once(once, [] {
		auto brush = [](COLORREF c, UINT style = BS_SOLID) {
			auto b = Register(new Brush);
			b->color = c;
			b->style = style;
			b->stock = true;
			return static_cast<HGDIOBJ>(b);
		};
		auto pen = [](COLORREF c, int style = PS_SOLID) {
			auto p = Register(new Pen);
			p->color = c;
			p->style = style;
			p->stock = true;
			return static_cast<HGDIOBJ>(p);
		};
		table[WHITE_BRUSH] = brush(RGB(255, 255, 255));
		table[LTGRAY_BRUSH] = brush(RGB(192, 192, 192));
		table[GRAY_BRUSH] = brush(RGB(128, 128, 128));
		table[DKGRAY_BRUSH] = brush(RGB(64, 64, 64));
		table[BLACK_BRUSH] = brush(RGB(0, 0, 0));
		table[NULL_BRUSH] = brush(0, BS_NULL);
		table[DC_BRUSH] = brush(RGB(255, 255, 255));
		table[WHITE_PEN] = pen(RGB(255, 255, 255));
		table[BLACK_PEN] = pen(RGB(0, 0, 0));
		table[NULL_PEN] = pen(0, PS_NULL);
		table[DC_PEN] = pen(RGB(0, 0, 0));
		LOGFONTW lf{};
		lf.lfHeight = -12;
		lf.lfWeight = FW_NORMAL;
		lf.lfCharSet = SHIFTJIS_CHARSET;
		std::wcscpy(lf.lfFaceName, L"MS UI Gothic");
		auto mk = [&lf](const wchar_t* face, LONG h, BYTE pitch) {
			LOGFONTW l = lf;
			std::wcscpy(l.lfFaceName, face);
			l.lfHeight = h;
			l.lfPitchAndFamily = pitch;
			auto f = static_cast<Font*>(ObjectOf(CreateFontIndirectW(&l)));
			f->stock = true;
			return static_cast<HGDIOBJ>(f);
		};
		table[SYSTEM_FONT] = mk(L"MS UI Gothic", -12, VARIABLE_PITCH);
		table[DEFAULT_GUI_FONT] = mk(L"MS UI Gothic", -12, VARIABLE_PITCH);
		table[ANSI_VAR_FONT] = table[DEFAULT_GUI_FONT];
		table[DEVICE_DEFAULT_FONT] = table[SYSTEM_FONT];
		table[ANSI_FIXED_FONT] = mk(L"MS Gothic", -12, FIXED_PITCH);
		table[OEM_FIXED_FONT] = table[ANSI_FIXED_FONT];
		table[SYSTEM_FIXED_FONT] = table[ANSI_FIXED_FONT];
	});
	return (idx >= 0 && idx <= STOCK_LAST) ? table[idx] : nullptr;
}

}  // namespace

}  // namespace winport::gdi

using namespace winport;
using namespace winport::gdi;

extern "C" {

/* ---- オブジェクト ---- */
HGDIOBJ WINAPI GetStockObject(int i) { return Stock(i); }

WINBOOL WINAPI DeleteObject(HGDIOBJ ho)
{
	auto o = ObjectOf(ho);
	if (!o || o->stock) return o != nullptr;
	if (o->type == ObjType::dc) return FALSE;
	{
		std::lock_guard lock(ObjMutex());
		Objects().erase(o);
	}
	delete o;
	return TRUE;
}

HGDIOBJ WINAPI SelectObject(HDC hdc, HGDIOBJ h)
{
	auto dc = DCOf(hdc);
	auto o = ObjectOf(h);
	if (!dc) return nullptr;
	if (!o) {
		// リージョンは値で選択する（ハンドルが無効なら何もしない）
		return nullptr;
	}
	switch (o->type) {
	case ObjType::font: { auto old = dc->font; dc->font = static_cast<HFONT>(h); return old; }
	case ObjType::brush: { auto old = dc->brush; dc->brush = static_cast<HBRUSH>(h); return old; }
	case ObjType::pen: { auto old = dc->pen; dc->pen = static_cast<HPEN>(h); return old; }
	case ObjType::bitmap: {
		if (dc->hwnd) return nullptr;
		HBITMAP old = dc->bitmap ? reinterpret_cast<HBITMAP>(dc->bitmap) : dc->defaultBitmap;
		dc->bitmap = static_cast<Bitmap*>(o);
		return old;
	}
	case ObjType::region:
		SelectClipRgn(hdc, static_cast<HRGN>(h));
		return reinterpret_cast<HGDIOBJ>(static_cast<intptr_t>(SIMPLEREGION));
	default: return nullptr;
	}
}

HGDIOBJ WINAPI GetCurrentObject(HDC hdc, UINT type)
{
	auto dc = DCOf(hdc);
	if (!dc) return nullptr;
	switch (type) {
	case OBJ_FONT: return dc->font;
	case OBJ_BRUSH: return dc->brush;
	case OBJ_PEN: return dc->pen;
	case OBJ_BITMAP: return dc->bitmap ? reinterpret_cast<HGDIOBJ>(dc->bitmap) : dc->defaultBitmap;
	default: return nullptr;
	}
}

DWORD WINAPI GetObjectType(HGDIOBJ h)
{
	auto o = ObjectOf(h);
	if (!o) return 0;
	switch (o->type) {
	case ObjType::font: return OBJ_FONT;
	case ObjType::brush: return OBJ_BRUSH;
	case ObjType::pen: return OBJ_PEN;
	case ObjType::bitmap: return OBJ_BITMAP;
	case ObjType::region: return OBJ_REGION;
	case ObjType::dc: return static_cast<DC*>(o)->hwnd ? OBJ_DC : OBJ_MEMDC;
	}
	return 0;
}

int WINAPI GetObjectW(HANDLE h, int c, LPVOID pv)
{
	auto o = ObjectOf(h);
	if (!o) return 0;
	switch (o->type) {
	case ObjType::font:
		if (pv && c >= static_cast<int>(sizeof(LOGFONTW))) std::memcpy(pv, &static_cast<Font*>(o)->lf, sizeof(LOGFONTW));
		return sizeof(LOGFONTW);
	case ObjType::bitmap: {
		auto b = static_cast<Bitmap*>(o);
		BITMAP bm{0, b->width, b->height, b->width * 4, 1, static_cast<WORD>(b->bpp), b->bits};
		if (pv && c >= static_cast<int>(sizeof(BITMAP))) std::memcpy(pv, &bm, sizeof(BITMAP));
		return sizeof(BITMAP);
	}
	case ObjType::brush: {
		auto b = static_cast<Brush*>(o);
		LOGBRUSH lb{b->style, b->color, 0};
		if (pv && c >= static_cast<int>(sizeof(LOGBRUSH))) std::memcpy(pv, &lb, sizeof(LOGBRUSH));
		return sizeof(LOGBRUSH);
	}
	case ObjType::pen: {
		auto p = static_cast<Pen*>(o);
		LOGPEN lp{static_cast<UINT>(p->style), {p->width, 0}, p->color};
		if (pv && c >= static_cast<int>(sizeof(LOGPEN))) std::memcpy(pv, &lp, sizeof(LOGPEN));
		return sizeof(LOGPEN);
	}
	default: return 0;
	}
}

HBRUSH WINAPI CreateSolidBrush(COLORREF color)
{
	auto b = Register(new Brush);
	b->color = color & 0xFFFFFF;
	return reinterpret_cast<HBRUSH>(b);
}

HBRUSH WINAPI CreateBrushIndirect(const LOGBRUSH* plbrush)
{
	auto b = Register(new Brush);
	b->style = plbrush->lbStyle;
	b->color = plbrush->lbColor & 0xFFFFFF;
	return reinterpret_cast<HBRUSH>(b);
}

HBRUSH WINAPI CreatePatternBrush(HBITMAP)
{
	// TODO(winport): パターンブラシ。当面は灰色の単色
	return CreateSolidBrush(RGB(128, 128, 128));
}

HBRUSH WINAPI CreateHatchBrush(int, COLORREF color) { return CreateSolidBrush(color); }

HPEN WINAPI CreatePen(int iStyle, int cWidth, COLORREF color)
{
	auto p = Register(new Pen);
	p->style = iStyle;
	p->width = cWidth;
	p->color = color & 0xFFFFFF;
	return reinterpret_cast<HPEN>(p);
}

HPEN WINAPI CreatePenIndirect(const LOGPEN* plpen) { return CreatePen(static_cast<int>(plpen->lopnStyle), plpen->lopnWidth.x, plpen->lopnColor); }

HPEN WINAPI ExtCreatePen(DWORD iPenStyle, DWORD cWidth, const LOGBRUSH* plbrush, DWORD, const DWORD*)
{
	return CreatePen(static_cast<int>(iPenStyle & PS_STYLE_MASK), static_cast<int>(cWidth), plbrush ? plbrush->lbColor : 0);
}

COLORREF WINAPI SetDCBrushColor(HDC hdc, COLORREF color)
{
	auto dc = DCOf(hdc);
	if (!dc) return CLR_INVALID;
	const COLORREF old = dc->dcBrushColor;
	dc->dcBrushColor = color;
	return old;
}

COLORREF WINAPI SetDCPenColor(HDC hdc, COLORREF color)
{
	auto dc = DCOf(hdc);
	if (!dc) return CLR_INVALID;
	const COLORREF old = dc->dcPenColor;
	dc->dcPenColor = color;
	return old;
}

/* ---- DC ---- */
HDC WINAPI CreateCompatibleDC(HDC hdc)
{
	auto dc = Register(new DC);
	dc->font = static_cast<HFONT>(GetStockObject(SYSTEM_FONT));
	dc->brush = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
	dc->pen = static_cast<HPEN>(GetStockObject(BLACK_PEN));
	if (auto src = DCOf(hdc)) dc->font = src->font;
	// 既定の 1x1 ビットマップ（Windows と同じく、選択を戻すときに返す）
	dc->defaultBitmap = CreateBitmap(1, 1, 1, 32, nullptr);
	return reinterpret_cast<HDC>(dc);
}

HDC WINAPI CreateDCW(LPCWSTR, LPCWSTR, LPCWSTR, const DEVMODEW*) { return CreateCompatibleDC(nullptr); }

WINBOOL WINAPI DeleteDC(HDC hdc)
{
	auto dc = DCOf(hdc);
	if (!dc) return FALSE;
	if (dc->hwnd) { ReleaseWindowDC(hdc); return TRUE; }
	const HBITMAP def = dc->defaultBitmap;
	{
		std::lock_guard lock(ObjMutex());
		Objects().erase(dc);
	}
	delete dc;
	DeleteObject(def);
	return TRUE;
}

int WINAPI SaveDC(HDC hdc)
{
	auto dc = DCOf(hdc);
	if (!dc) return 0;
	dc->saved.push_back({dc->font, dc->brush, dc->pen, dc->textColor, dc->bkColor, dc->bkMode, dc->textAlign, dc->rop2,
	                     dc->pos, dc->origin, dc->hasClip, dc->clip});
	return static_cast<int>(dc->saved.size());
}

WINBOOL WINAPI RestoreDC(HDC hdc, int nSavedDC)
{
	auto dc = DCOf(hdc);
	if (!dc || dc->saved.empty()) return FALSE;
	size_t idx = nSavedDC < 0 ? dc->saved.size() + nSavedDC : static_cast<size_t>(nSavedDC - 1);
	if (idx >= dc->saved.size()) return FALSE;
	const auto s = dc->saved[idx];
	dc->saved.resize(idx);
	dc->font = s.font; dc->brush = s.brush; dc->pen = s.pen; dc->textColor = s.textColor; dc->bkColor = s.bkColor;
	dc->bkMode = s.bkMode; dc->textAlign = s.textAlign; dc->rop2 = s.rop2; dc->pos = s.pos; dc->origin = s.origin;
	dc->hasClip = s.hasClip; dc->clip = s.clip;
	return TRUE;
}

int WINAPI GetDeviceCaps(HDC, int index)
{
	const RECT scr = ToRECT(backend::ScreenRect());
	switch (index) {
	case LOGPIXELSX: case LOGPIXELSY: return 96;
	case BITSPIXEL: return 32;
	case PLANES: return 1;
	case HORZRES: case DESKTOPHORZRES: return scr.right - scr.left;
	case VERTRES: case DESKTOPVERTRES: return scr.bottom - scr.top;
	case HORZSIZE: return (scr.right - scr.left) * 254 / 960;
	case VERTSIZE: return (scr.bottom - scr.top) * 254 / 960;
	case NUMCOLORS: return -1;
	case TECHNOLOGY: return DT_RASDISPLAY;
	case RASTERCAPS: return RC_BITBLT | RC_STRETCHBLT | RC_DI_BITMAP;
	case SIZEPALETTE: return 0;
	case VREFRESH: return 60;
	default: return 0;
	}
}

COLORREF WINAPI SetTextColor(HDC hdc, COLORREF color)
{
	auto dc = DCOf(hdc);
	if (!dc) return CLR_INVALID;
	const COLORREF old = dc->textColor;
	dc->textColor = color & 0xFFFFFF;
	return old;
}

COLORREF WINAPI GetTextColor(HDC hdc) { auto dc = DCOf(hdc); return dc ? dc->textColor : CLR_INVALID; }

COLORREF WINAPI SetBkColor(HDC hdc, COLORREF color)
{
	auto dc = DCOf(hdc);
	if (!dc) return CLR_INVALID;
	const COLORREF old = dc->bkColor;
	dc->bkColor = color & 0xFFFFFF;
	return old;
}

COLORREF WINAPI GetBkColor(HDC hdc) { auto dc = DCOf(hdc); return dc ? dc->bkColor : CLR_INVALID; }

int WINAPI SetBkMode(HDC hdc, int mode)
{
	auto dc = DCOf(hdc);
	if (!dc) return 0;
	const int old = dc->bkMode;
	dc->bkMode = mode;
	return old;
}

int WINAPI GetBkMode(HDC hdc) { auto dc = DCOf(hdc); return dc ? dc->bkMode : 0; }

UINT WINAPI SetTextAlign(HDC hdc, UINT align)
{
	auto dc = DCOf(hdc);
	if (!dc) return GDI_ERROR;
	const UINT old = dc->textAlign;
	dc->textAlign = align;
	return old;
}

UINT WINAPI GetTextAlign(HDC hdc) { auto dc = DCOf(hdc); return dc ? dc->textAlign : GDI_ERROR; }

int WINAPI SetROP2(HDC hdc, int rop2)
{
	auto dc = DCOf(hdc);
	if (!dc) return 0;
	const int old = dc->rop2;
	dc->rop2 = rop2;
	return old;
}

int WINAPI GetROP2(HDC hdc) { auto dc = DCOf(hdc); return dc ? dc->rop2 : 0; }

int WINAPI SetMapMode(HDC, int) { return MM_TEXT; }
int WINAPI GetMapMode(HDC) { return MM_TEXT; }
int WINAPI SetStretchBltMode(HDC, int) { return BLACKONWHITE; }
int WINAPI SetGraphicsMode(HDC, int) { return GM_COMPATIBLE; }

WINBOOL WINAPI SetViewportOrgEx(HDC hdc, int x, int y, LPPOINT lppt)
{
	auto dc = DCOf(hdc);
	if (!dc) return FALSE;
	if (lppt) *lppt = dc->origin;
	dc->origin = {x, y};
	return TRUE;
}

WINBOOL WINAPI GetViewportOrgEx(HDC hdc, LPPOINT lppt) { auto dc = DCOf(hdc); if (!dc) return FALSE; *lppt = dc->origin; return TRUE; }

WINBOOL WINAPI SetWindowOrgEx(HDC hdc, int x, int y, LPPOINT lppt)
{
	auto dc = DCOf(hdc);
	if (!dc) return FALSE;
	if (lppt) *lppt = {-dc->origin.x, -dc->origin.y};
	dc->origin = {-x, -y};
	return TRUE;
}

WINBOOL WINAPI SetWindowExtEx(HDC, int, int, LPSIZE lpsz) { if (lpsz) *lpsz = {1, 1}; return TRUE; }
WINBOOL WINAPI SetViewportExtEx(HDC, int, int, LPSIZE lpsz) { if (lpsz) *lpsz = {1, 1}; return TRUE; }
WINBOOL WINAPI GetWindowExtEx(HDC, LPSIZE lpsize) { *lpsize = {1, 1}; return TRUE; }
WINBOOL WINAPI GetViewportExtEx(HDC, LPSIZE lpsize) { *lpsize = {1, 1}; return TRUE; }

WINBOOL WINAPI DPtoLP(HDC hdc, LPPOINT lppt, int c)
{
	auto dc = DCOf(hdc);
	if (!dc) return FALSE;
	for (int i = 0; i < c; ++i) { lppt[i].x -= dc->origin.x; lppt[i].y -= dc->origin.y; }
	return TRUE;
}

WINBOOL WINAPI LPtoDP(HDC hdc, LPPOINT lppt, int c)
{
	auto dc = DCOf(hdc);
	if (!dc) return FALSE;
	for (int i = 0; i < c; ++i) { lppt[i].x += dc->origin.x; lppt[i].y += dc->origin.y; }
	return TRUE;
}

/* ---- リージョン ---- */
HRGN WINAPI CreateRectRgn(int x1, int y1, int x2, int y2)
{
	auto r = Register(new Region);
	RECT rc{std::min(x1, x2), std::min(y1, y2), std::max(x1, x2), std::max(y1, y2)};
	if (!IsRectEmpty(&rc)) r->rects.push_back(rc);
	return reinterpret_cast<HRGN>(r);
}

HRGN WINAPI CreateRectRgnIndirect(const RECT* lprect) { return CreateRectRgn(lprect->left, lprect->top, lprect->right, lprect->bottom); }

int WINAPI CombineRgn(HRGN hrgnDst, HRGN hrgnSrc1, HRGN hrgnSrc2, int iMode)
{
	auto d = static_cast<Region*>(ObjectOf(hrgnDst));
	auto a = static_cast<Region*>(ObjectOf(hrgnSrc1));
	auto b = static_cast<Region*>(ObjectOf(hrgnSrc2));
	if (!d || !a) return ERROR;
	std::vector<RECT> res;
	switch (iMode) {
	case RGN_COPY: res = a->rects; break;
	case RGN_AND: if (!b) return ERROR; res = IntersectAll(a->rects, b->rects); break;
	case RGN_OR: if (!b) return ERROR; res = UnionAll(a->rects, b->rects); break;
	case RGN_DIFF: if (!b) return ERROR; res = SubtractAll(a->rects, b->rects); break;
	case RGN_XOR: if (!b) return ERROR; res = UnionAll(SubtractAll(a->rects, b->rects), SubtractAll(b->rects, a->rects)); break;
	default: return ERROR;
	}
	d->rects = res;
	return res.empty() ? NULLREGION : res.size() == 1 ? SIMPLEREGION : COMPLEXREGION;
}

int WINAPI OffsetRgn(HRGN hrgn, int x, int y)
{
	auto r = static_cast<Region*>(ObjectOf(hrgn));
	if (!r) return ERROR;
	for (auto& rc : r->rects) OffsetRect(&rc, x, y);
	return r->rects.empty() ? NULLREGION : SIMPLEREGION;
}

int WINAPI GetRgnBox(HRGN hrgn, LPRECT lprc)
{
	auto r = static_cast<Region*>(ObjectOf(hrgn));
	if (!r) return ERROR;
	*lprc = r->Bounds();
	return r->rects.empty() ? NULLREGION : SIMPLEREGION;
}

WINBOOL WINAPI PtInRegion(HRGN hrgn, int x, int y)
{
	auto r = static_cast<Region*>(ObjectOf(hrgn));
	if (!r) return FALSE;
	for (auto& rc : r->rects) if (PtInRect(&rc, POINT{x, y})) return TRUE;
	return FALSE;
}

WINBOOL WINAPI SetRectRgn(HRGN hrgn, int left, int top, int right, int bottom)
{
	auto r = static_cast<Region*>(ObjectOf(hrgn));
	if (!r) return FALSE;
	r->rects.clear();
	RECT rc{left, top, right, bottom};
	if (!IsRectEmpty(&rc)) r->rects.push_back(rc);
	return TRUE;
}

int WINAPI SelectClipRgn(HDC hdc, HRGN hrgn)
{
	auto dc = DCOf(hdc);
	if (!dc) return ERROR;
	auto r = static_cast<Region*>(ObjectOf(hrgn));
	if (!r) { dc->hasClip = false; dc->clip.clear(); return SIMPLEREGION; }
	dc->hasClip = true;
	dc->clip = r->rects;
	return r->rects.empty() ? NULLREGION : SIMPLEREGION;
}

int WINAPI ExtSelectClipRgn(HDC hdc, HRGN hrgn, int mode)
{
	if (mode == RGN_COPY) return SelectClipRgn(hdc, hrgn);
	auto dc = DCOf(hdc);
	auto r = static_cast<Region*>(ObjectOf(hrgn));
	if (!dc || !r) return ERROR;
	if (!dc->hasClip) { dc->hasClip = true; dc->clip = {RECT{0, 0, dc->TargetWidth(), dc->TargetHeight()}}; }
	if (mode == RGN_AND) dc->clip = IntersectAll(dc->clip, r->rects);
	else if (mode == RGN_OR) dc->clip = UnionAll(dc->clip, r->rects);
	else if (mode == RGN_DIFF) dc->clip = SubtractAll(dc->clip, r->rects);
	return dc->clip.empty() ? NULLREGION : SIMPLEREGION;
}

int WINAPI GetClipRgn(HDC hdc, HRGN hrgn)
{
	auto dc = DCOf(hdc);
	auto r = static_cast<Region*>(ObjectOf(hrgn));
	if (!dc || !r) return -1;
	if (!dc->hasClip) return 0;
	r->rects = dc->clip;
	return 1;
}

int WINAPI IntersectClipRect(HDC hdc, int left, int top, int right, int bottom)
{
	auto dc = DCOf(hdc);
	if (!dc) return ERROR;
	RECT rc{left + dc->origin.x, top + dc->origin.y, right + dc->origin.x, bottom + dc->origin.y};
	if (!dc->hasClip) { dc->hasClip = true; dc->clip = {RECT{0, 0, dc->TargetWidth(), dc->TargetHeight()}}; }
	dc->clip = IntersectAll(dc->clip, {rc});
	return dc->clip.empty() ? NULLREGION : SIMPLEREGION;
}

int WINAPI ExcludeClipRect(HDC hdc, int left, int top, int right, int bottom)
{
	auto dc = DCOf(hdc);
	if (!dc) return ERROR;
	RECT rc{left + dc->origin.x, top + dc->origin.y, right + dc->origin.x, bottom + dc->origin.y};
	if (!dc->hasClip) { dc->hasClip = true; dc->clip = {RECT{0, 0, dc->TargetWidth(), dc->TargetHeight()}}; }
	dc->clip = SubtractAll(dc->clip, {rc});
	return dc->clip.empty() ? NULLREGION : COMPLEXREGION;
}

int WINAPI GetClipBox(HDC hdc, LPRECT lprect)
{
	auto dc = DCOf(hdc);
	if (!dc) return ERROR;
	RECT r{0, 0, dc->TargetWidth(), dc->TargetHeight()};
	if (dc->paintDC) IntersectRect(&r, &r, &dc->paintClip);
	if (dc->hasClip) {
		RECT b{};
		for (auto& c : dc->clip) { if (IsRectEmpty(&b)) b = c; else UnionRect(&b, &b, &c); }
		IntersectRect(&r, &r, &b);
	}
	OffsetRect(&r, -dc->origin.x, -dc->origin.y);
	*lprect = r;
	return IsRectEmpty(&r) ? NULLREGION : SIMPLEREGION;
}

WINBOOL WINAPI FillRgn(HDC hdc, HRGN hrgn, HBRUSH hbr)
{
	auto r = static_cast<Region*>(ObjectOf(hrgn));
	if (!r) return FALSE;
	for (auto& rc : r->rects) FillRect(hdc, &rc, hbr);
	return TRUE;
}

WINBOOL WINAPI PaintRgn(HDC hdc, HRGN hrgn)
{
	auto dc = DCOf(hdc);
	return dc ? FillRgn(hdc, hrgn, dc->brush) : FALSE;
}

/* ---- 図形 ---- */
WINBOOL WINAPI MoveToEx(HDC hdc, int x, int y, LPPOINT lppt)
{
	auto dc = DCOf(hdc);
	if (!dc) return FALSE;
	if (lppt) *lppt = dc->pos;
	dc->pos = {x, y};
	return TRUE;
}

WINBOOL WINAPI LineTo(HDC hdc, int x, int y)
{
	auto dc = DCOf(hdc);
	if (!dc) return FALSE;
	DrawScope s(dc);
	if (s) {
		int w;
		if (PenSetup(dc, s.ctx, &w)) {
			// Windows の線は画素の中心を通り、終点を含まない
			const double off = (w % 2) ? 0.5 : 0.0;
			const double x0 = dc->pos.x, y0 = dc->pos.y;
			double x1 = x, y1 = y;
			const double ax = (x0 == x1) ? off : 0, ay = (y0 == y1) ? off : 0;
			CGContextBeginPath(s.ctx);
			CGContextMoveToPoint(s.ctx, x0 + ax + (y0 == y1 ? 0 : off), y0 + ay + (x0 == x1 ? 0 : off));
			CGContextAddLineToPoint(s.ctx, x1 + ax + (y0 == y1 ? 0 : off), y1 + ay + (x0 == x1 ? 0 : off));
			CGContextStrokePath(s.ctx);
			s.Dirty(RECT{std::min<LONG>(dc->pos.x, x) - w, std::min<LONG>(dc->pos.y, y) - w, std::max<LONG>(dc->pos.x, x) + w, std::max<LONG>(dc->pos.y, y) + w});
		}
	}
	dc->pos = {x, y};
	return TRUE;
}

WINBOOL WINAPI Polyline(HDC hdc, const POINT* apt, int cpt)
{
	if (cpt < 2) return FALSE;
	auto dc = DCOf(hdc);
	if (!dc) return FALSE;
	const POINT saved = dc->pos;
	MoveToEx(hdc, apt[0].x, apt[0].y, nullptr);
	for (int i = 1; i < cpt; ++i) LineTo(hdc, apt[i].x, apt[i].y);
	dc->pos = saved;
	return TRUE;
}

WINBOOL WINAPI PolyPolyline(HDC hdc, const POINT* apt, const DWORD* asz, DWORD csz)
{
	for (DWORD i = 0; i < csz; ++i) {
		Polyline(hdc, apt, static_cast<int>(asz[i]));
		apt += asz[i];
	}
	return TRUE;
}

WINBOOL WINAPI Polygon(HDC hdc, const POINT* apt, int cpt)
{
	auto dc = DCOf(hdc);
	if (!dc || cpt < 2) return FALSE;
	DrawScope s(dc);
	if (!s) return TRUE;
	CGContextBeginPath(s.ctx);
	CGContextMoveToPoint(s.ctx, apt[0].x + 0.5, apt[0].y + 0.5);
	RECT bounds{apt[0].x, apt[0].y, apt[0].x, apt[0].y};
	for (int i = 1; i < cpt; ++i) {
		CGContextAddLineToPoint(s.ctx, apt[i].x + 0.5, apt[i].y + 0.5);
		bounds.left = std::min(bounds.left, apt[i].x); bounds.top = std::min(bounds.top, apt[i].y);
		bounds.right = std::max(bounds.right, apt[i].x); bounds.bottom = std::max(bounds.bottom, apt[i].y);
	}
	CGContextClosePath(s.ctx);
	if (!BrushIsNull(dc->brush)) {
		SetFill(s.ctx, BrushColor(dc, dc->brush));
		CGContextDrawPath(s.ctx, PenSetup(dc, s.ctx, nullptr) ? kCGPathFillStroke : kCGPathFill);
	} else if (PenSetup(dc, s.ctx, nullptr)) {
		CGContextStrokePath(s.ctx);
	}
	InflateRect(&bounds, 2, 2);
	s.Dirty(bounds);
	return TRUE;
}

WINBOOL WINAPI Rectangle(HDC hdc, int left, int top, int right, int bottom)
{
	auto dc = DCOf(hdc);
	if (!dc) return FALSE;
	DrawScope s(dc);
	if (!s) return TRUE;
	if (!BrushIsNull(dc->brush)) {
		SetFill(s.ctx, BrushColor(dc, dc->brush));
		CGContextFillRect(s.ctx, CGRectMake(left, top, right - left, bottom - top));
	}
	int w;
	if (PenSetup(dc, s.ctx, &w)) {
		CGContextStrokeRect(s.ctx, CGRectMake(left + w / 2.0, top + w / 2.0, right - left - w, bottom - top - w));
	}
	s.Dirty(RECT{left, top, right, bottom});
	return TRUE;
}

WINBOOL WINAPI RoundRect(HDC hdc, int left, int top, int right, int bottom, int width, int height)
{
	auto dc = DCOf(hdc);
	if (!dc) return FALSE;
	DrawScope s(dc);
	if (!s) return TRUE;
	CGPathRef path = CGPathCreateWithRoundedRect(CGRectMake(left + 0.5, top + 0.5, right - left - 1, bottom - top - 1),
	                                             std::min(width / 2.0, (right - left - 1) / 2.0), std::min(height / 2.0, (bottom - top - 1) / 2.0), nullptr);
	if (!BrushIsNull(dc->brush)) {
		SetFill(s.ctx, BrushColor(dc, dc->brush));
		CGContextAddPath(s.ctx, path);
		CGContextFillPath(s.ctx);
	}
	if (PenSetup(dc, s.ctx, nullptr)) {
		CGContextAddPath(s.ctx, path);
		CGContextStrokePath(s.ctx);
	}
	CGPathRelease(path);
	s.Dirty(RECT{left, top, right, bottom});
	return TRUE;
}

WINBOOL WINAPI Ellipse(HDC hdc, int left, int top, int right, int bottom)
{
	auto dc = DCOf(hdc);
	if (!dc) return FALSE;
	DrawScope s(dc);
	if (!s) return TRUE;
	const CGRect r = CGRectMake(left + 0.5, top + 0.5, right - left - 1, bottom - top - 1);
	if (!BrushIsNull(dc->brush)) { SetFill(s.ctx, BrushColor(dc, dc->brush)); CGContextFillEllipseInRect(s.ctx, r); }
	if (PenSetup(dc, s.ctx, nullptr)) CGContextStrokeEllipseInRect(s.ctx, r);
	s.Dirty(RECT{left, top, right, bottom});
	return TRUE;
}

COLORREF WINAPI SetPixel(HDC hdc, int x, int y, COLORREF color)
{
	auto dc = DCOf(hdc);
	if (!dc) return CLR_INVALID;
	DrawScope s(dc);
	if (s) FillRectWith(s, RECT{x, y, x + 1, y + 1}, color);
	return color;
}

COLORREF WINAPI GetPixel(HDC hdc, int x, int y)
{
	auto dc = DCOf(hdc);
	if (!dc || !dc->Target()) return CLR_INVALID;
	CGContextRef ctx = dc->Target();
	const double scale = dc->surface ? dc->surface->scale : dc->bitmap->scale;
	auto data = static_cast<unsigned char*>(CGBitmapContextGetData(ctx));
	const size_t bpr = CGBitmapContextGetBytesPerRow(ctx);
	const size_t px = static_cast<size_t>((x + dc->origin.x) * scale), py = static_cast<size_t>((y + dc->origin.y) * scale);
	if (!data || px >= CGBitmapContextGetWidth(ctx) || py >= CGBitmapContextGetHeight(ctx)) return CLR_INVALID;
	const unsigned char* p = data + py * bpr + px * 4;  // BGRX
	return RGB(p[2], p[1], p[0]);
}

/* ---- ビットマップ ---- */
HBITMAP WINAPI CreateBitmap(int nWidth, int nHeight, UINT, UINT nBitCount, const VOID* lpBits)
{
	auto b = Register(new Bitmap);
	b->width = std::max(nWidth, 1);
	b->height = std::max(nHeight, 1);
	b->bpp = static_cast<int>(nBitCount);
	b->ctx = CreateFlippedBitmapContext(b->width, b->height, 1.0, nullptr);
	if (lpBits && nBitCount == 32) {
		// 上から下への 32bpp 画素
		auto dst = static_cast<unsigned char*>(CGBitmapContextGetData(b->ctx));
		const size_t bpr = CGBitmapContextGetBytesPerRow(b->ctx);
		for (int y = 0; y < b->height; ++y) std::memcpy(dst + y * bpr, static_cast<const unsigned char*>(lpBits) + y * b->width * 4, b->width * 4);
	}
	return reinterpret_cast<HBITMAP>(b);
}

HBITMAP WINAPI CreateCompatibleBitmap(HDC hdc, int cx, int cy)
{
	auto dc = DCOf(hdc);
	auto b = Register(new Bitmap);
	b->width = std::max(cx, 1);
	b->height = std::max(cy, 1);
	b->scale = (dc && dc->surface) ? dc->surface->scale : backend::ScaleFactor();
	b->ctx = CreateFlippedBitmapContext(b->width, b->height, b->scale, nullptr);
	return reinterpret_cast<HBITMAP>(b);
}

HBITMAP WINAPI CreateDIBSection(HDC, const BITMAPINFO* pbmi, UINT, VOID** ppvBits, HANDLE, DWORD)
{
	const auto& h = pbmi->bmiHeader;
	auto b = Register(new Bitmap);
	b->width = std::max<int>(h.biWidth, 1);
	b->height = std::max<int>(h.biHeight < 0 ? -h.biHeight : h.biHeight, 1);
	b->bpp = h.biBitCount;
	if (h.biBitCount != 32) {
		ReportUnimplemented("gdi32", "CreateDIBSection(non-32bpp)");
	}
	// TODO(winport): biHeight > 0（ボトムアップ）の DIB は行の並びが逆になる。直接画素を書く箇所で要確認
	b->bits = std::calloc(static_cast<size_t>(b->width) * b->height, 4);
	b->ownsBits = true;
	b->ctx = CreateFlippedBitmapContext(b->width, b->height, 1.0, b->bits);
	if (ppvBits) *ppvBits = b->bits;
	return reinterpret_cast<HBITMAP>(b);
}

namespace {

//! src の (sx,sy,w,h) を dst の (x,y,w,h) へ描く（拡大縮小あり）
bool Blit(DC* d, int x, int y, int w, int h, DC* sdc, int sx, int sy, int sw, int sh, DWORD rop)
{
	DrawScope s(d);
	if (!s) return true;
	const RECT dst{x, y, x + w, y + h};
	switch (rop) {
	case BLACKNESS: FillRectWith(s, dst, RGB(0, 0, 0)); return true;
	case WHITENESS: FillRectWith(s, dst, RGB(255, 255, 255)); return true;
	case PATCOPY: FillRectWith(s, dst, BrushColor(d, d->brush)); return true;
	case DSTINVERT:
		CGContextSetBlendMode(s.ctx, kCGBlendModeDifference);
		FillRectWith(s, dst, RGB(255, 255, 255));
		return true;
	case PATINVERT:
		CGContextSetBlendMode(s.ctx, kCGBlendModeDifference);
		FillRectWith(s, dst, BrushColor(d, d->brush));
		return true;
	default: break;
	}
	if (!sdc || !sdc->Target()) return false;
	CGImageRef full;
	double sscale;
	{
		std::unique_lock<std::mutex> lk;
		if (sdc->surface && sdc->surface != d->surface) lk = std::unique_lock(sdc->surface->mutex);
		full = CGBitmapContextCreateImage(sdc->Target());
		sscale = sdc->surface ? sdc->surface->scale : sdc->bitmap->scale;
	}
	if (!full) return false;
	const CGRect crop = CGRectMake((sx + sdc->origin.x) * sscale, (sy + sdc->origin.y) * sscale, sw * sscale, sh * sscale);
	CGImageRef part = CGImageCreateWithImageInRect(full, crop);
	CGImageRelease(full);
	if (!part) return false;
	switch (rop) {
	case SRCINVERT: CGContextSetBlendMode(s.ctx, kCGBlendModeDifference); break;
	case SRCAND: CGContextSetBlendMode(s.ctx, kCGBlendModeMultiply); break;
	case SRCPAINT: CGContextSetBlendMode(s.ctx, kCGBlendModeLighten); break;
	default: break;
	}
	// ctx は y 下向きなので、画像を描く間だけ上下を戻す
	CGContextTranslateCTM(s.ctx, x, y + h);
	CGContextScaleCTM(s.ctx, 1, -1);
	CGContextSetInterpolationQuality(s.ctx, kCGInterpolationNone);
	CGContextDrawImage(s.ctx, CGRectMake(0, 0, w, h), part);
	CGImageRelease(part);
	s.Dirty(dst);
	return true;
}

}  // namespace

WINBOOL WINAPI BitBlt(HDC hdc, int x, int y, int cx, int cy, HDC hdcSrc, int x1, int y1, DWORD rop)
{
	auto d = DCOf(hdc);
	if (!d) return FALSE;
	return Blit(d, x, y, cx, cy, DCOf(hdcSrc), x1, y1, cx, cy, rop);
}

WINBOOL WINAPI StretchBlt(HDC hdcDest, int xDest, int yDest, int wDest, int hDest, HDC hdcSrc, int xSrc, int ySrc, int wSrc, int hSrc, DWORD rop)
{
	auto d = DCOf(hdcDest);
	if (!d) return FALSE;
	return Blit(d, xDest, yDest, wDest, hDest, DCOf(hdcSrc), xSrc, ySrc, wSrc, hSrc, rop);
}

WINBOOL WINAPI PatBlt(HDC hdc, int x, int y, int w, int h, DWORD rop)
{
	auto d = DCOf(hdc);
	if (!d) return FALSE;
	if (w < 0) { x += w; w = -w; }
	if (h < 0) { y += h; h = -h; }
	return Blit(d, x, y, w, h, nullptr, 0, 0, 0, 0, rop);
}

WINBOOL WINAPI AlphaBlend(HDC hdcDest, int xoriginDest, int yoriginDest, int wDest, int hDest, HDC hdcSrc,
                          int xoriginSrc, int yoriginSrc, int wSrc, int hSrc, BLENDFUNCTION)
{
	return StretchBlt(hdcDest, xoriginDest, yoriginDest, wDest, hDest, hdcSrc, xoriginSrc, yoriginSrc, wSrc, hSrc, SRCCOPY);
}

WINBOOL WINAPI TransparentBlt(HDC hdcDest, int xoriginDest, int yoriginDest, int wDest, int hDest, HDC hdcSrc,
                              int xoriginSrc, int yoriginSrc, int wSrc, int hSrc, UINT)
{
	return StretchBlt(hdcDest, xoriginDest, yoriginDest, wDest, hDest, hdcSrc, xoriginSrc, yoriginSrc, wSrc, hSrc, SRCCOPY);
}

/* ---- user32 の描画系 ---- */
HDC WINAPI GetDC(HWND hWnd) { return CreateWindowDC(hWnd, nullptr); }
HDC WINAPI GetDCEx(HWND hWnd, HRGN, DWORD) { return CreateWindowDC(hWnd, nullptr); }
HDC WINAPI GetWindowDC(HWND hWnd) { return CreateWindowDC(hWnd, nullptr); }

int WINAPI ReleaseDC(HWND, HDC hDC)
{
	ReleaseWindowDC(hDC);
	return 1;
}

HDC WINAPI BeginPaint(HWND hWnd, LPPAINTSTRUCT lpPaint)
{
	auto w = GetWindowObject(hWnd);
	*lpPaint = {};
	if (!w) return nullptr;
	RECT rc;
	bool erase;
	{
		std::lock_guard lock(UserMutex());
		const SIZE s = ClientSize(w);
		rc = w->updatePending ? w->updateRect : RECT{0, 0, s.cx, s.cy};
		erase = w->eraseBackground;
		w->updatePending = false;
		w->eraseBackground = false;
	}
	HDC hdc = CreateWindowDC(hWnd, &rc);
	if (erase) erase = SendMessageW(hWnd, WM_ERASEBKGND, reinterpret_cast<WPARAM>(hdc), 0) == 0;
	lpPaint->hdc = hdc;
	lpPaint->rcPaint = rc;
	lpPaint->fErase = erase;
	return hdc;
}

WINBOOL WINAPI EndPaint(HWND hWnd, const PAINTSTRUCT* lpPaint)
{
	auto dc = DCOf(lpPaint->hdc);
	if (dc) {
		// 描いた範囲に関わらず、無効領域全体を画面へ反映する
		AddDirty(dc, lpPaint->rcPaint);
		ReleaseWindowDC(lpPaint->hdc);
	}
	(void)hWnd;
	return TRUE;
}

int WINAPI FillRect(HDC hDC, const RECT* lprc, HBRUSH hbr)
{
	auto dc = DCOf(hDC);
	if (!dc) return 0;
	if (reinterpret_cast<ULONG_PTR>(hbr) <= COLOR_MENUBAR + 1) hbr = GetSysColorBrush(static_cast<int>(reinterpret_cast<ULONG_PTR>(hbr)) - 1);
	if (BrushIsNull(hbr)) return 1;
	DrawScope s(dc);
	if (s) FillRectWith(s, *lprc, BrushColor(dc, hbr));
	return 1;
}

int WINAPI FrameRect(HDC hDC, const RECT* lprc, HBRUSH hbr)
{
	const RECT& r = *lprc;
	RECT e[4] = {{r.left, r.top, r.right, r.top + 1}, {r.left, r.bottom - 1, r.right, r.bottom},
	             {r.left, r.top, r.left + 1, r.bottom}, {r.right - 1, r.top, r.right, r.bottom}};
	for (auto& x : e) FillRect(hDC, &x, hbr);
	return 1;
}

WINBOOL WINAPI InvertRect(HDC hDC, const RECT* lprc)
{
	return PatBlt(hDC, lprc->left, lprc->top, lprc->right - lprc->left, lprc->bottom - lprc->top, DSTINVERT);
}

WINBOOL WINAPI DrawFocusRect(HDC hDC, const RECT* lprc)
{
	auto dc = DCOf(hDC);
	if (!dc) return FALSE;
	DrawScope s(dc);
	if (!s) return TRUE;
	CGContextSetBlendMode(s.ctx, kCGBlendModeDifference);
	SetStroke(s.ctx, RGB(255, 255, 255));
	CGContextSetLineWidth(s.ctx, 1);
	const CGFloat d[] = {1, 1};
	CGContextSetLineDash(s.ctx, 0, d, 2);
	CGContextStrokeRect(s.ctx, CGRectMake(lprc->left + 0.5, lprc->top + 0.5, lprc->right - lprc->left - 1, lprc->bottom - lprc->top - 1));
	s.Dirty(*lprc);
	return TRUE;
}

WINBOOL WINAPI DrawEdge(HDC hdc, LPRECT qrc, UINT edge, UINT grfFlags)
{
	const HBRUSH light = GetSysColorBrush(COLOR_3DHILIGHT), shadow = GetSysColorBrush(COLOR_3DSHADOW);
	const bool raised = (edge & BDR_RAISEDOUTER) != 0;
	RECT r = *qrc;
	auto line = [&](RECT x, HBRUSH b) { FillRect(hdc, &x, b); };
	if (grfFlags & BF_TOP) line({r.left, r.top, r.right, r.top + 1}, raised ? light : shadow);
	if (grfFlags & BF_LEFT) line({r.left, r.top, r.left + 1, r.bottom}, raised ? light : shadow);
	if (grfFlags & BF_BOTTOM) line({r.left, r.bottom - 1, r.right, r.bottom}, raised ? shadow : light);
	if (grfFlags & BF_RIGHT) line({r.right - 1, r.top, r.right, r.bottom}, raised ? shadow : light);
	if (grfFlags & BF_ADJUST) InflateRect(qrc, -1, -1);
	if (grfFlags & BF_MIDDLE) { InflateRect(&r, -1, -1); FillRect(hdc, &r, GetSysColorBrush(COLOR_BTNFACE)); }
	return TRUE;
}

WINBOOL WINAPI DrawFrameControl(HDC hdc, LPRECT lprc, UINT, UINT)
{
	DrawEdge(hdc, lprc, EDGE_RAISED, BF_RECT | BF_MIDDLE);
	return TRUE;
}

int WINAPI ScrollWindowEx(HWND hWnd, int dx, int dy, const RECT* prcScroll, const RECT* prcClip, HRGN hrgnUpdate,
                          LPRECT prcUpdate, UINT flags)
{
	auto w = GetWindowObject(hWnd);
	if (!w) return ERROR;
	auto surface = EnsureSurface(w);
	RECT client{0, 0, surface->width, surface->height};
	RECT area = prcScroll ? *prcScroll : client;
	if (prcClip) IntersectRect(&area, &area, prcClip);
	IntersectRect(&area, &area, &client);
	{
		std::lock_guard lock(surface->mutex);
		if (CGImageRef img = CGBitmapContextCreateImage(surface->ctx)) {
			CGContextSaveGState(surface->ctx);
			CGContextClipToRect(surface->ctx, CGRectMake(area.left, area.top, area.right - area.left, area.bottom - area.top));
			CGContextTranslateCTM(surface->ctx, dx, dy + surface->height);
			CGContextScaleCTM(surface->ctx, 1, -1);
			CGContextDrawImage(surface->ctx, CGRectMake(0, 0, surface->width, surface->height), img);
			CGContextRestoreGState(surface->ctx);
			CGImageRelease(img);
		}
	}
	// 露出した帯
	RECT exposed{};
	if (dy > 0) exposed = {area.left, area.top, area.right, std::min(area.bottom, area.top + dy)};
	else if (dy < 0) exposed = {area.left, std::max(area.top, area.bottom + dy), area.right, area.bottom};
	if (dx > 0) { RECT e{area.left, area.top, std::min(area.right, area.left + dx), area.bottom}; if (IsRectEmpty(&exposed)) exposed = e; else UnionRect(&exposed, &exposed, &e); }
	else if (dx < 0) { RECT e{std::max(area.left, area.right + dx), area.top, area.right, area.bottom}; if (IsRectEmpty(&exposed)) exposed = e; else UnionRect(&exposed, &exposed, &e); }
	if (prcUpdate) *prcUpdate = exposed;
	if (hrgnUpdate) SetRectRgn(hrgnUpdate, exposed.left, exposed.top, exposed.right, exposed.bottom);
	// 無効領域もずらす
	{
		std::lock_guard lock(UserMutex());
		if (w->updatePending) OffsetRect(&w->updateRect, dx, dy);
	}
	if ((flags & SW_INVALIDATE) && !IsRectEmpty(&exposed)) InvalidateRect(hWnd, &exposed, (flags & SW_ERASE) != 0);
	backend::Present(hWnd, ToB(area));
	return IsRectEmpty(&exposed) ? NULLREGION : SIMPLEREGION;
}

WINBOOL WINAPI ScrollWindow(HWND hWnd, int XAmount, int YAmount, const RECT* lpRect, const RECT* lpClipRect)
{
	ScrollWindowEx(hWnd, XAmount, YAmount, lpRect, lpClipRect, nullptr, nullptr, SW_INVALIDATE | SW_ERASE);
	return TRUE;
}

WINBOOL WINAPI GdiFlush(void) { return TRUE; }

}  // extern "C"
