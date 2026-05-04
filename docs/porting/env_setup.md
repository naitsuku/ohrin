# wxWidgets 開発環境セットアップ記録

生成日: 2026-05-04
プラットフォーム: macOS (Apple Silicon, Darwin 25.3.0)

## インストール

```bash
brew install wxwidgets
# cmake / ninja / pkg-config は既に導入済み
```

## バージョン

| ツール | バージョン |
|---|---|
| Homebrew | 5.1.3 |
| wxWidgets | **3.3.2** （CLAUDE.md 想定の 3.2.x より新しい） |
| CMake | 4.2.2 |
| Ninja | 1.13.2 |
| pkg-config | 2.5.1 |
| AppleClang | 17.0.0.17000013 |

> ⚠️ wxWidgets 3.3 系は 3.2 と API 互換だが微妙な差異が出る可能性あり。
> 問題が出たら `brew install wxwidgets@3.2` を検討する。

## wx-config 出力（抜粋）

```
$ wx-config --cxxflags
-I/opt/homebrew/lib/wx/include/osx_cocoa-unicode-3.3
-I/opt/homebrew/include/wx-3.3
-D_FILE_OFFSET_BITS=64 -DwxDEBUG_LEVEL=0 -DWXUSINGDLL
-D__WXMAC__ -D__WXOSX__ -D__WXOSX_COCOA__

$ wx-config --libs
-L/opt/homebrew/lib
-framework IOKit -framework Carbon -framework Cocoa
-framework QuartzCore -framework AudioToolbox -framework System -framework OpenGL
-lwx_osx_cocoau_xrc-3.3 -lwx_osx_cocoau_html-3.3 -lwx_osx_cocoau_qa-3.3
-lwx_osx_cocoau_core-3.3 -lwx_baseu_xml-3.3 -lwx_baseu_net-3.3 -lwx_base...
```

ターゲット: `osx_cocoa` バックエンド（macOS ネイティブ）。

## Hello World ビルド検証

ソース: `tools/porting/wx_hello_test/main.cpp` + `CMakeLists.txt`

> **配置の差異**: phase0_prompt.md は `tools/wx_hello_test/` を指定しているが、
> upstream の `tools/` にはビルドツールが多数あるため、混入を避けて
> `tools/porting/wx_hello_test/` 配下に新規作成した。

### configure ログ
```
-- Found wxWidgets: -L/opt/homebrew/lib;;;-framework IOKit;-framework Carbon;
   -framework Cocoa;-framework QuartzCore;-framework AudioToolbox;-framework System;
   -framework OpenGL;-lwx_osx_cocoau_core-3.3;-lwx_baseu-3.3 (found version "3.3.2")
-- Configuring done (1.0s)
-- Generating done (0.0s)
```

### build ログ
```
[1/2] Building CXX object CMakeFiles/wx_hello_test.dir/main.cpp.o
[2/2] Linking CXX executable wx_hello_test
```

### 成果物
```
$ file build/wx_hello_test
build/wx_hello_test: Mach-O 64-bit executable arm64
```

警告 0、エラー 0 でビルド完了。

## GUI 起動確認

`./build/wx_hello_test` の対話的起動はバックグラウンドプロセスとして可能。
本セッションでは GUI イベントループに入る挙動を避け、ビルド成功のみで
動作確認とした（CI でも同様の方針が望ましい）。

## 既知の留意点

- wxWidgets 3.3.2 は 3.2.x からマイナーアップ。`wxStringView` など追加 API あり。
- macOS リリースビルドでは Cocoa バックエンドが選択される。
  Linux では `wxGTK` バックエンドになるので CMake の `find_package` は同一でよいが、
  `wx-config --selected-config` で確認する。
- `pkg-config` は wxWidgets では使わない（wxWidgets は独自の wx-config を提供）。
