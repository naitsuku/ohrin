/*
	cocoa_backend.mm — 表示系（AppKit）。backend.h の backend::* を実装する

	- トップレベルの HWND ごとに NSWindow（WPWindow）とコンテンツビュー、子 HWND ごとに WPView を作る
	- WPView は y 下向き（isFlipped）。ビューの範囲はトップレベルならコンテンツ領域、子ならウィンドウ矩形で、
	  その中のクライアント矩形に描画面（Surface）の画像を描く。枠とスクロールバーはクライアントの外側
	- 入力は bridge::Post* で所有スレッドのキューに積むだけで、Win32 のコードはここでは動かさない
	- このファイルは Windows ヘッダを含めない（BOOL や interface が Cocoa と衝突するため）

	SPDX-License-Identifier: Zlib
*/
#import <Cocoa/Cocoa.h>

#include <cmath>
#include <functional>
#include <map>
#include <mutex>

#include "backend.h"

using BRect = winport::backend::Rect;  // Carbon の Rect と区別する
namespace bridge = winport::bridge;

namespace {

// Win32 の値（windows.h を含めないのでここで定義する）
constexpr int SB_PAGEUP = 2, SB_PAGEDOWN = 3, SB_THUMBPOSITION = 4, SB_THUMBTRACK = 5, SB_ENDSCROLL = 8;
constexpr uintptr_t IDC_ARROW = 32512, IDC_IBEAM = 32513, IDC_WAIT = 32514, IDC_CROSS = 32515, IDC_SIZENWSE = 32642,
                    IDC_SIZENESW = 32643, IDC_SIZEWE = 32644, IDC_SIZENS = 32645, IDC_SIZEALL = 32646, IDC_NO = 32648,
                    IDC_HAND = 32649, IDC_APPSTARTING = 32650;
constexpr int kScrollBarWidth = 15;

bool g_available = false;
bool g_quitting = false;
double g_scale = 2.0;
BRect g_screen{0, 0, 1440, 900};
BRect g_work{0, 25, 1440, 900};
int g_titleBar = 28;
CGFloat g_primaryHeight = 900;  // Cocoa ⇔ Win32 のスクリーン座標変換用

// キャレット（全体で 1 つ）
struct CaretState {
	void* hwnd = nullptr;
	BRect rc{};
	bool visible = false;
	bool blinkOn = true;
} g_caret;

// IME の未確定文字列（全体で 1 つ。表示はキャレットのあるビュー）
NSAttributedString* g_marked = nil;
NSRange g_markedSelection = {0, 0};
NSFont* g_imeFont = nil;

NSRect ToCocoaScreen(const BRect& r)
{
	return NSMakeRect(r.left, g_primaryHeight - r.bottom, r.Width(), r.Height());
}

BRect FromCocoaScreen(NSRect r)
{
	const int left = static_cast<int>(std::lround(r.origin.x));
	const int top = static_cast<int>(std::lround(g_primaryHeight - (r.origin.y + r.size.height)));
	return {left, top, left + static_cast<int>(std::lround(r.size.width)), top + static_cast<int>(std::lround(r.size.height))};
}

unsigned ModifiersOf(NSEventModifierFlags f)
{
	unsigned m = 0;
	if (f & NSEventModifierFlagShift) m |= bridge::kShift;
	if (f & NSEventModifierFlagControl) m |= bridge::kControl;
	if (f & NSEventModifierFlagOption) m |= bridge::kAlt;
	if (f & NSEventModifierFlagCommand) m |= bridge::kCommand;
	const NSUInteger buttons = [NSEvent pressedMouseButtons];
	if (buttons & 1) m |= bridge::kLeft;
	if (buttons & 2) m |= bridge::kRight;
	if (buttons & 4) m |= bridge::kMiddle;
	return m;
}

std::u16string ToU16(NSString* s)
{
	std::u16string out(s.length, u'\0');
	[s getCharacters:reinterpret_cast<unichar*>(out.data()) range:NSMakeRange(0, s.length)];
	return out;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
@class WPView;

@interface WPWindow : NSWindow
@property(nonatomic) void* hwnd;
@property(nonatomic) BOOL applyingFrame;
@end

@interface WPView : NSView <NSTextInputClient>
@property(nonatomic) void* hwnd;
@property(nonatomic) BOOL isContentView;
@property(nonatomic, strong) NSScroller* vScroller;
@property(nonatomic, strong) NSScroller* hScroller;
@property(nonatomic) double wheelAccumY;
@property(nonatomic) double wheelAccumX;
@property(nonatomic, strong) NSEvent* currentKey;  //!< interpretKeyEvents 中のキーイベント
- (void)syncScrollBars;
@end

@interface WPPlatform : NSObject
@property(nonatomic, strong) WPWindow* window;
@property(nonatomic, strong) WPView* view;
@end

@implementation WPPlatform
@end

namespace {

WPPlatform* PlatformOf(void* hwnd)
{
	const auto info = bridge::GetInfo(hwnd);
	return info.platform ? (__bridge WPPlatform*)info.platform : nil;
}

void ApplyFrame(void* hwnd);

//! hwnd の表示系オブジェクトを用意する（親が未作成なら作らない）
WPPlatform* Realize(void* hwnd)
{
	const auto info = bridge::GetInfo(hwnd);
	if (!info.valid) return nil;
	if (info.platform) return (__bridge WPPlatform*)info.platform;
	WPPlatform* p = [WPPlatform new];
	if (info.topLevel) {
		NSWindowStyleMask style = NSWindowStyleMaskBorderless;
		if (info.captioned) {
			style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable;
			if (info.minimizable) style |= NSWindowStyleMaskMiniaturizable;
			if (info.resizable) style |= NSWindowStyleMaskResizable;
		}
		BRect content = info.windowRect;
		if (info.captioned) content.top += g_titleBar;
		WPWindow* win = [[WPWindow alloc] initWithContentRect:ToCocoaScreen(content) styleMask:style backing:NSBackingStoreBuffered defer:NO];
		win.hwnd = hwnd;
		win.releasedWhenClosed = NO;
		win.delegate = (id<NSWindowDelegate>)win;
		win.title = [NSString stringWithCharacters:reinterpret_cast<const unichar*>(info.title.data()) length:info.title.size()];
		win.acceptsMouseMovedEvents = YES;
		WPView* view = [[WPView alloc] initWithFrame:NSMakeRect(0, 0, content.Width(), content.Height())];
		view.hwnd = hwnd;
		view.isContentView = YES;
		view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
		win.contentView = view;
		[win makeFirstResponder:view];
		p.window = win;
		p.view = view;
	} else {
		WPPlatform* parent = Realize(info.parent);
		if (!parent) return nil;
		// 親を作る過程で自分も作られていることがある
		if (void* existing = bridge::GetInfo(hwnd).platform) return (__bridge WPPlatform*)existing;
		WPView* view = [[WPView alloc] initWithFrame:NSZeroRect];
		view.hwnd = hwnd;
		view.clipsToBounds = YES;  // Win32 の子ウィンドウは親の範囲外に描かれない
		[parent.view addSubview:view positioned:NSWindowBelow relativeTo:nil];
		p.view = view;
	}
	bridge::SetPlatform(hwnd, (__bridge_retained void*)p);
	ApplyFrame(hwnd);
	for (void* child : bridge::Children(hwnd)) Realize(child);
	[p.view syncScrollBars];
	return p;
}

void ApplyFrame(void* hwnd)
{
	const auto info = bridge::GetInfo(hwnd);
	if (!info.platform) return;
	WPPlatform* p = (__bridge WPPlatform*)info.platform;
	if (info.topLevel) {
		p.window.applyingFrame = YES;
		[p.window setFrame:ToCocoaScreen(info.windowRect) display:NO];
		p.window.applyingFrame = NO;
	} else {
		const auto parent = bridge::GetInfo(info.parent);
		const BRect r = info.windowRect;
		p.view.frame = NSMakeRect(r.left + parent.clientInView.left, r.top + parent.clientInView.top, r.Width(), r.Height());
		p.view.hidden = !info.selfVisible;
	}
	[p.view syncScrollBars];
	[p.view setNeedsDisplay:YES];
}

NSCursor* CursorFor(uintptr_t id)
{
	switch (id) {
	case IDC_IBEAM: return [NSCursor IBeamCursor];
	case IDC_CROSS: return [NSCursor crosshairCursor];
	case IDC_SIZEWE: return [NSCursor resizeLeftRightCursor];
	case IDC_SIZENS: return [NSCursor resizeUpDownCursor];
	case IDC_HAND: return [NSCursor pointingHandCursor];
	case IDC_NO: return [NSCursor operationNotAllowedCursor];
	case IDC_SIZEALL: return [NSCursor openHandCursor];
	case IDC_SIZENWSE: case IDC_SIZENESW: return [NSCursor crosshairCursor];
	case IDC_WAIT: case IDC_APPSTARTING: case IDC_ARROW: default: return [NSCursor arrowCursor];
	}
}

void OnMain(dispatch_block_t block) { dispatch_async(dispatch_get_main_queue(), block); }

}  // namespace

// ---------------------------------------------------------------------------------------------
@implementation WPWindow

- (BOOL)canBecomeKeyWindow { return YES; }
- (BOOL)canBecomeMainWindow { return YES; }

// キー入力は Cocoa のキーボード操作（スペースでボタンを押す等）に渡さず、すべて sakura に送る。
// 例外は Cmd+Q（アプリの終了）だけ
- (void)sendEvent:(NSEvent*)e
{
	if (e.type == NSEventTypeKeyDown || e.type == NSEventTypeKeyUp) {
		const BOOL quit = (e.modifierFlags & NSEventModifierFlagCommand) && [e.charactersIgnoringModifiers.lowercaseString isEqualToString:@"q"];
		if (!quit && [self.contentView isKindOfClass:WPView.class]) {
			if (e.type == NSEventTypeKeyDown) [(WPView*)self.contentView keyDown:e];
			else [(WPView*)self.contentView keyUp:e];
			return;
		}
	}
	[super sendEvent:e];
}

- (BOOL)windowShouldClose:(NSWindow*)sender
{
	bridge::CloseRequested(self.hwnd);
	return NO;
}

- (void)reportFrame
{
	if (self.applyingFrame) return;
	bridge::FrameMovedByUser(self.hwnd, FromCocoaScreen(self.frame));
}

- (void)windowDidResize:(NSNotification*)n { [self reportFrame]; }
- (void)windowDidMove:(NSNotification*)n { [self reportFrame]; }
- (void)windowDidBecomeKey:(NSNotification*)n
{
	bridge::Activated(self.hwnd, true);
	winport::backend::MenuBarChanged(self.hwnd);
}
- (void)windowDidResignKey:(NSNotification*)n { bridge::Activated(self.hwnd, false); }

@end

// ---------------------------------------------------------------------------------------------
@implementation WPView

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return self.isContentView; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { return YES; }

- (void)drawRect:(NSRect)dirty
{
	CGContextRef ctx = NSGraphicsContext.currentContext.CGContext;
	const auto info = bridge::GetInfo(self.hwnd);
	if (!info.valid) return;
	const BRect c = info.clientInView;
	// 枠（非クライアント）。macOS 14 以降は clipsToBounds が既定で NO なので、自分の範囲に限って塗る
	CGContextSetRGBFillColor(ctx, 0.94, 0.94, 0.94, 1);
	CGContextFillRect(ctx, NSRectToCGRect(NSIntersectionRect(dirty, self.bounds)));
	double scale = 1;
	int w = 0, h = 0;
	CGImageRef img = bridge::SnapshotSurface(self.hwnd, &scale, &w, &h);
	if (getenv("WINPORT_TRACE")) fprintf(stderr, "drawRect %p dirty=%s img=%p %dx%d\n", self.hwnd, NSStringFromRect(dirty).UTF8String, (void*)img, w, h);
	if (img) {
		CGContextSaveGState(ctx);
		CGContextClipToRect(ctx, CGRectMake(c.left, c.top, c.Width(), c.Height()));
		CGContextTranslateCTM(ctx, c.left, c.top + h);
		CGContextScaleCTM(ctx, 1, -1);
		CGContextSetInterpolationQuality(ctx, kCGInterpolationNone);
		CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), img);
		CGContextRestoreGState(ctx);
		CGImageRelease(img);
	}
	if (g_caret.hwnd == self.hwnd && g_marked.length) {
		[self drawMarkedTextAt:c];
	} else if (g_caret.hwnd == self.hwnd && g_caret.visible && g_caret.blinkOn) {
		CGContextSaveGState(ctx);
		CGContextSetBlendMode(ctx, kCGBlendModeDifference);
		CGContextSetRGBFillColor(ctx, 1, 1, 1, 1);
		CGContextFillRect(ctx, CGRectMake(c.left + g_caret.rc.left, c.top + g_caret.rc.top, g_caret.rc.Width(), g_caret.rc.Height()));
		CGContextRestoreGState(ctx);
	}
}

