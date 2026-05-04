# Win32 API 依存度分析

対象: `sakura_core/` 配下の .cpp / .h

生成: `tools/porting/analyze_win32_deps.py`

## カテゴリ別サマリ

| カテゴリ | 影響ファイル数 | 総出現回数 | 難易度重み |
|---|---:|---:|---:|
| window_management | 212 | 2399 | 4 |
| ime | 7 | 24 | 5 |
| file_io | 25 | 74 | 2 |
| registry | 6 | 32 | 2 |
| ipc_shared_memory | 98 | 205 | 5 |
| gdi_drawing | 97 | 895 | 4 |
| com_ole | 26 | 82 | 3 |
| process_thread | 9 | 26 | 3 |
| clipboard | 6 | 52 | 2 |
| shell_integration | 6 | 16 | 2 |

影響を受けるファイル総数: **309**

## 移植難易度スコア Top 20

スコア = Σ(出現回数 × カテゴリ重み)。値が大きいほど移植が困難。

| 順位 | スコア | ファイル | 主な依存カテゴリ |
|---:|---:|---|---|
| 1 | 578 | `sakura_core/prop/CPropCommon.h` | window_management(138), gdi_drawing(4), ipc_shared_memory(2) |
| 2 | 509 | `sakura_core/window/CEditWnd.cpp` | window_management(114), gdi_drawing(12), ime(1) |
| 3 | 460 | `sakura_core/window/CTabWnd.cpp` | window_management(92), gdi_drawing(23) |
| 4 | 352 | `sakura_core/outline/CDlgFuncList.cpp` | window_management(80), gdi_drawing(8) |
| 5 | 336 | `sakura_core/print/CPrintPreview.cpp` | gdi_drawing(56), window_management(28) |
| 6 | 328 | `sakura_core/view/CEditView.cpp` | window_management(52), gdi_drawing(22), ime(6) |
| 7 | 296 | `sakura_core/view/CEditView_Paint.cpp` | gdi_drawing(60), window_management(14) |
| 8 | 256 | `sakura_core/apiwrap/StdControl.h` | window_management(64) |
| 9 | 244 | `sakura_core/uiparts/CImageListMgr.cpp` | gdi_drawing(61) |
| 10 | 232 | `sakura_core/window/CSplitBoxWnd.cpp` | gdi_drawing(37), window_management(21) |
| 11 | 206 | `sakura_core/dlg/CDlgOpenFile_CommonFileDialog.cpp` | window_management(46), shell_integration(6), ipc_shared_memory(2) |
| 12 | 197 | `sakura_core/typeprop/CPropTypes.h` | window_management(45), gdi_drawing(3), ipc_shared_memory(1) |
| 13 | 192 | `sakura_core/view/CCaret.cpp` | gdi_drawing(38), window_management(10) |
| 14 | 188 | `sakura_core/uiparts/CGraphics.cpp` | gdi_drawing(45), window_management(2) |
| 15 | 187 | `sakura_core/_main/CControlTray.cpp` | window_management(39), file_io(3), ipc_shared_memory(3) |
| 16 | 185 | `sakura_core/window/CSplitterWnd.cpp` | window_management(31), gdi_drawing(14), ipc_shared_memory(1) |
| 17 | 178 | `sakura_core/dlg/CDlgAbout.cpp` | gdi_drawing(27), window_management(15), shell_integration(5) |
| 18 | 178 | `sakura_core/util/window.cpp` | window_management(27), gdi_drawing(15), ipc_shared_memory(2) |
| 19 | 177 | `sakura_core/dlg/CDialog.cpp` | window_management(34), gdi_drawing(9), ipc_shared_memory(1) |
| 20 | 173 | `sakura_core/view/CEditView_Scroll.cpp` | gdi_drawing(24), window_management(18), ipc_shared_memory(1) |

## カテゴリ別ヒット詳細

### window_management (重み 4)

- `sakura_core/_main/CControlProcess.cpp:187` `HWND` — `HWND hwnd = m_pcTray->Create( GetProcessInstance() );`
- `sakura_core/_main/CControlTray.cpp:66` `HWND` — `static LRESULT CALLBACK CControlTrayWndProc( HWND, UINT, WPARAM, LPARAM );`
- `sakura_core/_main/CControlTray.cpp:108` `HWND` — `void CControlTray::DoGrepCreateWindow(HINSTANCE hinst, HWND msgParent, CDlgGrep& cDlgGrep)`
- `sakura_core/_main/CControlTray.cpp:169` `HWND` — `HWND	hwnd,	// handle of window`
- `sakura_core/_main/CControlTray.cpp:179` `WM_CREATE` — `case WM_CREATE:`
- `sakura_core/_main/CControlTray.cpp:190` `DefWindowProc` — `return ::DefWindowProc( hwnd, uMsg, wParam, lParam );`
- `sakura_core/_main/CControlTray.cpp:215` `HWND` — `HWND CControlTray::Create( HINSTANCE hInstance )`
- `sakura_core/_main/CControlTray.cpp:224` `HWND` — `HWND hwndWork = ::FindWindow( strCEditAppName.c_str(), strCEditAppName.c_str() );`
- `sakura_core/_main/CControlTray.cpp:246` `RegisterClass` — `ATOM	atom = RegisterClass( &wc );`
- `sakura_core/_main/CControlTray.cpp:253` `CreateWindow` — `::CreateWindow(`
- `sakura_core/_main/CControlTray.cpp:286` `HWND` — `bool CControlTray::CreateTrayIcon( [[maybe_unused]] HWND hWnd )`
- `sakura_core/_main/CControlTray.cpp:341` `HWND` — `BOOL CControlTray::TrayMessage( HWND hDlg, DWORD dwMessage, UINT uID, HICON hIcon, const WCHAR* pszTip )`
- `sakura_core/_main/CControlTray.cpp:366` `HWND` — `HWND	hwnd,	// handle of window`
- `sakura_core/_main/CControlTray.cpp:375` `HWND` — `HWND			hwndWork;`
- `sakura_core/_main/CControlTray.cpp:380` `HWND` — `static HWND	hwndHtmlHelp;`
- `sakura_core/_main/CControlTray.cpp:430` `HWND` — `HWND target = m_pShareData->m_sNodes.m_pEditArr[i].GetHwnd();`
- `sakura_core/_main/CControlTray.cpp:447` `HWND` — `::SendMessageW(HWND(lParam), MYWM_UIPI_CHECK, 0L, LPARAM(hWnd));	// 返事を返す`
- `sakura_core/_main/CControlTray.cpp:503` `WM_CLOSE` — `::SendMessage( hwnd, WM_CLOSE, 0, 0 );`
- `sakura_core/_main/CControlTray.cpp:520` `WM_CREATE` — `case WM_CREATE:`
- `sakura_core/_main/CControlTray.cpp:565` `WM_COMMAND` — `case WM_COMMAND:`
- `sakura_core/_main/CControlTray.cpp:566` `HWND` — `OnCommand( HIWORD(wParam), LOWORD(wParam), (HWND) lParam );`
- `sakura_core/_main/CControlTray.cpp:781` `WM_COMMAND` — `WM_COMMAND,`
- `sakura_core/_main/CControlTray.cpp:896` `WM_CLOSE` — `case WM_CLOSE:`
- `sakura_core/_main/CControlTray.cpp:910` `WM_DESTROY` — `OnDestroy();	// 2006.07.09 ryoji WM_DESTROY と同じ処理をする（トレイアイコンの破棄などもNT系では必要）`
- `sakura_core/_main/CControlTray.cpp:914` `WM_DESTROY` — `case WM_DESTROY:`
- `sakura_core/_main/CControlTray.cpp:950` `HWND` — `void CControlTray::OnCommand( WORD wNotifyCode, [[maybe_unused]] WORD wID , [[maybe_unused]] HWND hwndCtl )`
- `sakura_core/_main/CControlTray.cpp:1108` `HWND` — `HWND				hWndParent,			//!< [in] 親ウィンドウハンドル．エラーメッセージ表示用`
- `sakura_core/_main/CControlTray.cpp:1145` `HWND` — `HWND hwndAncestor = MyGetAncestor( hWndParent, GA_ROOTOWNER2 );	// 2007.10.22 ryoji GA_ROOTOWNER -> GA_ROOTOWNER2`
- `sakura_core/_main/CControlTray.cpp:1331` `HWND` — `HWND			hWndParent,`
- `sakura_core/_main/CControlTray.cpp:1365` `HWND` — `void CControlTray::ActiveNextWindow(HWND hwndParent)`

