/*
	print.cpp — 印刷（プリンター DC、PrintDlg、スプーラー）

	プリンター DC は 600 dpi の仮想デバイスで、描画は CGPDFContext へ書く。上流の描画処理は MM_LOMETRIC の座標で
	そのまま動く。EndDoc で PDF を標準の印刷パネルへ渡す。ページ範囲・部数・PDF として保存は印刷パネルに任せるので、
	PrintDlgW は（プリンターの設定を除いて）画面を出さない。

	環境変数 WINPORT_PRINT_TO_FILE を指定すると、パネルを出さずにその PDF へ書く（プリンターへ送らずに確認するため）。

	SPDX-License-Identifier: Zlib
*/
#include "gdi_internal.h"

#include <commdlg.h>
#include <winspool.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

#include <unistd.h>

using namespace winport;
using namespace winport::gdi;

namespace {

std::u16string U16(const std::wstring& s) { return std::u16string(s.begin(), s.end()); }
std::wstring W(const std::u16string& s) { return std::wstring(s.begin(), s.end()); }

const wchar_t kPdfDevice[] = L"PDF";	//!< プリンターが 1 台も無くてもプレビューと PDF 保存はできる
const wchar_t kDriver[] = L"winspool";
const wchar_t kPort[] = L"";

struct Paper {
	short id;
	short width, length;	//!< 0.1 mm
};

constexpr Paper kPapers[] = {
	{DMPAPER_A3, 2970, 4200}, {DMPAPER_A4, 2100, 2970}, {DMPAPER_A4SMALL, 2100, 2970}, {DMPAPER_A5, 1480, 2100},
	{DMPAPER_B4, 2500, 3540}, {DMPAPER_B5, 1820, 2570}, {DMPAPER_LETTER, 2159, 2794}, {DMPAPER_LETTERSMALL, 2159, 2794},
	{DMPAPER_LEGAL, 2159, 3556}, {DMPAPER_TABLOID, 2794, 4318}, {DMPAPER_LEDGER, 4318, 2794}, {DMPAPER_STATEMENT, 1397, 2159},
	{DMPAPER_EXECUTIVE, 1841, 2667}, {DMPAPER_FOLIO, 2159, 3302}, {DMPAPER_QUARTO, 2150, 2750},
};

//! ドライバーと同じく、用紙の種類から幅と長さを埋める。表に無い種類は指定された値のまま
void NormalizePaper(DEVMODEW* dm)
{
	for (const Paper& p : kPapers) {
		if (p.id != dm->dmPaperSize) continue;
		dm->dmPaperWidth = p.width;
		dm->dmPaperLength = p.length;
		break;
	}
	dm->dmFields |= DM_ORIENTATION | DM_PAPERSIZE | DM_PAPERLENGTH | DM_PAPERWIDTH;
}

HGLOBAL NewDevMode()
{
	HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DEVMODEW));
	auto dm = static_cast<DEVMODEW*>(GlobalLock(h));
	dm->dmSize = sizeof(DEVMODEW);
	dm->dmOrientation = DMORIENT_PORTRAIT;
	dm->dmPaperSize = DMPAPER_A4;
	dm->dmCopies = 1;
	NormalizePaper(dm);
	GlobalUnlock(h);
	return h;
}

HGLOBAL NewDevNames(const std::wstring& device, bool isDefault)
{
	const size_t driverLength = std::size(kDriver), deviceLength = device.size() + 1, portLength = std::size(kPort);
	const size_t header = (sizeof(DEVNAMES) + sizeof(wchar_t) - 1) / sizeof(wchar_t);
	HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, (header + driverLength + deviceLength + portLength) * sizeof(wchar_t));
	auto names = static_cast<DEVNAMES*>(GlobalLock(h));
	names->wDriverOffset = static_cast<WORD>(header);
	names->wDeviceOffset = static_cast<WORD>(header + driverLength);
	names->wOutputOffset = static_cast<WORD>(header + driverLength + deviceLength);
	names->wDefault = isDefault ? DN_DEFAULTPRN : 0;
	auto text = reinterpret_cast<wchar_t*>(names);
	std::wmemcpy(text + names->wDriverOffset, kDriver, driverLength);
	std::wmemcpy(text + names->wDeviceOffset, device.c_str(), deviceLength);
	std::wmemcpy(text + names->wOutputOffset, kPort, portLength);
	GlobalUnlock(h);
	return h;
}

std::vector<std::wstring> KnownPrinters()
{
	std::vector<std::wstring> names;
	if (backend::Available()) {
		for (const auto& n : backend::PrinterNames()) names.push_back(W(n));
	}
	if (names.empty()) names.emplace_back(kPdfDevice);
	return names;
}