//! IME の未確定文字列をキャレット位置に描く（Windows の IME のコンポジションウィンドウに相当）
- (void)drawMarkedTextAt:(BRect)c
{
	const CGFloat h = std::max(g_caret.rc.Height(), 14);
	NSFont* font = g_imeFont ?: [NSFont systemFontOfSize:h * 0.8];
	NSDictionary* attrs = @{NSFontAttributeName: font, NSForegroundColorAttributeName: NSColor.blackColor};
	NSAttributedString* text = [[NSAttributedString alloc] initWithString:g_marked.string attributes:attrs];
	const NSSize size = text.size;
	CGFloat x = c.left + g_caret.rc.left;
	const CGFloat y = c.top + g_caret.rc.top;
	const CGFloat maxX = c.right - size.width - 2;
	if (x > maxX) x = std::max<CGFloat>(c.left, maxX);
	[[NSColor colorWithRed:1 green:1 blue:0.94 alpha:1] setFill];
	NSRectFill(NSMakeRect(x, y, size.width + 2, h));
	// 変換中の文節は背景で示す
	if (g_markedSelection.length && NSMaxRange(g_markedSelection) <= text.length) {
		const CGFloat x0 = [text attributedSubstringFromRange:NSMakeRange(0, g_markedSelection.location)].size.width;
		const CGFloat w = [text attributedSubstringFromRange:g_markedSelection].size.width;
		[[NSColor colorWithRed:0.75 green:0.85 blue:1 alpha:1] setFill];
		NSRectFill(NSMakeRect(x + x0, y, w, h));
	}
	[text drawAtPoint:NSMakePoint(x, y + (h - size.height) / 2)];
	[NSColor.blackColor setFill];
	NSRectFill(NSMakeRect(x, y + h - 1, size.width, 1));
}