…他 2369 件（省略）

### ime (重み 5)

- `sakura_core/cmd/CViewCommander_Edit_advanced.cpp:970` `ImmGetContext` — `HIMC hIMC = ::ImmGetContext( m_pCommanderView->GetHwnd() );`
- `sakura_core/cmd/CViewCommander_Edit_advanced.cpp:996` `ImmReleaseContext` — `::ImmReleaseContext( m_pCommanderView->GetHwnd(), hIMC);`
- `sakura_core/doc/CDocEditor.cpp:150` `ImmGetContext` — `hIme = ImmGetContext( hwnd ); //######大丈夫？ // 2013.06.04 EditWndからViewに変更`
- `sakura_core/doc/CDocEditor.cpp:183` `ImmReleaseContext` — `ImmReleaseContext( hwnd, hIme ); //######大丈夫？`
- `sakura_core/env/CHokanMgr.cpp:356` `WM_IME_STARTCOMPOSITION` — `if((wMsg >= WM_IME_STARTCOMPOSITION && wMsg <= WM_IME_KEYLAST) || (wMsg >= WM_IME_SETCONTEXT && wMsg <= WM_IME_KEYUP)){`
- `sakura_core/util/os.cpp:338` `ImmGetContext` — `HIMC hIMC = ImmGetContext(hWnd);`
- `sakura_core/util/os.cpp:346` `ImmReleaseContext` — `bRet &= ImmReleaseContext(hWnd, hIMC);`
- `sakura_core/view/CEditView.cpp:485` `WM_IME_COMPOSITION` — `case WM_IME_COMPOSITION:`
- `sakura_core/view/CEditView.cpp:490` `ImmGetContext` — `hIMC = ImmGetContext( hwnd );`
- `sakura_core/view/CEditView.cpp:500` `ImmGetCompositionString` — `dwSize = ImmGetCompositionString(hIMC, GCS_RESULTSTR, nullptr, 0);`
- `sakura_core/view/CEditView.cpp:519` `ImmGetCompositionString` — `ImmGetCompositionString(hIMC, GCS_RESULTSTR, pszText, dwSize);`
- `sakura_core/view/CEditView.cpp:530` `ImmReleaseContext` — `ImmReleaseContext( hwnd, hIMC );`
- `sakura_core/view/CEditView.cpp:542` `WM_IME_ENDCOMPOSITION` — `case WM_IME_ENDCOMPOSITION:`
- `sakura_core/view/CEditView_Ime.cpp:46` `ImmGetContext` — `hIme = ImmGetContext( GetHwnd() );`
- `sakura_core/view/CEditView_Ime.cpp:59` `ImmReleaseContext` — `ImmReleaseContext( GetHwnd(), hIme );`
- `sakura_core/view/CEditView_Ime.cpp:75` `ImmGetContext` — `HIMC hIMC = ::ImmGetContext( GetHwnd() );`
- `sakura_core/view/CEditView_Ime.cpp:84` `ImmSetCompositionWindow` — `::ImmSetCompositionWindow( hIMC, &CompForm );`
- `sakura_core/view/CEditView_Ime.cpp:86` `ImmReleaseContext` — `::ImmReleaseContext( GetHwnd() , hIMC );`
- `sakura_core/view/CEditView_Ime.cpp:100` `ImmGetContext` — `HIMC	hIMC = ::ImmGetContext( GetHwnd() );`
- `sakura_core/view/CEditView_Ime.cpp:104` `ImmReleaseContext` — `::ImmReleaseContext( GetHwnd() , hIMC );`
- `sakura_core/view/CEditView_Ime.cpp:259` `ImmGetContext` — `HIMC hIMC = ::ImmGetContext( hwnd );`
- `sakura_core/view/CEditView_Ime.cpp:264` `ImmGetCompositionString` — `LONG immRet = ::ImmGetCompositionString(hIMC, GCS_COMPSTR, m_szComposition, int(std::size(m_szComposition)));`
- `sakura_core/view/CEditView_Ime.cpp:268` `ImmReleaseContext` — `::ImmReleaseContext( hwnd, hIMC );`
- `sakura_core/window/CEditWnd.cpp:1315` `WM_IME_COMPOSITION` — `case WM_IME_COMPOSITION:`

### file_io (重み 2)

