/*
	test-winport-print.cpp — マッピングモードとプリンター DC のテスト（mac 版独自）

	SPDX-License-Identifier: Zlib
*/
#include "pch.h"

#include <commdlg.h>
#include <winspool.h>

#pragma push_macro("interface")
#undef interface
#include <CoreGraphics/CoreGraphics.h>
#pragma pop_macro("interface")

#include <cstdio>
#include <string>

namespace {

DEVMODEW A4(short orientation)
{
	DEVMODEW dm{};
	dm.dmSize = sizeof(dm);
	dm.dmPaperSize = DMPAPER_A4;
	dm.dmOrientation = orientation;
	return dm;
}

//! PDF のページ数と 1 ページ目の大きさ（ポイント）
int ReadPdf(const std::string& path, CGRect* box)
{
	CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(path.c_str()), static_cast<CFIndex>(path.size()), false);
	CGPDFDocumentRef doc = CGPDFDocumentCreateWithURL(url);
	CFRelease(url);
	if (!doc) return -1;
	const int pages = static_cast<int>(CGPDFDocumentGetNumberOfPages(doc));
	if (pages > 0) *box = CGPDFPageGetBoxRect(CGPDFDocumentGetPage(doc, 1), kCGPDFMediaBox);
	CGPDFDocumentRelease(doc);
	return pages;
}

}  // namespace

TEST(WinportMapMode, LometricOnPrinter)
{
	const DEVMODEW dm = A4(DMORIENT_PORTRAIT);
	HDC hdc = ::CreateDCW(L"winspool", L"PDF", nullptr, &dm);
	ASSERT_NE(nullptr, hdc);
	EXPECT_EQ(MM_TEXT, ::SetMapMode(hdc, MM_LOMETRIC));
	EXPECT_EQ(MM_LOMETRIC, ::GetMapMode(hdc));

	// 上流の GetPrintMetrics と同じ計算で用紙の大きさ（0.1 mm）が戻る
	POINT size{::GetDeviceCaps(hdc, HORZRES), ::GetDeviceCaps(hdc, VERTRES)};
	::DPtoLP(hdc, &size, 1);
	EXPECT_EQ(2100, std::abs(size.x));
	EXPECT_EQ(2970, std::abs(size.y));
	EXPECT_LT(size.y, 0);	// y は上向き

	POINT p{254, -254};		// 1 インチ
	::LPtoDP(hdc, &p, 1);
	EXPECT_EQ(600, p.x);
	EXPECT_EQ(600, p.y);
	::DeleteDC(hdc);
}

TEST(WinportMapMode, AnisotropicKeepsRatioAndAcceptsZoom)
{
	HDC hdc = ::CreateCompatibleDC(nullptr);
	::SetMapMode(hdc, MM_LOMETRIC);
	::SetMapMode(hdc, MM_ANISOTROPIC);
	SIZE window{};
	::GetWindowExtEx(hdc, &window);
	POINT before{1000, -1000};
	::LPtoDP(hdc, &before, 1);
	EXPECT_TRUE(::SetWindowExtEx(hdc, window.cx / 2, window.cy / 2, nullptr));	// 200%
	POINT after{1000, -1000};
	::LPtoDP(hdc, &after, 1);
	EXPECT_EQ(before.x * 2, after.x);
	EXPECT_EQ(before.y * 2, after.y);

	// MM_TEXT では比率を変えられない
	::SetMapMode(hdc, MM_TEXT);
	::SetWindowExtEx(hdc, 10, 10, nullptr);
	POINT plain{5, 7};
	::LPtoDP(hdc, &plain, 1);
	EXPECT_EQ(5, plain.x);
	EXPECT_EQ(7, plain.y);
	::DeleteDC(hdc);
}

TEST(WinportMapMode, SaveDCRestoresMapping)
{
	HDC hdc = ::CreateCompatibleDC(nullptr);
	const int saved = ::SaveDC(hdc);
	::SetMapMode(hdc, MM_LOMETRIC);
	::RestoreDC(hdc, saved);
	EXPECT_EQ(MM_TEXT, ::GetMapMode(hdc));
	::DeleteDC(hdc);
}

