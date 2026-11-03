# winport API 棚卸し（リンク未解決シンボル）

`tools/porting/winport_inventory.py` が生成。sakura_core 全 375 ファイルを winport ヘッダで
コンパイルし、リンクしたときの未解決シンボルを mingw-w64 のインポートライブラリで DLL 別に分類した。

- 未解決シンボル総数: **528**

## DLL 別件数

| DLL | 件数 |
|---|---:|
| user32 | 177 |
| kernel32 | 108 |
| gdi32 | 58 |
| CRT (MSVC 拡張) | 48 |
| darkmodelib（winport 代替ヘッダ） | 28 |
| uuid | 15 |
| shell32 | 15 |
| comctl32 | 15 |
| imm32 | 11 |
| oleaut32 | 10 |
| ole32 | 9 |
| advapi32 | 9 |
| comdlg32 | 6 |
| uxtheme | 6 |
| shlwapi | 5 |
| winspool | 3 |
| msimg32 | 2 |
| dwmapi | 1 |
| htmlhelp | 1 |
| mpr | 1 |

## 参照元の領域別（ユニークシンボル数）

| 領域 | シンボル数 |
|---|---:|
| window | 163 |
| view | 105 |
| outline | 100 |
| util | 89 |
| _main | 68 |
| dlg | 61 |
| macro | 49 |
| cmd | 46 |
| env | 32 |
| prop | 31 |
| print | 31 |
| uiparts | 30 |
| typeprop | 30 |
| _os | 25 |
| doc | 23 |
| agent | 19 |
| charset | 12 |
| io | 10 |
| sakura_core | 8 |
| func | 6 |
| view/figures | 5 |
| types | 5 |
| plugin | 4 |
| main/cpp/cxx | 4 |
| apiwrap | 4 |
| docplus | 4 |
| mem | 4 |
| doc/layout | 3 |
| debug | 2 |
| extmodule | 2 |
| recent | 2 |
| doc/logic | 2 |
| basis | 1 |
| parse | 1 |

## DLL 別一覧（参照ファイル数）

### user32（177）