- `sakura_core/_main/CControlProcess.cpp:240` `CloseHandle` — `::CloseHandle( m_hEventCPInitialized );`
- `sakura_core/_main/CControlProcess.cpp:244` `CloseHandle` — `::CloseHandle( m_hMutexCP );`
- `sakura_core/_main/CControlProcess.cpp:249` `CloseHandle` — `::CloseHandle( m_hMutex );`
- `sakura_core/_main/CControlTray.cpp:1170` `DeleteFile` — `::DeleteFile( fileName );`
- `sakura_core/_main/CControlTray.cpp:1318` `CloseHandle` — `CloseHandle( p.hThread );`
- `sakura_core/_main/CControlTray.cpp:1319` `CloseHandle` — `CloseHandle( p.hProcess );`
- `sakura_core/_main/CMutex.h:40` `CloseHandle` — `::CloseHandle( m_hObj );`
- `sakura_core/_main/CNormalProcess.cpp:132` `CloseHandle` — `::CloseHandle( hMutex );`
- `sakura_core/_main/CNormalProcess.cpp:164` `CloseHandle` — `::CloseHandle( hMutex );`
- `sakura_core/_main/CNormalProcess.cpp:213` `CloseHandle` — `::CloseHandle( hMutex );`
- `sakura_core/_main/CNormalProcess.cpp:267` `CloseHandle` — `::CloseHandle( hMutex );`
- `sakura_core/_main/CNormalProcess.cpp:411` `CloseHandle` — `::CloseHandle( hMutex );`
- `sakura_core/_main/CNormalProcess.cpp:499` `CloseHandle` — `::CloseHandle( hMutex );`
- `sakura_core/_main/CProcessFactory.cpp:151` `CloseHandle` — `::CloseHandle( hMutexCP );`
- `sakura_core/_main/CProcessFactory.cpp:232` `CloseHandle` — `::CloseHandle( p.hThread );`
- `sakura_core/_main/CProcessFactory.cpp:233` `CloseHandle` — `::CloseHandle( p.hProcess );`
- `sakura_core/_main/CProcessFactory.cpp:264` `CloseHandle` — `::CloseHandle( hEvent );`
- `sakura_core/_main/CProcessFactory.cpp:268` `CloseHandle` — `::CloseHandle( hEvent );`
- `sakura_core/agent/CBackupAgent.cpp:181` `DeleteFile` — `if( ::DeleteFile( szPath ) == 0 ){`
- `sakura_core/agent/CBackupAgent.cpp:206` `MoveFile` — `if( ::MoveFile( szPath, szNewPath ) == 0 ){`
- `sakura_core/agent/CBackupAgent.cpp:234` `CreateDirectory` — `::CreateDirectory( szPath2, nullptr );`
- `sakura_core/agent/CBackupAgent.cpp:238` `CopyFile` — `if( ::CopyFile( target_file, szPath, FALSE ) ){`
- `sakura_core/agent/CGrepAgent.cpp:155` `GetFileSize` — `LONGLONG GetFileSize(){`
- `sakura_core/agent/CGrepAgent.cpp:159` `GetFileSize` — `return m_cfl.GetFileSize();`
- `sakura_core/agent/CGrepAgent.cpp:257` `WriteFile` — `::WriteFile(out, cmemOut.GetRawPtr(), cmemOut.GetRawLength(), &dwWrite, nullptr);`
- `sakura_core/agent/CGrepAgent.cpp:1482` `GetFileSize` — `if( 5000000 < cfl.GetFileSize() ){`
- `sakura_core/agent/CGrepAgent.cpp:1770` `DeleteFile` — `if( FALSE == ::DeleteFile( oldFile.c_str() ) ){`
- `sakura_core/agent/CGrepAgent.cpp:1776` `MoveFile` — `if( FALSE == ::MoveFile( fileName, oldFile.c_str() ) ){`
- `sakura_core/agent/CGrepAgent.cpp:1782` `DeleteFile` — `if( FALSE == ::DeleteFile( fileName ) ){`
- `sakura_core/agent/CGrepAgent.cpp:1788` `MoveFile` — `if( FALSE == ::MoveFile( name.c_str(), fileName ) ){`

…他 44 件（省略）

### registry (重み 2)

