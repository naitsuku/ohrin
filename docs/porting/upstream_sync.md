# 上流（sakura-editor/sakura）の取り込み手順

上流のリリースタグを `merge` で取り込む（公開後は履歴を書き換えないので rebase はしない）。
初回は 2026-11-08 に v2.4.3 を取り込んだ（それまでの土台は 2026-04-29 の master `71514c301`）。

## 手順

```bash
git fetch upstream --tags
git merge --no-ff --no-commit v2.4.4        # 取り込むタグ
git diff --name-only --diff-filter=U        # 衝突したファイル
```

1. **衝突を解く**
   - `CLAUDE.md` は上流にもある。こちら（移植の方針）を残す: `git checkout --ours CLAUDE.md`
   - `sakura_core` の `#ifdef SAKURA_WINPORT` の箇所。上流が同じ不具合を直していたら上流側を採って ifdef を消す
     （v2.4.3 では CMenuDrawer の `tbd[i]` がそうだった）
2. **ビルド**: `cmake -S . -B build && cmake --build build`
   - コンパイルエラーの多くは `std::filesystem::path` の `native()` / `c_str()` を wstring として使う箇所（I-101）。
     `#ifdef SAKURA_WINPORT` で `.wstring()` にする
   - リンクエラーは互換層に無い Win32 API。`winport/src` に実装する（意味のあるもの）か、
     `tools/porting/gen_winport_stubs.py` で空実装を作る（呼ばれても困らないもの）
3. **上流の新しい仕組みを確認する**: 互換層は「1 スレッド = 1 仮想プロセス」なので、
   **プロセスに 1 つ** のつもりの static は全エディターで共有されてしまう。新しいシングルトンや static の追加に注意
   （v2.4.3 の `TSakuraSingleton` がこれで、全ウィンドウのメニューが空になった。I-126）
4. **テスト**: `build/bin/sakura_mac_tests`。上流が増やしたテストファイルは `tests/mac_tests/CMakeLists.txt` の
   `TESTS1_ENABLED` に足す（mac で成り立たないものは理由を書いて除外）
   ```bash
   for f in $(ls src/test/cpp/tests1/*.cpp | xargs -n1 basename); do grep -q "$f" tests/mac_tests/CMakeLists.txt || echo "未登録 $f"; done
   ```
5. **ASan**: `cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" -DCMAKE_C_FLAGS=... -DCMAKE_OBJCXX_FLAGS=... -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address`
   でテストとアプリ（起動 → 新規作成 → 複数ファイル → Cmd+Q）を通す
6. **アプリで確認**: メニューが開く、新規ウィンドウ、複数ファイルを渡して起動、保存とクリップボード、Cmd+Q の終了コード 0
7. 版番号は `src/main/cmake/version.cmake` から自動で読む（`sakura_mac/CMakeLists.txt`）。DMG を作り直す

## 差分を減らす

Windows でも起きる不具合は上流へ報告・PR する（issues.md I-125）。上流で直れば、次の取り込みで ifdef を消せる。