`BeginPaint`(7), `EndPaint`(7), `RegisterWindowMessageW`(7), `DispatchMessageW`(6), `GetFocus`(6), `GetWindowTextW`(6), `SendMessageTimeoutW`(6), `BringWindowToTop`(5), `CreatePopupMenu`(5), `CreateWindowExW`(5), `DestroyMenu`(5), `GetActiveWindow`(5), `GetDlgCtrlID`(5), `IsIconic`(5), `IsWindow`(5), `IsWindowEnabled`(5), `TrackPopupMenu`(5), `WindowFromPoint`(5), `AllowSetForegroundWindow`(4), `CallWindowProcW`(4), `ClientToScreen`(4), `DrawFocusRect`(4), `GetDesktopWindow`(4), `GetDlgItem`(4), `GetDlgItemInt`(4), `GetMessageW`(4), `GetWindowTextLengthW`(4), `IsDialogMessageW`(4), `IsZoomed`(4), `MessageBeep`(4), `MsgWaitForMultipleObjects`(4), `PeekMessageW`(4), `PostQuitMessage`(4), `ReleaseDC`(4), `SetDlgItemInt`(4), `TranslateMessage`(4), `CheckMenuItem`(3), `CheckMenuRadioItem`(3), `CreateDialogParamW`(3), `DefWindowProcW`(3), `DestroyIcon`(3), `DialogBoxParamW`(3), `DrawTextW`(3), `EnableMenuItem`(3), `EndDialog`(3), `GetAsyncKeyState`(3), `GetCapture`(3), `GetClassNameW`(3), `GetDC`(3), `GetMenuItemCount`(3), `GetMenuItemID`(3), `GetSysColor`(3), `GetWindow`(3), `GetWindowLongPtrW`(3), `GetWindowPlacement`(3), `InflateRect`(3), `IntersectRect`(3), `LoadCursorW`(3), `LoadIconW`(3), `LoadImageW`(3), `PostMessageW`(3), `SendDlgItemMessageW`(3), `SetCapture`(3), `SetCursor`(3), `SetForegroundWindow`(3), `SetTimer`(3), `SystemParametersInfoW`(3), `UpdateWindow`(3), `AttachThreadInput`(2), `CharNextW`(2), `CheckDlgButton`(2), `DestroyCaret`(2), `DrawEdge`(2), `EqualRect`(2), `FindWindowW`(2), `GetClientRect`(2), `GetComboBoxInfo`(2), `GetDoubleClickTime`(2), `GetForegroundWindow`(2), `GetKeyState`(2), `GetLastActivePopup`(2), `GetParent`(2), `GetScrollInfo`(2), `GetSysColorBrush`(2), `GetWindowDC`(2), `GetWindowThreadProcessId`(2), `InvalidateRect`(2), `IsDlgButtonChecked`(2), `IsWindowVisible`(2), `KillTimer`(2), `MoveWindow`(2), `RedrawWindow`(2), `RegisterClassExW`(2), `RegisterClassW`(2), `RegisterClipboardFormatW`(2), `ReleaseCapture`(2), `ScrollWindowEx`(2), `SetActiveWindow`(2), `SetRect`(2), `SetScrollInfo`(2), `SetWindowPlacement`(2), `SetWindowPos`(2), `ShowScrollBar`(2), `UnionRect`(2), `AppendMenuW`(1), `CallNextHookEx`(1), `CharLowerW`(1), `CharPrevW`(1), `CloseClipboard`(1), `CreateAcceleratorTableW`(1), `CreateCaret`(1), `CreateDialogIndirectParamW`(1), `CreateMenu`(1), `DeleteMenu`(1), `DestroyAcceleratorTable`(1), `DestroyWindow`(1), `DragDetect`(1), `DrawFrameControl`(1), `DrawMenuBar`(1), `EmptyClipboard`(1), `EnableWindow`(1), `EnumChildWindows`(1), `EnumClipboardFormats`(1), `FillRect`(1), `FindWindowExW`(1), `GetCaretPos`(1), `GetClipboardData`(1), `GetClipboardFormatNameW`(1), `GetCursor`(1), `GetCursorPos`(1), `GetDCEx`(1), `GetDlgItemTextW`(1), `GetKeyboardLayout`(1), `GetMenu`(1), `GetMenuItemInfoW`(1), `GetMonitorInfoW`(1), `GetSubMenu`(1), `GetSystemMetrics`(1), `GetWindowRect`(1), `HideCaret`(1), `InsertMenuItemW`(1), `InsertMenuW`(1), `IsChild`(1), `IsClipboardFormatAvailable`(1), `LockWindowUpdate`(1), `MapDialogRect`(1), `MapVirtualKeyW`(1), `MessageBoxExW`(1), `MonitorFromPoint`(1), `MonitorFromRect`(1), `MonitorFromWindow`(1), `OffsetRect`(1), `OpenClipboard`(1), `OpenIcon`(1), `PostMessageA`(1), `PtInRect`(1), `RegisterHotKey`(1), `ScreenToClient`(1), `SendMessageW`(1), `SetCaretPos`(1), `SetClipboardData`(1), `SetDlgItemTextW`(1), `SetFocus`(1), `SetMenu`(1), `SetMenuInfo`(1), `SetWindowLongPtrW`(1), `SetWindowTextW`(1), `SetWindowsHookExW`(1), `ShowCaret`(1), `ShowOwnedPopups`(1), `ShowWindow`(1), `TranslateAcceleratorW`(1), `UnhookWindowsHookEx`(1), `UnregisterHotKey`(1), `ValidateRect`(1), `WaitForInputIdle`(1), `WinHelpW`(1)

### kernel32（108）