// ---- スクロールバー ----
- (NSScroller*)makeScroller:(BOOL)vertical
{
	NSScroller* s = [[NSScroller alloc] initWithFrame:NSMakeRect(0, 0, vertical ? kScrollBarWidth : 100, vertical ? 100 : kScrollBarWidth)];
	s.scrollerStyle = NSScrollerStyleLegacy;
	s.target = self;
	s.action = @selector(scrollerAction:);
	s.enabled = YES;
	[self addSubview:s];
	return s;
}

- (void)syncScrollBars
{
	const auto info = bridge::GetInfo(self.hwnd);
	if (!info.valid) return;
	const BRect c = info.clientInView;
	for (int bar = 0; bar < 2; ++bar) {
		NSScroller* s = bar ? self.vScroller : self.hScroller;
		if (!info.scrollVisible[bar]) { s.hidden = YES; continue; }
		if (!s) {
			s = [self makeScroller:bar == 1];
			if (bar) self.vScroller = s; else self.hScroller = s;
		}
		s.hidden = NO;
		s.frame = bar ? NSMakeRect(c.right, c.top, kScrollBarWidth, c.Height()) : NSMakeRect(c.left, c.bottom, c.Width(), kScrollBarWidth);
		const double range = info.scrollMax[bar] - info.scrollMin[bar] + 1;
		const double page = info.scrollPage[bar] > 0 ? info.scrollPage[bar] : 1;
		const double movable = std::max(1.0, range - page);
		s.knobProportion = std::clamp(page / std::max(range, 1.0), 0.0, 1.0);
		s.doubleValue = std::clamp((info.scrollPos[bar] - info.scrollMin[bar]) / movable, 0.0, 1.0);
	}
}