- `sakura_core/env/CFileNameManager.cpp:232` `HKEY_CURRENT_USER` — `bFolderPath = ReadRegistry( HKEY_CURRENT_USER,`
- `sakura_core/env/CFileNameManager.cpp:236` `HKEY_LOCAL_MACHINE` — `bFolderPath = ReadRegistry( HKEY_LOCAL_MACHINE,`
- `sakura_core/typeprop/CDlgTypeList.cpp:846` `HKEY_LOCAL_MACHINE` — `if( ( errorCode = keyExt_HKLM.Open(HKEY_LOCAL_MACHINE, sDotExt.c_str(), KEY_READ) ) == 0 )`
- `sakura_core/typeprop/CDlgTypeList.cpp:852` `HKEY_CURRENT_USER` — `if((errorCode = keyExt.Open(HKEY_CURRENT_USER, sDotExt.c_str(), KEY_READ | KEY_WRITE)) != 0)`
- `sakura_core/typeprop/CDlgTypeList.cpp:854` `HKEY_CURRENT_USER` — `if( (errorCode = keyExt.Create(HKEY_CURRENT_USER, sDotExt.c_str())) != 0 ){ return errorCode; }`
- `sakura_core/typeprop/CDlgTypeList.cpp:869` `HKEY_CURRENT_USER` — `if( ! CRegKey::ExistsKey(HKEY_CURRENT_USER, sProgIDPath.c_str()) )`
- `sakura_core/typeprop/CDlgTypeList.cpp:873` `HKEY_LOCAL_MACHINE` — `if( (errorCode = CopyRegistry(HKEY_LOCAL_MACHINE, sBasePath + szProgID_HKLM, HKEY_CURRENT_USER, sProgIDPath)) != 0 ){ re`
- `sakura_core/typeprop/CDlgTypeList.cpp:873` `HKEY_CURRENT_USER` — `if( (errorCode = CopyRegistry(HKEY_LOCAL_MACHINE, sBasePath + szProgID_HKLM, HKEY_CURRENT_USER, sProgIDPath)) != 0 ){ re`
- `sakura_core/typeprop/CDlgTypeList.cpp:883` `HKEY_CURRENT_USER` — `if( (errorCode = keyShellActionCommand.Open(HKEY_CURRENT_USER, sShellActionCommandPath.c_str(), KEY_READ | KEY_WRITE)) !`
- `sakura_core/typeprop/CDlgTypeList.cpp:885` `HKEY_CURRENT_USER` — `if( (errorCode = keyShellActionCommand.Create(HKEY_CURRENT_USER, sShellActionCommandPath.c_str())) != 0 ){ return errorC`
- `sakura_core/typeprop/CDlgTypeList.cpp:894` `HKEY_CURRENT_USER` — `if( (errorCode = keyShellAction.Open(HKEY_CURRENT_USER, sShellActionPath.c_str(), KEY_READ | KEY_WRITE)) !=0 ){ return e`
- `sakura_core/typeprop/CDlgTypeList.cpp:898` `HKEY_CURRENT_USER` — `if( (errorCode = keyShell.Open(HKEY_CURRENT_USER, sShellPath.c_str(), KEY_READ | KEY_WRITE)) != 0 ){ return errorCode; }`
- `sakura_core/typeprop/CDlgTypeList.cpp:908` `HKEY_CURRENT_USER` — `if( (errorCode = keyBackup.Open(HKEY_CURRENT_USER, sBackupPath.c_str(), KEY_READ | KEY_WRITE)) != 0 )`
- `sakura_core/typeprop/CDlgTypeList.cpp:910` `HKEY_CURRENT_USER` — `if( (errorCode = keyBackup.Create(HKEY_CURRENT_USER, sBackupPath.c_str())) != 0 ){ return errorCode; }`
- `sakura_core/typeprop/CDlgTypeList.cpp:920` `HKEY_CURRENT_USER` — `if( (errorCode = keyBackup.Open(HKEY_CURRENT_USER, sBackupPath.c_str(), KEY_READ | KEY_WRITE)) != 0 )`
- `sakura_core/typeprop/CDlgTypeList.cpp:964` `HKEY_CURRENT_USER` — `if((errorCode = keyExt.Open(HKEY_CURRENT_USER, sDotExt.c_str(), KEY_READ | KEY_WRITE)) != 0)`
- `sakura_core/typeprop/CDlgTypeList.cpp:983` `HKEY_CURRENT_USER` — `if( (errorCode = keyShellAction.Open(HKEY_CURRENT_USER, sShellActionPath.c_str(), KEY_READ | KEY_WRITE)) != 0 )`
- `sakura_core/typeprop/CDlgTypeList.cpp:989` `HKEY_CURRENT_USER` — `if( (errorCode = keyShell.Open(HKEY_CURRENT_USER, sShellPath.c_str(), KEY_READ | KEY_WRITE)) != 0 ){ return errorCode; }`
- `sakura_core/typeprop/CDlgTypeList.cpp:991` `HKEY_CURRENT_USER` — `if( (errorCode = keyBackup.Open(HKEY_CURRENT_USER, sBackupPath.c_str(), KEY_READ | KEY_WRITE)) != 0 )`
- `sakura_core/typeprop/CDlgTypeList.cpp:1006` `HKEY_CURRENT_USER` — `if( (errorCode = DeleteRegistry(HKEY_CURRENT_USER, sProgIDPath)) != 0 ){ return errorCode; }`
- `sakura_core/typeprop/CDlgTypeList.cpp:1013` `HKEY_CURRENT_USER` — `if( (errorCode = DeleteRegistry(HKEY_CURRENT_USER, sDotExt)) != 0 ){ return errorCode; }`
- `sakura_core/typeprop/CDlgTypeList.cpp:1016` `HKEY_CURRENT_USER` — `if( (errorCode = DeleteRegistry(HKEY_CURRENT_USER, sShellActionPath)) != 0 ){ return errorCode; }`
- `sakura_core/typeprop/CDlgTypeList.cpp:1054` `HKEY_CURRENT_USER` — `if((errorCode = keyExt.Open(HKEY_CURRENT_USER, sDotExt.c_str(), KEY_READ)) != 0)`
- `sakura_core/typeprop/CDlgTypeList.cpp:1069` `HKEY_CURRENT_USER` — `if( ! CRegKey::ExistsKey(HKEY_CURRENT_USER, sShellActionPath.c_str()) )`
- `sakura_core/typeprop/CDlgTypeList.cpp:1076` `HKEY_CURRENT_USER` — `if( (errorCode = keyShell.Open(HKEY_CURRENT_USER, sShellPath.c_str(), KEY_READ)) != 0 ){ return errorCode; }`
- `sakura_core/util/RegKey.h:69` `RegCloseKey` — `RegCloseKey(_key);`
- `sakura_core/util/RegKey.h:129` `RegDeleteValue` — `return RegDeleteValue(_key, valueName);`
- `sakura_core/util/RegKey.h:133` `RegDeleteKey` — `return RegDeleteKey(_key, path);`
- `sakura_core/util/RegKey.h:179` `RegDeleteKey` — `return RegDeleteKey(root, path);`
- `sakura_core/util/os.cpp:159` `RegCloseKey` — `RegCloseKey(Key);`

…他 2 件（省略）

### ipc_shared_memory (重み 5)