//! パネルの間も描画だけは進める（commdlg.cpp の WaitModal と同じ）
template <typename Done>
void WaitPanel(Done done)
{
	while (!done()) {
		MSG m;
		while (PeekMessageW(&m, nullptr, WM_PAINT, WM_PAINT, PM_REMOVE)) DispatchMessageW(&m);
		Sleep(10);
	}
}

bool ChoosePrinter(LPPRINTDLGW pd)
{
	const std::vector<std::wstring> known = KnownPrinters();
	std::vector<std::u16string> names;
	for (const auto& n : known) names.push_back(U16(n));
	std::u16string name = names.front();
	if (pd->hDevNames) {
		auto dn = static_cast<DEVNAMES*>(GlobalLock(pd->hDevNames));
		name = U16(reinterpret_cast<wchar_t*>(dn) + dn->wDeviceOffset);
		GlobalUnlock(pd->hDevNames);
	}
	std::atomic<int> result{0};
	backend::ShowPrinterChooser(&names, &name, &result);
	WaitPanel([&] { return result.load() != 0; });
	if (result.load() != 1) return false;
	if (pd->hDevNames) GlobalFree(pd->hDevNames);
	pd->hDevNames = NewDevNames(W(name), name == names.front());
	return true;
}

std::wstring TemporaryPdf()
{
	char path[] = "/tmp/sakura-print-XXXXXX";
	const int fd = mkstemp(path);
	if (fd >= 0) close(fd);
	const std::string text = path;
	return std::wstring(text.begin(), text.end());
}

std::string Utf8(const std::wstring& s)
{
	std::string out;
	for (wchar_t c : s) {
		if (c < 0x80) out += static_cast<char>(c);
		else if (c < 0x800) { out += static_cast<char>(0xC0 | (c >> 6)); out += static_cast<char>(0x80 | (c & 63)); }
		else { out += static_cast<char>(0xE0 | (c >> 12)); out += static_cast<char>(0x80 | ((c >> 6) & 63)); out += static_cast<char>(0x80 | (c & 63)); }
	}
	return out;
}

double PageWidthPoints(const DC* dc) { return dc->pageWidth * 72.0 / kPrinterDpi; }
double PageHeightPoints(const DC* dc) { return dc->pageHeight * 72.0 / kPrinterDpi; }

}  // namespace

extern "C" {

HDC WINAPI CreateDCW(LPCWSTR, LPCWSTR device, LPCWSTR, const DEVMODEW* devMode)
{
	HDC hdc = CreateCompatibleDC(nullptr);
	auto dc = DCOf(hdc);
	dc->printer = true;
	dc->printerName = device ? device : L"";
	DEVMODEW dm{};
	dm.dmPaperSize = DMPAPER_A4;
	dm.dmOrientation = DMORIENT_PORTRAIT;
	if (devMode) {
		dm.dmPaperSize = devMode->dmPaperSize;
		dm.dmPaperWidth = devMode->dmPaperWidth;
		dm.dmPaperLength = devMode->dmPaperLength;
		dm.dmOrientation = devMode->dmOrientation;
	}
	NormalizePaper(&dm);
	if (dm.dmPaperWidth <= 0 || dm.dmPaperLength <= 0) { dm.dmPaperWidth = 2100; dm.dmPaperLength = 2970; }
	int width = dm.dmPaperWidth, length = dm.dmPaperLength;
	if (dm.dmOrientation == DMORIENT_LANDSCAPE) std::swap(width, length);
	dc->pageWidth = static_cast<int>(std::lround(width * kPrinterDpi / 254.0));
	dc->pageHeight = static_cast<int>(std::lround(length * kPrinterDpi / 254.0));
	return hdc;
}

int WINAPI StartDocW(HDC hdc, const DOCINFOW* info)
{
	auto dc = DCOf(hdc);
	if (!dc || !dc->printer || dc->pdf) return SP_ERROR;
	dc->jobName = info && info->lpszDocName ? info->lpszDocName : L"";
	dc->outputPath = info && info->lpszOutput ? info->lpszOutput : L"";
	if (dc->outputPath.empty()) {
		if (const char* file = std::getenv("WINPORT_PRINT_TO_FILE")) dc->outputPath.assign(file, file + std::strlen(file));
	}
	dc->pdfPath = dc->outputPath.empty() ? TemporaryPdf() : dc->outputPath;
	const std::string path = Utf8(dc->pdfPath);
	CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(path.c_str()), static_cast<CFIndex>(path.size()), false);
	const CGRect box = CGRectMake(0, 0, PageWidthPoints(dc), PageHeightPoints(dc));
	dc->pdf = url ? CGPDFContextCreateWithURL(url, &box, nullptr) : nullptr;
	if (url) CFRelease(url);
	return dc->pdf ? 1 : SP_ERROR;
}