- (void)scrollerAction:(NSScroller*)sender
{
	const int bar = sender == self.vScroller ? 1 : 0;
	const auto info = bridge::GetInfo(self.hwnd);
	const double range = info.scrollMax[bar] - info.scrollMin[bar] + 1;
	const double page = info.scrollPage[bar] > 0 ? info.scrollPage[bar] : 1;
	switch (sender.hitPart) {
	case NSScrollerKnob:
	case NSScrollerKnobSlot: {
		const int pos = info.scrollMin[bar] + static_cast<int>(std::lround(sender.doubleValue * std::max(0.0, range - page)));
		bridge::PostScroll(self.hwnd, bar, SB_THUMBTRACK, pos);
		bridge::PostScroll(self.hwnd, bar, SB_THUMBPOSITION, pos);
		break;
	}
	case NSScrollerDecrementPage: bridge::PostScroll(self.hwnd, bar, SB_PAGEUP, 0); break;
	case NSScrollerIncrementPage: bridge::PostScroll(self.hwnd, bar, SB_PAGEDOWN, 0); break;
	default: break;
	}
	bridge::PostScroll(self.hwnd, bar, SB_ENDSCROLL, 0);
}

// ---- マウス ----
- (void)updateTrackingAreas
{
	for (NSTrackingArea* a in self.trackingAreas) [self removeTrackingArea:a];
	[self addTrackingArea:[[NSTrackingArea alloc] initWithRect:NSZeroRect
	                                                   options:NSTrackingMouseMoved | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect
	                                                     owner:self userInfo:nil]];
	[super updateTrackingAreas];
}

- (void)postMouse:(NSEvent*)e kind:(bridge::Mouse)kind button:(int)button
{
	const NSPoint p = [self.window convertPointToScreen:e.locationInWindow];
	const int x = static_cast<int>(std::floor(p.x));
	const int y = static_cast<int>(std::floor(g_primaryHeight - p.y));
	bridge::PostMouse(self.hwnd, kind, button, x, y, ModifiersOf(e.modifierFlags));
}

- (void)mouseDown:(NSEvent*)e { [self postMouse:e kind:(e.clickCount == 2 ? bridge::Mouse::doubleClick : bridge::Mouse::down) button:0]; }
- (void)mouseUp:(NSEvent*)e { [self postMouse:e kind:bridge::Mouse::up button:0]; }
- (void)mouseDragged:(NSEvent*)e { [self postMouse:e kind:bridge::Mouse::move button:0]; }
- (void)mouseMoved:(NSEvent*)e { [self postMouse:e kind:bridge::Mouse::move button:0]; }
- (void)rightMouseDown:(NSEvent*)e { [self postMouse:e kind:(e.clickCount == 2 ? bridge::Mouse::doubleClick : bridge::Mouse::down) button:1]; }
- (void)rightMouseUp:(NSEvent*)e { [self postMouse:e kind:bridge::Mouse::up button:1]; }
- (void)rightMouseDragged:(NSEvent*)e { [self postMouse:e kind:bridge::Mouse::move button:1]; }
- (void)otherMouseDown:(NSEvent*)e { [self postMouse:e kind:bridge::Mouse::down button:2]; }
- (void)otherMouseUp:(NSEvent*)e { [self postMouse:e kind:bridge::Mouse::up button:2]; }
- (void)otherMouseDragged:(NSEvent*)e { [self postMouse:e kind:bridge::Mouse::move button:2]; }

- (void)scrollWheel:(NSEvent*)e
{
	const NSPoint p = [self.window convertPointToScreen:e.locationInWindow];
	const int x = static_cast<int>(std::floor(p.x)), y = static_cast<int>(std::floor(g_primaryHeight - p.y));
	const unsigned mods = ModifiersOf(e.modifierFlags);
	// 1 ノッチ = 120 = 3 行。トラックパッドの画素単位は 1 行 ≒ 16pt として換算する
	const double k = e.hasPreciseScrollingDeltas ? 40.0 / 16.0 : 120.0;
	self.wheelAccumY += e.scrollingDeltaY * k;
	self.wheelAccumX += -e.scrollingDeltaX * k;
	const int dy = static_cast<int>(self.wheelAccumY / 40.0) * 40;
	const int dx = static_cast<int>(self.wheelAccumX / 40.0) * 40;
	if (dy) { self.wheelAccumY -= dy; bridge::PostWheel(self.hwnd, dy, false, x, y, mods); }
	if (dx) { self.wheelAccumX -= dx; bridge::PostWheel(self.hwnd, dx, true, x, y, mods); }
}

// ---- キーボード ----
- (void*)topLevelHwnd
{
	return ((WPWindow*)self.window).hwnd;
}

- (void)keyDown:(NSEvent*)e
{
	// Cmd / Ctrl / Option の組み合わせはコマンドとして sakura へ（未確定文字列があるときは IME へ）
	const NSEventModifierFlags cmdLike = NSEventModifierFlagCommand | NSEventModifierFlagControl | NSEventModifierFlagOption;
	if ((e.modifierFlags & cmdLike) && !g_marked.length) {
		bridge::PostKey([self topLevelHwnd], e.keyCode, true, e.isARepeat, ToU16(e.characters), ModifiersOf(e.modifierFlags));
		return;
	}
	// IME を通す。結果は insertText / setMarkedText / doCommandBySelector のいずれかで届く
	self.currentKey = e;
	[self interpretKeyEvents:@[e]];
	self.currentKey = nil;
}

// ---- NSTextInputClient ----
- (void)redrawMarked
{
	if (WPPlatform* p = g_caret.hwnd ? PlatformOf(g_caret.hwnd) : nil) [p.view setNeedsDisplay:YES];
}