TEST(WinportPrint, DefaultPrinterHandlesAreUsable)
{
	PRINTDLGW pd{};
	pd.lStructSize = sizeof(pd);
	pd.Flags = PD_RETURNDEFAULT;
	ASSERT_TRUE(::PrintDlgW(&pd));
	auto names = static_cast<DEVNAMES*>(::GlobalLock(pd.hDevNames));
	std::wstring device = reinterpret_cast<wchar_t*>(names) + names->wDeviceOffset;
	EXPECT_FALSE(device.empty());
	::GlobalUnlock(pd.hDevNames);
	auto dm = static_cast<DEVMODEW*>(::GlobalLock(pd.hDevMode));
	EXPECT_EQ(DMPAPER_A4, dm->dmPaperSize);
	EXPECT_EQ(2970, dm->dmPaperLength);
	::GlobalUnlock(pd.hDevMode);
	HANDLE printer = nullptr;
	EXPECT_TRUE(::OpenPrinterW(device.data(), &printer, nullptr));
	EXPECT_TRUE(::ClosePrinter(printer));
	wchar_t unknown[] = L"no such printer";
	EXPECT_FALSE(::OpenPrinterW(unknown, &printer, nullptr));
	::GlobalFree(pd.hDevMode);
	::GlobalFree(pd.hDevNames);
}

TEST(WinportPrint, DocumentPropertiesFollowsPaperKind)
{
	DEVMODEW dm = A4(DMORIENT_PORTRAIT);
	dm.dmPaperSize = DMPAPER_B5;
	dm.dmPaperWidth = 1;	// 前の用紙の値が残っていても種類を優先する
	dm.dmPaperLength = 1;
	wchar_t name[] = L"PDF";
	EXPECT_EQ(IDOK, ::DocumentPropertiesW(nullptr, nullptr, name, &dm, &dm, DM_IN_BUFFER | DM_OUT_BUFFER));
	EXPECT_EQ(1820, dm.dmPaperWidth);
	EXPECT_EQ(2570, dm.dmPaperLength);
}

TEST(WinportPrint, WritesPagesToPdf)
{
	const std::string path = "/tmp/winport-print-test.pdf";
	const std::wstring wide(path.begin(), path.end());
	const DEVMODEW dm = A4(DMORIENT_LANDSCAPE);
	HDC hdc = ::CreateDCW(L"winspool", L"PDF", nullptr, &dm);
	ASSERT_NE(nullptr, hdc);
	::SetMapMode(hdc, MM_LOMETRIC);
	DOCINFOW doc{};
	doc.cbSize = sizeof(doc);
	doc.lpszDocName = L"test";
	doc.lpszOutput = wide.c_str();
	ASSERT_GT(::StartDocW(hdc, &doc), 0);
	for (int page = 0; page < 3; ++page) {
		EXPECT_GT(::StartPage(hdc), 0);
		EXPECT_TRUE(::TextOutW(hdc, 100, -100, L"印刷テスト", 5));
		EXPECT_TRUE(::Rectangle(hdc, 100, -300, 500, -600));
		EXPECT_GT(::EndPage(hdc), 0);
	}
	EXPECT_GT(::EndDoc(hdc), 0);
	::DeleteDC(hdc);

	CGRect box{};
	EXPECT_EQ(3, ReadPdf(path, &box));
	EXPECT_NEAR(841.9, box.size.width, 0.5);	// A4 横
	EXPECT_NEAR(595.3, box.size.height, 0.5);
	std::remove(path.c_str());
}

TEST(WinportPrint, AbortDiscardsJob)
{
	const std::string path = "/tmp/winport-print-abort.pdf";
	const std::wstring wide(path.begin(), path.end());
	HDC hdc = ::CreateDCW(L"winspool", L"PDF", nullptr, nullptr);
	DOCINFOW doc{};
	doc.cbSize = sizeof(doc);
	doc.lpszOutput = wide.c_str();
	ASSERT_GT(::StartDocW(hdc, &doc), 0);
	::StartPage(hdc);
	EXPECT_GT(::AbortDoc(hdc), 0);
	EXPECT_LE(::EndDoc(hdc), 0);	// 二重に終わらない
	::DeleteDC(hdc);
	std::remove(path.c_str());
}