- `sakura_core/StdAfx.h:187` `DLLSHAREDATA` — `#include "env/DLLSHAREDATA.h"`
- `sakura_core/_main/CAppMode.cpp:25` `DLLSHAREDATA` — `DLLSHAREDATA* pShare = &GetDllShareData();`
- `sakura_core/_main/CAppMode.cpp:41` `DLLSHAREDATA` — `DLLSHAREDATA* pShare = &GetDllShareData();`
- `sakura_core/_main/CControlProcess.cpp:21` `DLLSHAREDATA` — `#include "env/DLLSHAREDATA.h"`
- `sakura_core/_main/CControlTray.cpp:1117` `DLLSHAREDATA` — `DLLSHAREDATA*	pShareData = &GetDllShareData();`
- `sakura_core/_main/CControlTray.cpp:1338` `DLLSHAREDATA` — `DLLSHAREDATA*	pShareData;`
- `sakura_core/_main/CControlTray.cpp:1451` `DLLSHAREDATA` — `DLLSHAREDATA* pShareData = &GetDllShareData();	/* 共有データ構造体のアドレスを返す */`
- `sakura_core/_main/CControlTray.h:37` `DLLSHAREDATA` — `struct DLLSHAREDATA;`
- `sakura_core/_main/CControlTray.h:141` `DLLSHAREDATA` — `DLLSHAREDATA*	m_pShareData;`
- `sakura_core/_main/CProcess.cpp:23` `DLLSHAREDATA` — `#include "env/DLLSHAREDATA.h"`
- `sakura_core/_main/CProcess.cpp:47` `DLLSHAREDATA` — `const DLLSHAREDATA *pShareData = &GetDllShareData();`
- `sakura_core/_main/WinMain.cpp:34` `DLLSHAREDATA` — `#include "env/DLLSHAREDATA.h"`
- `sakura_core/_main/WinMain.cpp:73` `DLLSHAREDATA` — `DEBUG_TRACE(L"sizeof(DLLSHAREDATA) = %d\n",sizeof(DLLSHAREDATA));`
- `sakura_core/_main/WinMain.cpp:73` `DLLSHAREDATA` — `DEBUG_TRACE(L"sizeof(DLLSHAREDATA) = %d\n",sizeof(DLLSHAREDATA));`
- `sakura_core/agent/CAutoSaveAgent.cpp:17` `DLLSHAREDATA` — `#include "env/DLLSHAREDATA.h"`
- `sakura_core/agent/CSearchAgent.cpp:22` `DLLSHAREDATA` — `#include "env/DLLSHAREDATA.h"`
- `sakura_core/charset/charcode.cpp:12` `DLLSHAREDATA` — `#include "env/DLLSHAREDATA.h"`
- `sakura_core/cmd/CViewCommander.h:18` `DLLSHAREDATA` — `struct DLLSHAREDATA;`
- `sakura_core/config/system_constants.h:530` `DLLSHAREDATA` — `MYDEVMODE の未使用メンバを削除した為、PRINTSETTING や DLLSHAREDATA のメモリレイアウトが変更`
- `sakura_core/config/system_constants.h:533` `DLLSHAREDATA` — `WriteQuit廃止に伴い未使用となった IniFolder のメンバを削除した為、DLLSHAREDATA のメモリレイアウトを変更`
- `sakura_core/config/system_constants.h:539` `DLLSHAREDATA` — `STypeConfig::m_nVertLineIdxの要素数を変更した為、DLLSHAREDATA のメモリレイアウトが変更`
- `sakura_core/dlg/CDialog.cpp:26` `DLLSHAREDATA` — `#include "env/DLLSHAREDATA.h"`
- `sakura_core/dlg/CDialog.h:24` `DLLSHAREDATA` — `struct DLLSHAREDATA;`
- `sakura_core/dlg/CDialog.h:150` `DLLSHAREDATA` — `DLLSHAREDATA*	m_pShareData;`
- `sakura_core/dlg/CDlgFavorite.cpp:20` `DLLSHAREDATA` — `#include "env/DLLSHAREDATA.h"`
- `sakura_core/dlg/CDlgFavorite.cpp:82` `DLLSHAREDATA` — `CRecentの各実装クラスは DLLSHAREDATA へ直接アクセスしている。`
- `sakura_core/dlg/CDlgGrep.cpp:29` `DLLSHAREDATA` — `#include "env/DLLSHAREDATA.h"`
- `sakura_core/dlg/CDlgGrepReplace.cpp:27` `DLLSHAREDATA` — `#include "env/DLLSHAREDATA.h"`
- `sakura_core/dlg/CDlgOpenFile.cpp:23` `DLLSHAREDATA` — `#include "env/DLLSHAREDATA.h"`
- `sakura_core/dlg/CDlgOpenFile_CommonFileDialog.cpp:28` `DLLSHAREDATA` — `#include "env/DLLSHAREDATA.h"`

…他 175 件（省略）

### gdi_drawing (重み 4)

- `sakura_core/_main/CControlTray.cpp:243` `HBRUSH` — `wc.hbrBackground	= (HBRUSH)(COLOR_WINDOW + 1);`
- `sakura_core/charset/charcode.cpp:90` `HDC` — `void CCharWidthCache::Init(const LOGFONT &lf, const LOGFONT &lfFull, HDC hdcOrg)`
- `sakura_core/charset/charcode.cpp:97` `SelectObject` — `m_hFont1Old = ::SelectObject(m_hdc1, m_hFont1);`
- `sakura_core/charset/charcode.cpp:104` `SelectObject` — `m_hFont2Old = ::SelectObject(m_hdc2, m_hFont2);`
- `sakura_core/charset/charcode.cpp:111` `HDC` — `std::array<HDC, 2> hdcArr = { m_hdc1, m_hdc2 };`
- `sakura_core/charset/charcode.cpp:191` `HDC` — `[[nodiscard]] HDC CCharWidthCache::SelectHDC(wchar_t c) const`
- `sakura_core/charset/charcode.cpp:212` `HDC` — `void Init( const LOGFONT &lf1, const LOGFONT &lf2, ECharWidthFontMode fMode, HDC hdc )`
- `sakura_core/charset/charcode.cpp:270` `HDC` — `void InitCharWidthCacheFromDC( const LOGFONT* lfs, ECharWidthFontMode fMode, HDC hdcOrg )`
- `sakura_core/charset/charcode.h:228` `DeleteObject` — `using FontHolder = cxx::ResourceHolder<&::DeleteObject, HFONT>;`
- `sakura_core/charset/charcode.h:228` `HFONT` — `using FontHolder = cxx::ResourceHolder<&::DeleteObject, HFONT>;`
- `sakura_core/charset/charcode.h:230` `SelectObject` — `using SelectionHolder = cxx::ResourceHolder<&::SelectObject>;`
- `sakura_core/charset/charcode.h:239` `HDC` — `void Init(const LOGFONT& lf, const LOGFONT& lfFull, HDC hdcOrg);`
- `sakura_core/charset/charcode.h:254` `HDC` — `[[nodiscard]] HDC SelectHDC(wchar_t c) const;`
- `sakura_core/charset/charcode.h:273` `HDC` — `void InitCharWidthCacheFromDC(const LOGFONT* lfs, ECharWidthFontMode fMode, HDC hdcOrg );`
- `sakura_core/cmd/CViewCommander_Cursor.cpp:229` `GetTextMetrics` — `const CLayoutXInt dx_default = this->m_pCommanderView->GetTextMetrics().GetLayoutXDefault(); // 文字のない部分の移動量。`
- `sakura_core/cmd/CViewCommander_Cursor.cpp:306` `GetTextMetrics` — `const CLayoutXInt dx_default = this->m_pCommanderView->GetTextMetrics().GetLayoutXDefault(); // 文字のない部分(※改行マーク部分を含む)の移動量`
- `sakura_core/cmd/CViewCommander_Edit.cpp:514` `HDC` — `HDC hdc = m_pCommanderView->GetDC();`
- `sakura_core/cmd/CViewCommander_Edit.cpp:764` `HDC` — `HDC hdc = m_pCommanderView->GetDC();`
- `sakura_core/dlg/CDialog.cpp:581` `HDC` — `HDC hDC;`
- `sakura_core/dlg/CDialog.cpp:582` `HFONT` — `HFONT hFont;`
- `sakura_core/dlg/CDialog.cpp:595` `HFONT` — `hFont = (HFONT)::SendMessageAny( hwndCtl, WM_GETFONT, 0, (LPARAM)nullptr );`
- `sakura_core/dlg/CDialog.cpp:596` `HFONT` — `hFont = (HFONT)::SelectObject( hDC, hFont );`
- `sakura_core/dlg/CDialog.cpp:596` `SelectObject` — `hFont = (HFONT)::SelectObject( hDC, hFont );`
- `sakura_core/dlg/CDialog.cpp:613` `SelectObject` — `::SelectObject( hDC, hFont );`
- `sakura_core/dlg/CDialog.cpp:640` `HFONT` — `HFONT CDialog::SetMainFont( HWND hTarget )`
- `sakura_core/dlg/CDialog.cpp:644` `HFONT` — `HFONT	hFont;`
- `sakura_core/dlg/CDialog.cpp:648` `HFONT` — `hFont = (HFONT)::SendMessage(hTarget, WM_GETFONT, 0, 0);`
- `sakura_core/dlg/CDialog.h:143` `HFONT` — `HFONT			m_hFontDialog;	// ダイアログに設定されているフォント(破棄禁止)`
- `sakura_core/dlg/CDialog.h:165` `HFONT` — `HFONT SetMainFont( HWND hTarget );`
- `sakura_core/dlg/CDialog.h:167` `HFONT` — `HFONT GetDialogFont() { return m_hFontDialog; }`