`GetLastError`(7), `FindClose`(6), `FindFirstFileW`(6), `GetFileAttributesW`(6), `GetModuleFileNameW`(6), `GetModuleHandleW`(6), `Sleep`(6), `CreateProcessW`(5), `DeleteFileW`(5), `FindNextFileW`(5), `FormatMessageW`(5), `GetLocalTime`(5), `GlobalFree`(5), `LocalFree`(5), `MultiByteToWideChar`(5), `SetCurrentDirectoryW`(5), `CloseHandle`(4), `CreateFileW`(4), `FindResourceW`(4), `GetStdHandle`(4), `GetSystemDirectoryW`(4), `GlobalAlloc`(4), `GlobalSize`(4), `LoadResource`(4), `LockResource`(4), `MoveFileW`(4), `MulDiv`(4), `WaitForSingleObject`(4), `CreateDirectoryW`(3), `CreateEventW`(3), `CreateMutexW`(3), `FreeLibrary`(3), `GetCurrentDirectoryW`(3), `GetCurrentProcess`(3), `GetFullPathNameW`(3), `GetProcAddress`(3), `GetTickCount64`(3), `GlobalLock`(3), `GlobalUnlock`(3), `ReleaseMutex`(3), `SizeofResource`(3), `WideCharToMultiByte`(3), `lstrcmpiW`(3), `lstrcpyW`(3), `lstrlenW`(3), `CopyFileW`(2), `CreateFileMappingW`(2), `CreatePipe`(2), `DuplicateHandle`(2), `GetCurrentProcessId`(2), `GetCurrentThreadId`(2), `GetExitCodeProcess`(2), `GetFileTime`(2), `GetTickCount`(2), `MapViewOfFile`(2), `PeekNamedPipe`(2), `ReadFile`(2), `SetEvent`(2), `TerminateProcess`(2), `UnmapViewOfFile`(2), `VirtualAlloc`(2), `VirtualFree`(2), `CompareFileTime`(1), `DebugBreak`(1), `EnumSystemCodePagesW`(1), `FileTimeToLocalFileTime`(1), `FileTimeToSystemTime`(1), `GetCPInfoExW`(1), `GetDateFormatW`(1), `GetDriveTypeW`(1), `GetEnvironmentVariableW`(1), `GetFileSize`(1), `GetLocaleInfoEx`(1), `GetLongPathNameW`(1), `GetPrivateProfileIntW`(1), `GetPrivateProfileStringW`(1), `GetProcessHeap`(1), `GetStartupInfoW`(1), `GetTempFileNameW`(1), `GetTempPathW`(1), `GetThreadUILanguage`(1), `GetTimeFormatW`(1), `GetUserDefaultUILanguage`(1), `GlobalFlags`(1), `HeapAlloc`(1), `HeapFree`(1), `IsValidCodePage`(1), `IsWow64Process`(1), `LCIDToLocaleName`(1), `LoadLibraryExW`(1), `LoadLibraryW`(1), `LocaleNameToLCID`(1), `OpenMutexW`(1), `OutputDebugStringW`(1), `ReplaceFileW`(1), `ResetEvent`(1), `SearchPathW`(1), `SetDllDirectoryW`(1), `SetFileAttributesW`(1), `SetLastError`(1), `SetProcessShutdownParameters`(1), `SetSearchPathMode`(1), `SetThreadUILanguage`(1), `Wow64DisableWow64FsRedirection`(1), `Wow64RevertWow64FsRedirection`(1), `WriteFile`(1), `lstrcmpW`(1), `lstrlenA`(1)

### gdi32（58）

`DeleteDC`(6), `ExtTextOutW`(6), `PatBlt`(5), `BitBlt`(4), `CreateCompatibleBitmap`(4), `CreateCompatibleDC`(4), `CreateDIBSection`(4), `GetStockObject`(4), `GetTextExtentPoint32W`(4), `GetTextMetricsW`(4), `SelectObject`(4), `SetTextColor`(4), `CreateFontIndirectW`(3), `CreateRectRgn`(3), `CreateRectRgnIndirect`(3), `CreateSolidBrush`(3), `DeleteObject`(3), `GetDeviceCaps`(3), `GetObjectW`(3), `PaintRgn`(3), `PolyPolyline`(3), `SetBkMode`(3), `TextOutW`(3), `CombineRgn`(2), `CreatePen`(2), `DPtoLP`(2), `EnumFontFamiliesW`(2), `GetTextColor`(2), `MoveToEx`(2), `Rectangle`(2), `RoundRect`(2), `SetROP2`(2), `StretchBlt`(2), `CreateBitmap`(1), `CreateDCW`(1), `CreatePatternBrush`(1), `EndDoc`(1), `EndPage`(1), `Escape`(1), `ExcludeClipRect`(1), `GetBkColor`(1), `GetCharacterPlacementW`(1), `GetClipBox`(1), `GetClipRgn`(1), `GetFontLanguageInfo`(1), `GetPixel`(1), `GetWindowExtEx`(1), `LPtoDP`(1), `LineTo`(1), `Polygon`(1), `SelectClipRgn`(1), `SetBkColor`(1), `SetMapMode`(1), `SetStretchBltMode`(1), `SetViewportOrgEx`(1), `SetWindowExtEx`(1), `StartDocW`(1), `StartPage`(1)