int WINAPI StartPage(HDC hdc)
{
	auto dc = DCOf(hdc);
	if (!dc || !dc->pdf || dc->inPage) return 0;
	CGPDFContextBeginPage(dc->pdf, nullptr);
	CGContextSaveGState(dc->pdf);
	// デバイス座標（600 dpi、y 下向き）から PDF のポイントへ
	CGContextTranslateCTM(dc->pdf, 0, PageHeightPoints(dc));
	CGContextScaleCTM(dc->pdf, 72.0 / kPrinterDpi, -72.0 / kPrinterDpi);
	dc->inPage = true;
	return 1;
}

int WINAPI EndPage(HDC hdc)
{
	auto dc = DCOf(hdc);
	if (!dc || !dc->inPage) return 0;
	dc->inPage = false;
	CGContextRestoreGState(dc->pdf);
	CGPDFContextEndPage(dc->pdf);
	return 1;
}

int WINAPI AbortDoc(HDC hdc)
{
	auto dc = DCOf(hdc);
	if (!dc || !dc->pdf) return SP_ERROR;
	if (dc->inPage) EndPage(hdc);
	CGPDFContextClose(dc->pdf);
	CGContextRelease(dc->pdf);
	dc->pdf = nullptr;
	if (dc->outputPath.empty()) std::remove(Utf8(dc->pdfPath).c_str());
	return 1;
}

int WINAPI EndDoc(HDC hdc)
{
	auto dc = DCOf(hdc);
	if (!dc || !dc->pdf) return SP_ERROR;
	if (dc->inPage) EndPage(hdc);
	CGPDFContextClose(dc->pdf);
	CGContextRelease(dc->pdf);
	dc->pdf = nullptr;
	if (!dc->outputPath.empty()) return 1;	// ファイルへの出力（DOCINFO の lpszOutput）
	if (backend::Available()) {
		backend::PrintJob job;
		job.pdfPath = U16(dc->pdfPath);
		job.title = U16(dc->jobName);
		job.printer = U16(dc->printerName);
		job.width = PageWidthPoints(dc);
		job.height = PageHeightPoints(dc);
		std::atomic<int> result{0};
		backend::RunPrintJob(&job, &result);
		WaitPanel([&] { return result.load() != 0; });
	}
	std::remove(Utf8(dc->pdfPath).c_str());
	return 1;
}

//! GETPRINTINGOFFSET などの問い合わせ。用紙全体が描画範囲なので、どれも未対応として返す
int WINAPI Escape(HDC, int, int, LPCSTR, LPVOID) { return 0; }

WINBOOL WINAPI OpenPrinterW(LPWSTR name, LPHANDLE printer, LPPRINTER_DEFAULTSW)
{
	if (!name || !printer) return FALSE;
	const auto known = KnownPrinters();
	if (std::find(known.begin(), known.end(), std::wstring(name)) == known.end()) return FALSE;
	*printer = reinterpret_cast<HANDLE>(1);
	return TRUE;
}

WINBOOL WINAPI ClosePrinter(HANDLE) { return TRUE; }

LONG WINAPI DocumentPropertiesW(HWND, HANDLE, LPWSTR, PDEVMODEW output, PDEVMODEW input, DWORD mode)
{
	if (!mode) return sizeof(DEVMODEW);
	if ((mode & DM_OUT_BUFFER) && output) {
		if (input && input != output) std::memcpy(output, input, sizeof(DEVMODEW));
		NormalizePaper(output);
	}
	return IDOK;
}

WINBOOL WINAPI PrintDlgW(LPPRINTDLGW pd)
{
	if (pd->Flags & PD_RETURNDEFAULT) {
		pd->hDevMode = NewDevMode();
		pd->hDevNames = NewDevNames(KnownPrinters().front(), true);
		return TRUE;
	}
	if (!pd->hDevMode) pd->hDevMode = NewDevMode();
	if (!pd->hDevNames) pd->hDevNames = NewDevNames(KnownPrinters().front(), true);
	if (pd->Flags & PD_PRINTSETUP) return backend::Available() && ChoosePrinter(pd);
	return TRUE;	// 範囲や部数は EndDoc の印刷パネルで選ぶ
}

}  // extern "C"
