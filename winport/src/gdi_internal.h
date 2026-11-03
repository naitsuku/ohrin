/*
	gdi_internal.h — GDI オブジェクトとデバイスコンテキストの内部表現

	座標: GDI 単位 = Cocoa のポイント（Retina では 1 単位 = scale 画素）。y は下向き。
	描画先は CGBitmapContext（ウィンドウの Surface か、メモリ DC に選択したビットマップ）。

	SPDX-License-Identifier: Zlib
*/
#pragma once
#include "user32_internal.h"

#include <unordered_map>

namespace winport::gdi {

enum class ObjType { font, brush, pen, bitmap, region, dc };

struct Object {
	explicit Object(ObjType t) : type(t) {}
	virtual ~Object() = default;
	ObjType type;
	bool stock = false;
};

struct Glyph {
	CTFontRef font;   //!< 描画に使うフォント（基本フォントかフォールバック）。Font が所有
	CGGlyph glyph;
	double advance;   //!< ポイント
};

struct Font : Object {
	Font() : Object(ObjType::font) {}
	~Font() override;
	LOGFONTW lf{};
	CTFontRef ct = nullptr;
	double ascent = 0, descent = 0, leading = 0;
	TEXTMETRICW tm{};
	std::mutex cacheMutex;
	std::unordered_map<char32_t, Glyph> cache;
	std::vector<CTFontRef> fallbacks;  //!< 解放用
	//! コードポイントのグリフ（無ければフォールバックを探す）
	Glyph GlyphFor(char32_t cp);
};

struct Brush : Object {
	Brush() : Object(ObjType::brush) {}
	UINT style = BS_SOLID;
	COLORREF color = 0;
	bool null() const { return style == BS_NULL; }
};

struct Pen : Object {
	Pen() : Object(ObjType::pen) {}
	int style = PS_SOLID;
	int width = 1;
	COLORREF color = 0;
};

struct Bitmap : Object {
	Bitmap() : Object(ObjType::bitmap) {}
	~Bitmap() override;
	int width = 0, height = 0;   //!< GDI 単位
	double scale = 1.0;
	int bpp = 32;
	CGContextRef ctx = nullptr;  //!< 上下反転済み（y 下向き）
	void* bits = nullptr;        //!< DIB セクションの場合の画素（ctx と共有）
	bool ownsBits = false;
};

struct Region : Object {
	Region() : Object(ObjType::region) {}
	std::vector<RECT> rects;     //!< 重なりのない矩形の集合（空 = 空領域）
	RECT Bounds() const;
};

struct DC : Object {
	DC() : Object(ObjType::dc) {}
	HWND hwnd = nullptr;                   //!< ウィンドウ DC
	std::shared_ptr<Surface> surface;      //!< ウィンドウ DC の描画面
	Bitmap* bitmap = nullptr;              //!< メモリ DC に選択中のビットマップ
	bool paintDC = false;                  //!< BeginPaint の DC
	RECT paintClip{};                      //!< BeginPaint の無効領域（paintDC のとき）
	HFONT font = nullptr;
	HBRUSH brush = nullptr;
	HPEN pen = nullptr;
	HBITMAP defaultBitmap = nullptr;
	COLORREF textColor = RGB(0, 0, 0);
	COLORREF bkColor = RGB(255, 255, 255);
	int bkMode = OPAQUE;
	UINT textAlign = TA_LEFT | TA_TOP;
	int rop2 = R2_COPYPEN;
	POINT pos{0, 0};
	POINT origin{0, 0};                    //!< SetViewportOrgEx
	bool hasClip = false;
	std::vector<RECT> clip;                //!< SelectClipRgn（デバイス座標）
	RECT dirty{};                          //!< 描画した範囲（Present 用、クライアント座標）
	bool dirtyAny = false;
	COLORREF dcBrushColor = RGB(255, 255, 255);
	COLORREF dcPenColor = RGB(0, 0, 0);

	struct Saved {
		HFONT font; HBRUSH brush; HPEN pen; COLORREF textColor, bkColor; int bkMode; UINT textAlign; int rop2;
		POINT pos, origin; bool hasClip; std::vector<RECT> clip;
	};
	std::vector<Saved> saved;

	CGContextRef Target() const;
	int TargetWidth() const;
	int TargetHeight() const;
};

Object* ObjectOf(HGDIOBJ h);
//! 生成した GDI オブジェクトを有効なハンドルとして登録する
void RegisterObject(Object* o);
DC* DCOf(HDC h);
Font* FontOf(HDC h);

//! ウィンドウ DC を作る（paint = BeginPaint 用。clip は無効領域）
HDC CreateWindowDC(HWND hwnd, const RECT* paintClip);
//! ウィンドウ DC を破棄し、描画範囲を画面へ反映する
void ReleaseWindowDC(HDC hdc);

//! 32bpp の直接アルファ画素（メモリ上 B,G,R,A）を DC へ合成する。全画素のアルファが 0 なら不透明として扱う
void DrawArgbPixels(HDC hdc, int x, int y, int w, int h, const uint32_t* px, int strideWords, double alpha);
//! DIB（BITMAPINFOHEADER + パレット + 画素）からビットマップを作る。bmBitsPixel は元の値のまま
HBITMAP CreateBitmapFromDib(const unsigned char* dib, size_t size);

CGColorRef CopyColor(COLORREF c);
inline CGFloat R(COLORREF c) { return GetRValue(c) / 255.0; }
inline CGFloat G(COLORREF c) { return GetGValue(c) / 255.0; }
inline CGFloat B(COLORREF c) { return GetBValue(c) / 255.0; }

//! y 下向き・指定倍率の CGBitmapContext を作る（data = nullptr なら確保）
CGContextRef CreateFlippedBitmapContext(int width, int height, double scale, void* data);

}  // namespace winport::gdi