- (void)insertText:(id)string replacementRange:(NSRange)replacementRange
{
	NSString* text = [string isKindOfClass:NSAttributedString.class] ? ((NSAttributedString*)string).string : string;
	const BOOL hadMarked = g_marked.length > 0;
	g_marked = nil;
	if (hadMarked) [self redrawMarked];
	NSEvent* e = self.currentKey;
	if (e && !hadMarked && text.length == 1) {
		// 変換を経ない通常のキー入力。WM_KEYDOWN → WM_CHAR の流れにする
		bridge::PostKey([self topLevelHwnd], e.keyCode, true, e.isARepeat, ToU16(text), ModifiersOf(e.modifierFlags));
	} else if (text.length) {
		bridge::PostImeResult([self topLevelHwnd], ToU16(text));
	}
}

- (void)setMarkedText:(id)string selectedRange:(NSRange)selectedRange replacementRange:(NSRange)replacementRange
{
	g_marked = [string isKindOfClass:NSAttributedString.class] ? string : [[NSAttributedString alloc] initWithString:string];
	g_markedSelection = selectedRange;
	[self redrawMarked];
}

- (void)unmarkText
{
	if (!g_marked.length) return;
	NSString* text = g_marked.string;
	g_marked = nil;
	[self redrawMarked];
	bridge::PostImeResult([self topLevelHwnd], ToU16(text));
}

- (void)doCommandBySelector:(SEL)selector
{
	NSEvent* e = self.currentKey;
	if (e) bridge::PostKey([self topLevelHwnd], e.keyCode, true, e.isARepeat, ToU16(e.characters), ModifiersOf(e.modifierFlags));
}

- (BOOL)hasMarkedText { return g_marked.length > 0; }
- (NSRange)markedRange { return g_marked.length ? NSMakeRange(0, g_marked.length) : NSMakeRange(NSNotFound, 0); }
- (NSRange)selectedRange { return g_marked.length ? g_markedSelection : NSMakeRange(0, 0); }
- (NSArray<NSAttributedStringKey>*)validAttributesForMarkedText { return @[]; }
- (NSUInteger)characterIndexForPoint:(NSPoint)point { return 0; }

- (NSAttributedString*)attributedSubstringForProposedRange:(NSRange)range actualRange:(NSRangePointer)actualRange
{
	if (!g_marked.length || NSMaxRange(range) > g_marked.length) return nil;
	if (actualRange) *actualRange = range;
	return [g_marked attributedSubstringFromRange:range];
}

//! 変換候補ウィンドウの位置。Win32 スレッドを待たないよう、表示系が持つキャレット位置で答える
- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actualRange
{
	if (actualRange) *actualRange = range;
	WPPlatform* p = g_caret.hwnd ? PlatformOf(g_caret.hwnd) : nil;
	if (!p) return [self.window convertRectToScreen:self.frame];
	const auto info = bridge::GetInfo(g_caret.hwnd);
	const NSRect r = NSMakeRect(info.clientInView.left + g_caret.rc.left, info.clientInView.top + g_caret.rc.top, 1, g_caret.rc.Height());
	return [p.view.window convertRectToScreen:[p.view convertRect:r toView:nil]];
}

- (void)keyUp:(NSEvent*)e
{
	bridge::PostKey([self topLevelHwnd], e.keyCode, false, false, {}, ModifiersOf(e.modifierFlags));
}

- (void)flagsChanged:(NSEvent*)e
{
	bridge::ModifiersChanged([self topLevelHwnd], ModifiersOf(e.modifierFlags));
}

- (BOOL)performKeyEquivalent:(NSEvent*)e
{
	// Cmd+Q（終了）以外の Cmd 組み合わせはメニューに渡さず、Ctrl として sakura に送る
	if ((e.modifierFlags & NSEventModifierFlagCommand) && self.isContentView && self.window.isKeyWindow) {
		NSString* ch = e.charactersIgnoringModifiers.lowercaseString;
		if ([ch isEqualToString:@"q"]) return NO;
		[self keyDown:e];
		return YES;
	}
	return NO;
}

@end

// ---------------------------------------------------------------------------------------------
// メニュー: HMENU の内容を読んで NSMenu を組み立てる
@interface WPMenu : NSMenu <NSMenuDelegate>
@property(nonatomic) void* hwnd;     //!< WM_COMMAND の送り先（ポップアップでは nullptr）
@property(nonatomic) void* hmenu;
@property(nonatomic) int position;   //!< メニューバー上の位置（-1 ならサブメニュー）
@end

@interface WPMenuTarget : NSObject
@property(nonatomic) void* hwnd;
@property(nonatomic) int selected;
- (void)itemChosen:(NSMenuItem*)item;
@end

@implementation WPMenuTarget
- (void)itemChosen:(NSMenuItem*)item
{
	self.selected = static_cast<int>(item.tag);
	if (self.hwnd) bridge::PostMenuCommand(self.hwnd, static_cast<unsigned>(item.tag));
}
@end

