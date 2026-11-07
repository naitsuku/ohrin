## 変更内容

## 確認したこと

- [ ] `cmake --build build` と `ctest --test-dir build --output-on-failure` が通る
- [ ] アプリで動作を確認した（確認した操作を書く）

## sakura_core/ を変更した場合

- [ ] `#ifdef SAKURA_WINPORT` で囲み、本家との差分を最小にした
- [ ] Windows 版でも起きる不具合なら、本家への報告・PR も検討した