…他 865 件（省略）

### com_ole (重み 3)

- `sakura_core/_os/CDropTarget.cpp:39` `IUnknown` — `IUnknown*	pThis,`
- `sakura_core/_os/CDropTarget.h:64` `IUnknown` — `static HRESULT QueryInterfaceImpl( IUnknown*, REFIID, REFIID, void** );`
- `sakura_core/_os/OleTypes.h:28` `BSTR` — `BSTR Data;`
- `sakura_core/_os/OleTypes.h:32` `BSTR` — `SysString(BSTR &Source)             { Data = ::SysAllocStringLen(Source, SysStringLen(Source)); }`
- `sakura_core/_os/OleTypes.h:80` `VARIANT` — `VARIANT Data;`
- `sakura_core/_os/OleTypes.h:83` `VARIANT` — `Variant(VARIANT &Source)        { ::VariantCopyInd(&Data, &Source); }`
- `sakura_core/_os/OleTypes.h:116` `VARIANT` — `inline Variant* Wrap(VARIANT *Value)`
- `sakura_core/_os/OleTypes.h:121` `BSTR` — `inline SysString* Wrap(BSTR *Value)`
- `sakura_core/basis/CErrorInfo.cpp:35` `BSTR` — `IFACEMETHODIMP CErrorInfo::GetSource(BSTR *pBstrSource)`
- `sakura_core/basis/CErrorInfo.cpp:45` `BSTR` — `IFACEMETHODIMP CErrorInfo::GetDescription(BSTR *pBstrDescription)`
- `sakura_core/basis/CErrorInfo.cpp:55` `BSTR` — `IFACEMETHODIMP CErrorInfo::GetHelpFile(BSTR *pBstrHelpFile)`
- `sakura_core/basis/CErrorInfo.h:43` `BSTR` — `IFACEMETHODIMP GetSource(BSTR *pBstrSource) override;`
- `sakura_core/basis/CErrorInfo.h:44` `BSTR` — `IFACEMETHODIMP GetDescription(BSTR *pBstrDescription) override;`
- `sakura_core/basis/CErrorInfo.h:45` `BSTR` — `IFACEMETHODIMP GetHelpFile(BSTR *pBstrHelpFile) override;`
- `sakura_core/io/CZipFile.cpp:75` `IDispatch` — `FAILED(pFileItem->get_GetFolder((IDispatch**)&pFile)) ||`
- `sakura_core/macro/CEditorIfObj.cpp:28` `VARIANT` — `bool CEditorIfObj::HandleFunction(CEditView* View, EFunctionCode ID, VARIANT *Arguments, const int ArgSize, VARIANT &Res`
- `sakura_core/macro/CEditorIfObj.cpp:28` `VARIANT` — `bool CEditorIfObj::HandleFunction(CEditView* View, EFunctionCode ID, VARIANT *Arguments, const int ArgSize, VARIANT &Res`
- `sakura_core/macro/CEditorIfObj.h:27` `VARIANT` — `bool HandleFunction(CEditView* View, EFunctionCode ID, VARIANT *Arguments, const int ArgSize, VARIANT &Result) override;`
- `sakura_core/macro/CEditorIfObj.h:27` `VARIANT` — `bool HandleFunction(CEditView* View, EFunctionCode ID, VARIANT *Arguments, const int ArgSize, VARIANT &Result) override;`
- `sakura_core/macro/CIfObj.cpp:85` `BSTR` — `/* [length_is][size_is][out] */ BSTR __RPC_FAR *rgBstrNames,`
- `sakura_core/macro/CIfObj.cpp:116` `VARIANT` — `/* [out] */ [[maybe_unused]] VARIANT __RPC_FAR *pVarResult,`
- `sakura_core/macro/CIfObj.cpp:125` `BSTR` — `/* [out] */ BSTR __RPC_FAR *pBstrName,`
- `sakura_core/macro/CIfObj.cpp:126` `BSTR` — `/* [out] */ BSTR __RPC_FAR *pBstrDocString,`
- `sakura_core/macro/CIfObj.cpp:128` `BSTR` — `/* [out] */ BSTR __RPC_FAR *pBstrHelpFile) override`
- `sakura_core/macro/CIfObj.cpp:159` `BSTR` — `/* [out] */ [[maybe_unused]] BSTR __RPC_FAR *pBstrDllName,`
- `sakura_core/macro/CIfObj.cpp:160` `BSTR` — `/* [out] */ [[maybe_unused]] BSTR __RPC_FAR *pBstrName,`
- `sakura_core/macro/CIfObj.cpp:182` `IUnknown` — `/* [in] */ [[maybe_unused]] IUnknown __RPC_FAR *pUnkOuter,`
- `sakura_core/macro/CIfObj.cpp:191` `BSTR` — `/* [out] */ [[maybe_unused]] BSTR __RPC_FAR *pBstrMops) override`
- `sakura_core/macro/CIfObj.cpp:240` `BSTR` — `/* [length_is][size_is][out] */ BSTR __RPC_FAR *rgBstrNames,`
- `sakura_core/macro/CIfObj.cpp:273` `VARIANT` — `VARIANT FAR* pvarResult,`

…他 52 件（省略）

### process_thread (重み 3)