### CRT (MSVC 拡張)（48）

`_wsplitpath_s`(7), `_wcsnicmp`(6), `wcscat_s`(6), `_snwprintf_s`(5), `_wcsicmp`(5), `wcscpy_s`(5), `wcsncat_s`(5), `_itow`(4), `_waccess`(4), `_wfopen`(4), `_wtol`(4), `vswprintf_s`(4), `wcstok_s`(4), `_swprintf`(3), `_vsnwprintf_s`(3), `_wcsdup`(3), `wcsnlen_s`(3), `_beginthreadex`(2), `_strnicmp`(2), `_vscwprintf`(2), `_wcstok`(2), `_wtempnam`(2), `_wunlink`(2), `swscanf_s`(2), `wcsncpy_s`(2), `_byteswap_ushort`(1), `_create_locale`(1), `_filelength`(1), `_fileno`(1), `_localtime64_s`(1), `_mbcjistojms_l`(1), `_mbcjmstojis_l`(1), `_memicmp`(1), `_time64`(1), `_tzset`(1), `_ultow`(1), `_vscprintf`(1), `_vsnprintf_s`(1), `_vswprintf`(1), `_waccess_s`(1), `_wcslwr`(1), `_wgetenv`(1), `_wmakepath`(1), `_wstrdate`(1), `_wtoi`(1), `memcpy_s`(1), `swprintf_s`(1), `vsprintf_s`(1)

### darkmodelib（winport 代替ヘッダ）（28）

`DarkMode::getTextColor()`(6), `DarkMode::setDarkWndSafe(HWND__*, bool)`(6), `DarkMode::getHotBackgroundColor()`(4), `DarkMode::setChildCtrlsTheme(HWND__*)`(4), `DarkMode::getCtrlBackgroundColor()`(3), `DarkMode::setWindowExStyle(HWND__*, bool, long long)`(3), `DarkMode::setWindowMenuBarSubclass(HWND__*)`(3), `DarkMode::HookDlgProc(HWND__*, unsigned int, unsigned long long, long long)`(2), `DarkMode::getBackgroundColor()`(2), `DarkMode::getDlgBackgroundColor()`(2), `DarkMode::getEdgeColor()`(2), `DarkMode::getHotEdgeColor()`(2), `DarkMode::setDarkWndNotifySafeEx(HWND__*, bool, bool)`(2), `DarkMode::setDefaultColors(bool)`(2), `DarkMode::getViewBackgroundColor()`(1), `DarkMode::initDarkMode()`(1), `DarkMode::isEnabled()`(1), `DarkMode::isExperimentalActive()`(1), `DarkMode::setChildCtrlsSubclassAndTheme(HWND__*)`(1), `DarkMode::setColorizeTitleBarConfig(bool)`(1), `DarkMode::setDarkModeConfig()`(1), `DarkMode::setDarkModeConfigEx(unsigned int)`(1), `DarkMode::setDarkTitleBarEx(HWND__*, bool)`(1), `DarkMode::setDarkTooltips(HWND__*, int)`(1), `DarkMode::setProgressBarCtrlSubclass(HWND__*)`(1), `DarkMode::setStatusBarCtrlSubclass(HWND__*)`(1), `DarkMode::setWindowCtlColorSubclass(HWND__*)`(1), `DarkMode::setWindowEraseBgSubclass(HWND__*)`(1)

### uuid（15）