namespace {

WPMenuTarget* g_menuBarTarget = nil;

//! "開く(&O)..." → "開く..."。& はニーモニック（&& は & 1 文字）
NSString* MenuTitle(std::u16string text)
{
	const size_t paren = text.find(u"(&");
	if (paren != std::u16string::npos && paren + 3 < text.size() && text[paren + 3] == u')') text.erase(paren, 4);
	std::u16string out;
	for (size_t i = 0; i < text.size(); ++i) {
		if (text[i] == u'&') {
			if (i + 1 < text.size() && text[i + 1] == u'&') { out += u'&'; ++i; }
			continue;
		}
		out += text[i];
	}
	return [NSString stringWithCharacters:reinterpret_cast<const unichar*>(out.data()) length:out.size()];
}

//! "Ctrl+Shift+S" のようなショートカット表記を mac の表示にする（Ctrl は Cmd に読み替える）。
//! キー入力は WPWindow.sendEvent が先に受けるので、ここで設定した値は表示にだけ使われる
void ApplyShortcut(NSMenuItem* item, NSString* keys)
{
	NSEventModifierFlags mods = 0;
	NSArray<NSString*>* parts = [keys componentsSeparatedByString:@"+"];
	for (NSUInteger i = 0; i + 1 < parts.count; ++i) {
		if ([parts[i] isEqualToString:@"Ctrl"]) mods |= NSEventModifierFlagCommand;
		else if ([parts[i] isEqualToString:@"Shift"]) mods |= NSEventModifierFlagShift;
		else if ([parts[i] isEqualToString:@"Alt"]) mods |= NSEventModifierFlagOption;
		else return;
	}
	NSString* key = parts.lastObject;
	NSString* equivalent = nil;
	if (key.length == 1) equivalent = key.lowercaseString;
	else if ([key hasPrefix:@"F"] && key.length <= 3 && key.integerValue == 0) {
		const int n = [key substringFromIndex:1].intValue;
		if (n >= 1 && n <= 12) { const unichar c = static_cast<unichar>(NSF1FunctionKey + n - 1); equivalent = [NSString stringWithCharacters:&c length:1]; }
	}
	if (!equivalent) return;
	item.keyEquivalent = equivalent;
	item.keyEquivalentModifierMask = mods;
}

void FillMenu(WPMenu* menu, WPMenuTarget* target)
{
	[menu removeAllItems];
	menu.autoenablesItems = NO;
	for (const auto& it : bridge::GetMenuItems(menu.hmenu)) {
		if (it.separator) { [menu addItem:NSMenuItem.separatorItem]; continue; }
		std::u16string label = it.text, keys;
		if (const auto tab = label.find(u'\t'); tab != std::u16string::npos) { keys = label.substr(tab + 1); label.resize(tab); }
		NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:MenuTitle(label) action:nil keyEquivalent:@""];
		item.enabled = !it.disabled;
		item.state = it.checked ? NSControlStateValueOn : NSControlStateValueOff;
		if (!keys.empty()) ApplyShortcut(item, [NSString stringWithCharacters:reinterpret_cast<const unichar*>(keys.data()) length:keys.size()]);
		if (it.sub) {
			WPMenu* sub = [[WPMenu alloc] initWithTitle:item.title];
			sub.hwnd = menu.hwnd;
			sub.hmenu = it.sub;
			sub.position = -1;
			FillMenu(sub, target);
			item.submenu = sub;
		} else {
			item.tag = it.id;
			item.target = target;
			item.action = @selector(itemChosen:);
		}
		[menu addItem:item];
	}
}

void RebuildMenuBar(void* hwnd)
{
	void* hmenu = bridge::MenuOfWindow(hwnd);
	if (!hmenu) return;  // ダイアログにはメニューが無いので、所有者のメニューのまま
	NSMenu* bar = NSApp.mainMenu;
	while (bar.numberOfItems > 1) [bar removeItemAtIndex:1];  // 先頭はアプリケーションメニュー
	if (!g_menuBarTarget) g_menuBarTarget = [WPMenuTarget new];
	g_menuBarTarget.hwnd = hwnd;
	int pos = 0;
	for (const auto& it : bridge::GetMenuItems(hmenu)) {
		NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:MenuTitle(it.text) action:nil keyEquivalent:@""];
		if (it.sub) {
			WPMenu* sub = [[WPMenu alloc] initWithTitle:item.title];
			sub.hwnd = hwnd;
			sub.hmenu = it.sub;
			sub.position = pos;
			sub.delegate = sub;  // 開く直前に内容を作る
			item.submenu = sub;
		}
		[bar addItem:item];
		++pos;
	}
}

}  // namespace

@implementation WPMenu
- (void)menuNeedsUpdate:(NSMenu*)menu
{
	bridge::InitMenuPopup(self.hwnd, self.hmenu, self.position, 500);
	FillMenu(self, g_menuBarTarget);
}
@end

@interface WPAppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation WPAppDelegate
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender
{
	// 保存確認があるので、ここでは終了せずウィンドウを閉じていく。全部閉じると backend::Quit が呼ばれる
	if (g_quitting) return NSTerminateNow;
	bridge::CloseAllWindows();
	return NSTerminateCancel;
}
@end

