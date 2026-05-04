# sakura_core/ 非 Windows ビルド除外ファイル一覧

生成日: 2026-05-04 (Phase 1)
最終更新: 同上

非 Windows ビルドで `sakura_core/CMakeLists.txt` の対象から外したファイルの一覧と理由。
Phase 2 以降で段階的に含めていく。

## 現在の収録方針

**Phase 1 時点では `sakura_core/platform/platform_impl_wx.cpp` のみを収録**
（PAL の wxWidgets / POSIX 実装）。

それ以外の `sakura_core/` 配下 `.cpp` 374 ファイルはすべて除外。

## 除外の根本原因

`sakura_core/StdAfx.h`（プリコンパイル済みヘッダ相当）が以下の Win32 SDK ヘッダを
直接 include している：

```cpp
#include <Windows.h>
#include <windowsx.h>
#include <comdef.h>
#include <CommCtrl.h>
#include <HtmlHelp.h>
#include <imm.h>
#include <oaidl.h>
#include <shellapi.h>
#include <ShlDisp.h>
#include <ShlObj.h>
#include <Uxtheme.h>
#include <vsstyle.h>
#include <wrl.h>
#include <wrl/client.h>
```

`sakura_core` のほぼ全ファイルが先頭で `#include "StdAfx.h"` しているため、
これらのヘッダすべてに shim を用意するか、StdAfx.h 自体を条件分岐させない限り、
非 Windows でコンパイルできない。

`Windows.h` のみは `sakura_core/platform/shim_win32/Windows.h` として最小 shim を
用意済み（`win32_types_stub.h` 経由）だが、`<comdef.h>`（COM）, `<CommCtrl.h>`
（共通コントロール）, `<imm.h>`（IME）, `<wrl.h>`（WinRT）等は意味のある shim を
書くこと自体が大規模な作業になる。

## Phase 2 への申し送り

含める順序の推奨:

1. **`basis/` 系**（小さい数値・文字列・ポイント・矩形ユーティリティ）
   - 多くは `POINT`, `RECT`, `SIZE` 程度の Win32 構造体しか使っていないので、
     `win32_types_stub.h` の追加だけで通る可能性がある
2. **`charset/` 系**（文字コード変換）
   - `wchar_t`/`char` 操作が中心、Win32 GUI に触れない
3. **`debug/CRunningTimer.cpp`, `debug/Debug1.cpp` など独立度の高いファイル**
4. **`doc/` 系**（テキストバッファ）
   - GUI 非依存部分から
5. **`util/` 系**
   - 一部はファイル I/O やシェル連携を含むので注意

各段階で `cmake --build build 2>&1 | grep "error:"` を見て、
StdAfx.h 経由で未定義の Win32 シンボルを `win32_types_stub.h` に追加する。

## 解析時点での Win32 依存度別ファイル数

`docs/porting/win32_dependency_analysis.md` 参照。

- 309 ファイル: 何らかの Win32 API を使用
- 65 ファイル: Win32 API 直接呼び出しなし（ただし StdAfx.h 経由の include は別問題）

## 個別の高リスクファイル（先送り推奨）

| ファイル | 難易度 | 主な Win32 依存 | 対応フェーズ |
|---|---:|---|---|
| `prop/CPropCommon.h` | 578 | window_management, GDI | Phase 4（プロパティシート） |
| `window/CEditWnd.cpp` | 509 | window, GDI, IME | Phase 2 |
| `window/CTabWnd.cpp` | 460 | window, GDI | Phase 2 |
| `outline/CDlgFuncList.cpp` | 352 | window, GDI | Phase 4（ダイアログ） |
| `print/CPrintPreview.cpp` | 336 | GDI, window | Phase 5（印刷） |
| `view/CEditView.cpp` | 328 | window, GDI, IME | Phase 2 |
| `view/CEditView_Paint.cpp` | 296 | GDI, window | Phase 2 |
| `apiwrap/StdControl.h` | 256 | window | Phase 2 |
| `uiparts/CImageListMgr.cpp` | 244 | GDI | Phase 3 |
| `_main/CControlTray.cpp` | 187 | window, file_io, IPC | Phase 3 |