`CLSID_FileOpenDialog`(2), `IID_IDataObject`(2), `CLSID_FileSaveDialog`(1), `CLSID_WICImagingFactory`(1), `FOLDERID_Documents`(1), `FOLDERID_Profile`(1), `FOLDERID_RoamingAppData`(1), `GUID_NULL`(1), `GUID_WICPixelFormat32bppPBGRA`(1), `IID_IDropSource`(1), `IID_IDropTarget`(1), `IID_IEnumFORMATETC`(1), `IID_IPersistFile`(1), `IID_IShellLinkW`(1), `IID_IUnknown`(1)

### shell32（15）

`DragQueryFileW`(4), `ShellExecuteW`(4), `DragFinish`(3), `DragAcceptFiles`(2), `SHCreateItemFromParsingName`(2), `SHGetDesktopFolder`(2), `SHGetFileInfoW`(2), `ShellExecuteExW`(2), `CLSID_ShellLink`(1), `DragQueryPoint`(1), `SHAddToRecentDocs`(1), `SHFileOperationW`(1), `SHGetKnownFolderPath`(1), `SHGetMalloc`(1), `Shell_NotifyIconW`(1)

### comctl32（15）

`DefSubclassProc`(7), `RemoveWindowSubclass`(7), `SetWindowSubclass`(7), `ImageList_Create`(2), `ImageList_Destroy`(2), `ImageList_Add`(1), `ImageList_Draw`(1), `ImageList_Duplicate`(1), `ImageList_GetIcon`(1), `ImageList_GetIconSize`(1), `ImageList_GetImageCount`(1), `ImageList_ReplaceIcon`(1), `ImageList_SetBkColor`(1), `InitCommonControlsEx`(1), `PropertySheetW`(1)

### imm32（11）

`ImmGetContext`(4), `ImmReleaseContext`(4), `ImmGetCompositionStringW`(2), `ImmGetConversionStatus`(2), `ImmGetOpenStatus`(2), `ImmSetOpenStatus`(2), `ImmGetProperty`(1), `ImmSetCompositionFontW`(1), `ImmSetCompositionStringW`(1), `ImmSetCompositionWindow`(1), `ImmSetConversionStatus`(1)

### oleaut32（10）

`VariantInit`(4), `SysStringLen`(3), `SysAllocString`(2), `SysAllocStringLen`(2), `SysFreeString`(2), `SysReAllocString`(1), `VariantChangeType`(1), `VariantClear`(1), `VariantCopy`(1), `VariantCopyInd`(1)

### ole32（9）

`CoTaskMemFree`(4), `CoCreateInstance`(3), `CLSIDFromProgID`(1), `DoDragDrop`(1), `OleInitialize`(1), `OleUninitialize`(1), `RegisterDragDrop`(1), `ReleaseStgMedium`(1), `RevokeDragDrop`(1)

### advapi32（9）

`RegQueryValueExW`(3), `RegCloseKey`(2), `RegOpenKeyExW`(2), `RegCreateKeyExW`(1), `RegDeleteKeyW`(1), `RegDeleteValueW`(1), `RegEnumKeyExW`(1), `RegEnumValueW`(1), `RegSetValueExW`(1)

### comdlg32（6）

`ChooseColorW`(1), `ChooseFontW`(1), `CommDlgExtendedError`(1), `GetOpenFileNameW`(1), `GetSaveFileNameW`(1), `PrintDlgW`(1)

### uxtheme（6）

`IsThemeActive`(2), `SetWindowTheme`(2), `DrawThemeBackground`(1), `DrawThemeParentBackground`(1), `IsThemeBackgroundPartiallyTransparent`(1), `OpenThemeData`(1)

### shlwapi（5）

`PathIsUNCW`(2), `PathMatchSpecW`(2), `PathAddBackslashW`(1), `PathIsDirectoryW`(1), `SHAutoComplete`(1)

### winspool（3）

`ClosePrinter`(1), `DocumentPropertiesW`(1), `OpenPrinterW`(1)

### msimg32（2）

`AlphaBlend`(2), `TransparentBlt`(1)

### dwmapi（1）

`DwmGetColorizationColor`(1)

### htmlhelp（1）

`HtmlHelpW`(1)

### mpr（1）

`WNetAddConnection3W`(1)