namespace winport::backend {

void Initialize()
{
	g_available = true;
	[NSApplication sharedApplication];
	NSApp.activationPolicy = NSApplicationActivationPolicyRegular;
	static WPAppDelegate* delegate = [WPAppDelegate new];
	NSApp.delegate = delegate;

	NSMenu* bar = [NSMenu new];
	NSMenuItem* appItem = [NSMenuItem new];
	[bar addItem:appItem];
	NSMenu* appMenu = [NSMenu new];
	[appMenu addItemWithTitle:@"サクラエディタを終了" action:@selector(terminate:) keyEquivalent:@"q"];
	appItem.submenu = appMenu;
	NSApp.mainMenu = bar;

	NSScreen* primary = NSScreen.screens.firstObject;
	g_primaryHeight = primary.frame.size.height;
	g_scale = primary.backingScaleFactor;
	g_screen = FromCocoaScreen(primary.frame);
	g_work = FromCocoaScreen(primary.visibleFrame);
	const NSRect fr = [NSWindow frameRectForContentRect:NSMakeRect(0, 0, 100, 100) styleMask:NSWindowStyleMaskTitled];
	g_titleBar = static_cast<int>(fr.size.height - 100);

	if (getenv("WINPORT_DUMP")) {
		[NSTimer scheduledTimerWithTimeInterval:5 repeats:NO block:^(NSTimer*) {
			bridge::DumpWindows();
			std::function<void(NSView*, int)> dumpView = [&](NSView* v, int depth) {
				fprintf(stderr, "%*sview %s hwnd=%p frame=%s hidden=%d\n", depth * 2, "", v.className.UTF8String,
				        [v isKindOfClass:WPView.class] ? ((WPView*)v).hwnd : nullptr, NSStringFromRect(v.frame).UTF8String, v.hidden);
				for (NSView* c in v.subviews) dumpView(c, depth + 1);
			};
			for (NSWindow* w in NSApp.windows) dumpView(w.contentView, 0);
		}];
	}
	[NSTimer scheduledTimerWithTimeInterval:0.53 repeats:YES block:^(NSTimer*) {
		g_caret.blinkOn = !g_caret.blinkOn;
		if (WPPlatform* p = g_caret.hwnd ? PlatformOf(g_caret.hwnd) : nil) {
			const auto info = bridge::GetInfo(g_caret.hwnd);
			const BRect c = info.clientInView;
			[p.view setNeedsDisplayInRect:NSMakeRect(c.left + g_caret.rc.left - 1, c.top + g_caret.rc.top - 1, g_caret.rc.Width() + 2, g_caret.rc.Height() + 2)];
		}
	}];
}

void Run()
{
	[NSApp activateIgnoringOtherApps:YES];
	[NSApp run];
}

void Quit()
{
	OnMain(^{
		g_quitting = true;
		[NSApp terminate:nil];
	});
}

void WindowVisibilityChanged(void* hwnd)
{
	OnMain(^{
		const auto info = bridge::GetInfo(hwnd);
		if (!info.valid) return;
		if (info.topLevel) {
			if (info.selfVisible) {
				WPPlatform* p = Realize(hwnd);
				[p.window orderFront:nil];
				// 所有されたウィンドウ（ダイアログ）は所有者の前に保つ
				if (WPPlatform* owner = info.owner ? PlatformOf(info.owner) : nil) {
					if (owner.window.visible) [owner.window addChildWindow:p.window ordered:NSWindowAbove];
				}
			} else if (WPPlatform* p = PlatformOf(hwnd)) {
				[p.window.parentWindow removeChildWindow:p.window];
				[p.window orderOut:nil];
			}
		} else if (WPPlatform* p = Realize(hwnd)) {
			p.view.hidden = !info.selfVisible;
		}
	});
}

void WindowDestroyed(void* hwnd, void* platform)
{
	if (!platform) return;
	OnMain(^{
		WPPlatform* p = (__bridge_transfer WPPlatform*)platform;
		if (g_caret.hwnd == hwnd) g_caret = CaretState{};
		[p.view removeFromSuperview];
		if (p.window) {
			p.window.delegate = nil;
			[p.window orderOut:nil];
			[p.window close];
		}
	});
}

void FrameChanged(void* hwnd) { OnMain(^{ ApplyFrame(hwnd); }); }

void TextChanged(void* hwnd)
{
	OnMain(^{
		const auto info = bridge::GetInfo(hwnd);
		if (WPPlatform* p = info.platform ? (__bridge WPPlatform*)info.platform : nil) {
			if (p.window) p.window.title = [NSString stringWithCharacters:reinterpret_cast<const unichar*>(info.title.data()) length:info.title.size()];
		}
	});
}

void Present(void* hwnd, const BRect& dirty)
{
	const BRect d = dirty;
	OnMain(^{
		const auto info = bridge::GetInfo(hwnd);
		if (getenv("WINPORT_TRACE")) fprintf(stderr, "Present %p (%d,%d)-(%d,%d) platform=%p\n", hwnd, d.left, d.top, d.right, d.bottom, info.platform);
		if (WPPlatform* p = info.platform ? (__bridge WPPlatform*)info.platform : nil) {
			const BRect c = info.clientInView;
			[p.view setNeedsDisplayInRect:NSMakeRect(c.left + d.left, c.top + d.top, d.Width(), d.Height())];
		}
	});
}

void Activate(void* hwnd)
{
	OnMain(^{
		const auto info = bridge::GetInfo(hwnd);
		if (!info.valid || !info.topLevel || !info.selfVisible) return;
		if (WPPlatform* p = Realize(hwnd)) {
			[NSApp activateIgnoringOtherApps:YES];
			[p.window makeKeyAndOrderFront:nil];
		}
	});
}

void CaretChanged(void* hwnd, const BRect& rc, bool visible)
{
	const BRect r = rc;
	OnMain(^{
		void* old = g_caret.hwnd;
		const BRect oldRc = g_caret.rc;
		g_caret.hwnd = hwnd;
		g_caret.rc = r;
		g_caret.visible = visible;
		g_caret.blinkOn = true;
		auto invalidate = [](void* h, const BRect& x) {
			if (!h) return;
			const auto info = bridge::GetInfo(h);
			if (WPPlatform* p = info.platform ? (__bridge WPPlatform*)info.platform : nil) {
				const BRect c = info.clientInView;
				[p.view setNeedsDisplayInRect:NSMakeRect(c.left + x.left - 1, c.top + x.top - 1, x.Width() + 2, x.Height() + 2)];
			}
		};
		invalidate(old, oldRc);
		invalidate(hwnd, r);
	});
}

void SetCursorShape(uintptr_t cursorId) { OnMain(^{ [CursorFor(cursorId) set]; }); }

void ScrollBarsChanged(void* hwnd)
{
	OnMain(^{
		if (WPPlatform* p = PlatformOf(hwnd)) [p.view syncScrollBars];
	});
}

void Beep() { OnMain(^{ NSBeep(); }); }

bool Available() { return g_available; }

long ClipboardChangeCount() { return NSPasteboard.generalPasteboard.changeCount; }

void ClipboardWriteText(const std::u16string& text)
{
	@autoreleasepool {
		NSPasteboard* pb = NSPasteboard.generalPasteboard;
		[pb clearContents];
		if (text.empty()) return;
		[pb setString:[NSString stringWithCharacters:reinterpret_cast<const unichar*>(text.data()) length:text.size()] forType:NSPasteboardTypeString];
	}
}

bool ClipboardReadText(std::u16string* text)
{
	@autoreleasepool {
		NSString* s = [NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString];
		if (!s) return false;
		text->resize(s.length);
		[s getCharacters:reinterpret_cast<unichar*>(text->data()) range:NSMakeRange(0, s.length)];
		return true;
	}
}

void MenuBarChanged(void* hwnd)
{
	OnMain(^{
		const auto info = bridge::GetInfo(hwnd);
		if (!info.platform) return;
		WPPlatform* p = (__bridge WPPlatform*)info.platform;
		if (p.window.isKeyWindow || !NSApp.keyWindow) RebuildMenuBar(hwnd);
	});
}

void ShowPopupMenu(void* hmenu, int x, int y, std::atomic<int>* result)
{
	// ShowMessageBox と同じ理由でランループへ積む
	CFRunLoopPerformBlock(CFRunLoopGetMain(), kCFRunLoopCommonModes, ^{
		WPMenuTarget* target = [WPMenuTarget new];
		target.selected = -1;
		WPMenu* menu = [[WPMenu alloc] initWithTitle:@""];
		menu.hmenu = hmenu;
		menu.position = -1;
		FillMenu(menu, target);
		[menu popUpMenuPositioningItem:nil atLocation:NSMakePoint(x, g_primaryHeight - y) inView:nil];
		result->store(target.selected);
	});
	CFRunLoopWakeUp(CFRunLoopGetMain());
}

void SetImeFont(const char* postscriptName, double size)
{
	NSString* name = @(postscriptName);
	OnMain(^{ g_imeFont = [NSFont fontWithName:name size:size]; });
}

void ShowMessageBox(const std::u16string& text, const std::u16string& caption, unsigned type, std::atomic<int>* result)
{
	NSString* message = [NSString stringWithCharacters:reinterpret_cast<const unichar*>(text.data()) length:text.size()];
	NSString* title = [NSString stringWithCharacters:reinterpret_cast<const unichar*>(caption.data()) length:caption.size()];
	// main queue のブロック内で runModal すると、閉じるまで他の OnMain が止まる（main queue は直列）。
	// 描画を止めないよう、ランループに直接積む
	CFRunLoopPerformBlock(CFRunLoopGetMain(), kCFRunLoopCommonModes, ^{
		struct Button { NSString* label; int id; NSString* key; };
		constexpr int IDOK = 1, IDCANCEL = 2, IDABORT = 3, IDRETRY = 4, IDIGNORE = 5, IDYES = 6, IDNO = 7;
		std::vector<Button> buttons;
		switch (type & 0xF) {
		case 1: buttons = {{@"OK", IDOK, @"\r"}, {@"キャンセル", IDCANCEL, @"\033"}}; break;
		case 2: buttons = {{@"中止", IDABORT, @"a"}, {@"再試行", IDRETRY, @"r"}, {@"無視", IDIGNORE, @"i"}}; break;
		case 3: buttons = {{@"はい", IDYES, @"y"}, {@"いいえ", IDNO, @"n"}, {@"キャンセル", IDCANCEL, @"\033"}}; break;
		case 4: buttons = {{@"はい", IDYES, @"y"}, {@"いいえ", IDNO, @"n"}}; break;
		case 5: buttons = {{@"再試行", IDRETRY, @"r"}, {@"キャンセル", IDCANCEL, @"\033"}}; break;
		default: buttons = {{@"OK", IDOK, @"\r"}}; break;
		}
		NSAlert* alert = [NSAlert new];
		alert.messageText = title;
		alert.informativeText = message;
		switch (type & 0xF0) {
		case 0x10: alert.alertStyle = NSAlertStyleCritical; break;   // MB_ICONSTOP
		case 0x30: alert.alertStyle = NSAlertStyleWarning; break;    // MB_ICONEXCLAMATION
		default: alert.alertStyle = NSAlertStyleInformational; break;
		}
		const size_t def = std::min<size_t>((type >> 8) & 0xF, buttons.size() - 1);  // MB_DEFBUTTON2/3
		for (size_t i = 0; i < buttons.size(); ++i) {
			NSButton* b = [alert addButtonWithTitle:buttons[i].label];
			b.keyEquivalent = i == def ? @"\r" : buttons[i].key;
		}
		[NSApp activateIgnoringOtherApps:YES];
		const NSInteger r = [alert runModal] - NSAlertFirstButtonReturn;
		result->store(buttons[std::clamp<NSInteger>(r, 0, buttons.size() - 1)].id);
	});
	CFRunLoopWakeUp(CFRunLoopGetMain());
}

double ScaleFactor() { return g_scale; }
BRect ScreenRect() { return g_screen; }
BRect WorkArea() { return g_work; }
int TitleBarHeight() { return g_titleBar; }

void CursorPos(int* x, int* y)
{
	const NSPoint p = [NSEvent mouseLocation];
	*x = static_cast<int>(std::floor(p.x));
	*y = static_cast<int>(std::floor(g_primaryHeight - p.y));
}

}  // namespace winport::backend