- `sakura_core/_main/CControlProcess.cpp:124` `CreateMutex` — `m_hMutex = ::CreateMutex( nullptr, FALSE, GSTR_MUTEX_SAKURA );`
- `sakura_core/_main/CControlProcess.cpp:127` `CreateMutex` — `TopErrorMessage( nullptr, L"CreateMutex()失敗。\n終了します。" );`
- `sakura_core/_main/CControlProcess.cpp:136` `CreateEvent` — `m_hEventCPInitialized = ::CreateEvent( nullptr, TRUE, FALSE, strInitEvent.c_str() );`
- `sakura_core/_main/CControlProcess.cpp:140` `CreateEvent` — `TopErrorMessage( nullptr, L"CreateEvent()失敗。\n終了します。" );`
- `sakura_core/_main/CControlProcess.cpp:147` `CreateMutex` — `m_hMutexCP = ::CreateMutex( nullptr, TRUE, strCtrlProcEvent.c_str() );`
- `sakura_core/_main/CControlProcess.cpp:150` `CreateMutex` — `TopErrorMessage( nullptr, L"CreateMutex()失敗。\n終了します。" );`
- `sakura_core/_main/CControlTray.cpp:1099` `CreateProcess` — `@date 2000.10.24 genta WinExec -> CreateProcess．同期機能を付加`
- `sakura_core/_main/CControlTray.cpp:1235` `CreateProcess` — `BOOL bCreateResult = CreateProcess(`
- `sakura_core/_main/CMutex.h:30` `CreateMutex` — `m_hObj = ::CreateMutex( psa, bInitialOwner, pszName );`
- `sakura_core/_main/CMutex.h:46` `WaitForSingleObject` — `DWORD dwRet = ::WaitForSingleObject( m_hObj, dwTimeout );`
- `sakura_core/_main/CNormalProcess.cpp:489` `CreateMutex` — `hMutex = ::CreateMutex( nullptr, TRUE, strMutexInitName.c_str() );`
- `sakura_core/_main/CNormalProcess.cpp:492` `CreateMutex` — `TopErrorMessage( nullptr, L"CreateMutex()失敗。\n終了します。" );`
- `sakura_core/_main/CNormalProcess.cpp:496` `WaitForSingleObject` — `DWORD dwRet = ::WaitForSingleObject( hMutex, 15000 );	// 2002/2/8 aroka 少し長くした`
- `sakura_core/_main/CProcessFactory.cpp:149` `OpenMutex` — `hMutexCP = ::OpenMutex( MUTEX_ALL_ACCESS, FALSE, strMutexSakuraCp.c_str() );	// 2006.04.10 ryoji ::CreateMutex() を ::Ope`
- `sakura_core/_main/CProcessFactory.cpp:149` `CreateMutex` — `hMutexCP = ::OpenMutex( MUTEX_ALL_ACCESS, FALSE, strMutexSakuraCp.c_str() );	// 2006.04.10 ryoji ::CreateMutex() を ::Ope`
- `sakura_core/_main/CProcessFactory.cpp:149` `OpenMutex` — `hMutexCP = ::OpenMutex( MUTEX_ALL_ACCESS, FALSE, strMutexSakuraCp.c_str() );	// 2006.04.10 ryoji ::CreateMutex() を ::Ope`
- `sakura_core/_main/CProcessFactory.cpp:155` `CreateMutex` — `return false;	// コントロールプロセスは存在していないか、まだ CreateMutex() してない`
- `sakura_core/_main/CProcessFactory.cpp:202` `CreateProcess` — `BOOL bCreateResult = ::CreateProcess(`
- `sakura_core/_main/CProcessFactory.cpp:262` `WaitForSingleObject` — `dwRet = ::WaitForSingleObject( hEvent, 30000 );`
- `sakura_core/cmd/CViewCommander_TagJump.cpp:604` `CreateProcess` — `BOOL bProcessResult = CreateProcess(`
- `sakura_core/macro/CWSH.cpp:265` `WaitForSingleObject` — `if(::WaitForSingleObject(pParam->hEvent, pParam->nCancelTimer * 1000) == WAIT_TIMEOUT){`
- `sakura_core/macro/CWSH.cpp:352` `CreateEvent` — `sThreadParam.hEvent = ::CreateEvent(nullptr, TRUE, FALSE, nullptr);`
- `sakura_core/macro/CWSH.cpp:380` `WaitForSingleObject` — `::WaitForSingleObject(hThread, INFINITE);`
- `sakura_core/prop/CPropComPlugin.cpp:584` `CreateProcess` — `BOOL bRet = ::CreateProcess( nullptr, szCmdLine, nullptr, nullptr, TRUE,`
- `sakura_core/view/CEditView_ExecCmd.cpp:241` `CreateProcess` — `if( CreateProcess( nullptr, cmdline, nullptr, nullptr, TRUE,`
- `sakura_core/view/CEditView_ExecCmd.cpp:260` `CreateProcess` — `if( CreateProcess( nullptr, cmdline, nullptr, nullptr, TRUE,`

### clipboard (重み 2)

- `sakura_core/_os/CClipboard.cpp:55` `OpenClipboard` — `if (OpenClipboard(hWndOwner)) {`
- `sakura_core/_os/CClipboard.cpp:72` `CloseClipboard` — `CloseClipboard();`
- `sakura_core/_os/CClipboard.cpp:82` `EmptyClipboard` — `EmptyClipboard();`
- `sakura_core/_os/CClipboard.cpp:109` `SetClipboardData` — `::SetClipboardData( CF_OEMTEXT, hgClipText );`
- `sakura_core/_os/CClipboard.cpp:115` `CF_UNICODETEXT` — `bool bUnicodeText = (uFormat == (UINT)-1 || uFormat == CF_UNICODETEXT);`
- `sakura_core/_os/CClipboard.cpp:131` `SetClipboardData` — `SetClipboardData( CF_UNICODETEXT, hgClipText );`
- `sakura_core/_os/CClipboard.cpp:131` `CF_UNICODETEXT` — `SetClipboardData( CF_UNICODETEXT, hgClipText );`
- `sakura_core/_os/CClipboard.cpp:161` `SetClipboardData` — `SetClipboardData( uFormatSakuraClip, hgClipSakura );`
- `sakura_core/_os/CClipboard.cpp:179` `SetClipboardData` — `SetClipboardData( uFormat, hgClipMSDEVColumn );`
- `sakura_core/_os/CClipboard.cpp:197` `SetClipboardData` — `SetClipboardData( uFormat, hgClipMSDEVLine );`
- `sakura_core/_os/CClipboard.cpp:213` `SetClipboardData` — `SetClipboardData( uFormat, hgClipMSDEVLine2 );`
- `sakura_core/_os/CClipboard.cpp:269` `SetClipboardData` — `SetClipboardData( uFormat, hgClipText );`
- `sakura_core/_os/CClipboard.cpp:319` `GetClipboardData` — `HGLOBAL hSakura = GetClipboardData( uFormatSakuraClip );`
- `sakura_core/_os/CClipboard.cpp:333` `CF_UNICODETEXT` — `if( uGetFormat == -1 || uGetFormat == CF_UNICODETEXT ){`
- `sakura_core/_os/CClipboard.cpp:334` `GetClipboardData` — `hUnicode = GetClipboardData( CF_UNICODETEXT );`
- `sakura_core/_os/CClipboard.cpp:334` `CF_UNICODETEXT` — `hUnicode = GetClipboardData( CF_UNICODETEXT );`
- `sakura_core/_os/CClipboard.cpp:349` `GetClipboardData` — `hText = GetClipboardData( CF_OEMTEXT );`
- `sakura_core/_os/CClipboard.cpp:369` `GetClipboardData` — `HDROP hDrop = (HDROP)GetClipboardData(CF_HDROP);`
- `sakura_core/_os/CClipboard.cpp:408` `CF_TEXT` — `{CF_TEXT        ,L"CF_TEXT"},`
- `sakura_core/_os/CClipboard.cpp:408` `CF_TEXT` — `{CF_TEXT        ,L"CF_TEXT"},`
- `sakura_core/_os/CClipboard.cpp:420` `CF_UNICODETEXT` — `{CF_UNICODETEXT ,L"CF_UNICODETEXT"},`
- `sakura_core/_os/CClipboard.cpp:420` `CF_UNICODETEXT` — `{CF_UNICODETEXT ,L"CF_UNICODETEXT"},`
- `sakura_core/_os/CClipboard.cpp:490` `CF_UNICODETEXT` — `if( uFormat == CF_UNICODETEXT || uFormat == GetSakuraFormat() ){`
- `sakura_core/_os/CClipboard.cpp:553` `SetClipboardData` — `SetClipboardData( uFormat, hgClipText );`
- `sakura_core/_os/CClipboard.cpp:608` `GetClipboardData` — `HGLOBAL hClipData = GetClipboardData( uFormat );`
- `sakura_core/_os/CClipboard.cpp:670` `CF_UNICODETEXT` — `if(::IsClipboardFormatAvailable(CF_UNICODETEXT))return true;`
- `sakura_core/_os/CClipboard.cpp:696` `CF_UNICODETEXT` — `if(IsClipboardFormatAvailable(CF_UNICODETEXT))return CF_UNICODETEXT;`
- `sakura_core/_os/CClipboard.cpp:696` `CF_UNICODETEXT` — `if(IsClipboardFormatAvailable(CF_UNICODETEXT))return CF_UNICODETEXT;`
- `sakura_core/_os/CClipboard.cpp:702` `OpenClipboard` — `BOOL CClipboard::OpenClipboard(`
- `sakura_core/_os/CClipboard.cpp:706` `OpenClipboard` — `return ::OpenClipboard(hWndNewOwner);`

…他 22 件（省略）

### shell_integration (重み 2)

- `sakura_core/cmd/CViewCommander_File.cpp:533` `ShellExecute` — `auto hInstance = ::ShellExecute(GetMainWindow(), L"open", L"explorer.exe", pszExplorerCommand, nullptr, SW_SHOWNORMAL);`
- `sakura_core/dlg/CDlgAbout.cpp:304` `ShellExecute` — `::ShellExecute( GetHwnd(), nullptr, buf, nullptr, nullptr, SW_SHOWNORMAL );`
- `sakura_core/dlg/CDlgAbout.cpp:310` `ShellExecute` — `::ShellExecute(GetHwnd(), nullptr, _T(CI_BUILD_URL), nullptr, nullptr, SW_SHOWNORMAL);`
- `sakura_core/dlg/CDlgAbout.cpp:312` `ShellExecute` — `::ShellExecute(GetHwnd(), nullptr, _T(GIT_REMOTE_ORIGIN_URL), nullptr, nullptr, SW_SHOWNORMAL);`
- `sakura_core/dlg/CDlgAbout.cpp:318` `ShellExecute` — `::ShellExecute(GetHwnd(), nullptr, _T(GITHUB_COMMIT_URL), nullptr, nullptr, SW_SHOWNORMAL);`
- `sakura_core/dlg/CDlgAbout.cpp:323` `ShellExecute` — `::ShellExecute(GetHwnd(), nullptr, _T(GITHUB_PR_HEAD_URL), nullptr, nullptr, SW_SHOWNORMAL);`
- `sakura_core/dlg/CDlgOpenFile_CommonFileDialog.cpp:1182` `GetOpenFileName` — `/*! リトライ機能付き GetOpenFileName`
- `sakura_core/dlg/CDlgOpenFile_CommonFileDialog.cpp:1188` `GetOpenFileName` — `BOOL bRet = ::GetOpenFileName( ofn );`
- `sakura_core/dlg/CDlgOpenFile_CommonFileDialog.cpp:1193` `GetOpenFileName` — `bRet = ::GetOpenFileName( ofn );`
- `sakura_core/dlg/CDlgOpenFile_CommonFileDialog.cpp:1199` `GetSaveFileName` — `/*! リトライ機能付き GetSaveFileName`
- `sakura_core/dlg/CDlgOpenFile_CommonFileDialog.cpp:1205` `GetSaveFileName` — `BOOL bRet = ::GetSaveFileName( ofn );`
- `sakura_core/dlg/CDlgOpenFile_CommonFileDialog.cpp:1210` `GetSaveFileName` — `bRet = ::GetSaveFileName( ofn );`
- `sakura_core/prop/CPropComPlugin.cpp:231` `ShellExecute` — `::ShellExecute( nullptr, L"open", sBaseDir.c_str(), nullptr, nullptr, SW_SHOW );`
- `sakura_core/prop/CPropComPlugin.cpp:256` `ShellExecute` — `::ShellExecute(nullptr, L"Open", plugin->m_sUrl.c_str(), nullptr, nullptr, SW_SHOW);`
- `sakura_core/util/shell.cpp:541` `ShellExecute` — `ShellExecute( ::GetActiveWindow(), nullptr, buf, nullptr, nullptr, SW_SHOWNORMAL );`
- `sakura_core/view/CEditView_Mouse.cpp:1510` `ShellExecute` — `::ShellExecute( nullptr, L"open", pszFile, nullptr, nullptr, SW_SHOW );`

