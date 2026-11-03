#!/usr/bin/env python3
"""gen_winport_stubs.py — 未実装 Win32 API の弱シンボルスタブを生成する。

リンカの未解決シンボル一覧（clang++ ... 2> link.txt）から、Win32 DLL 由来の C 関数について
「初回呼び出し時に名前を stderr に出して 0 を返す」弱シンボルを生成する。
winport/src に同名の実装（強シンボル）を書けば、そちらが優先される。

arm64 の呼び出し規約では引数はレジスタ渡しで呼び出し側が片付けるため、
引数なしで定義したスタブを任意の引数で呼んでも安全に 0（x0）を返せる。

既存の generated_stubs.cpp に載っている名前は保持し、新しく見つかった名前を追加する。

使い方:
    python3 tools/porting/gen_winport_stubs.py --link link.txt [--out winport/src/generated_stubs.cpp]

SPDX-License-Identifier: Zlib
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
import winport_inventory as inv  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
STUB_DLLS = {"kernel32", "user32", "gdi32", "comctl32", "comdlg32", "shell32", "shlwapi", "ole32",
             "oleaut32", "imm32", "advapi32", "uxtheme", "winspool", "version", "msimg32", "dwmapi",
             "mpr", "winmm", "htmlhelp", "shcore", "ntdll"}
RE_EXISTING = re.compile(r"^WINPORT_STUB\((\w+), (\w+)\)$", re.M)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--link", required=True)
    ap.add_argument("--out", default=os.path.join(ROOT, "winport/src/generated_stubs.cpp"))
    a = ap.parse_args()

    stubs = {}
    if os.path.exists(a.out):
        for dll, name in RE_EXISTING.findall(open(a.out).read()):
            stubs[name] = dll

    exports = {d: inv.exports_of(d) for d in STUB_DLLS}
    for sym in inv.parse_link(a.link):
        if "(" in sym or not sym.startswith("_"):
            continue
        name = sym[1:]
        dll = next((d for d in sorted(STUB_DLLS) if name in exports[d]), None)
        if dll:
            stubs[name] = dll

    with open(a.out, "w") as f:
        f.write("""/*
	generated_stubs.cpp — 未実装 Win32 API の弱シンボルスタブ（自動生成）

	tools/porting/gen_winport_stubs.py が生成する。手で編集しないこと。
	実装するときは winport/src の該当ファイルに正しいシグネチャで定義する（強シンボルが優先）。

	SPDX-License-Identifier: Zlib
*/
#include "winport_internal.h"

#define WINPORT_STUB(dll, name) \\
	extern "C" __attribute__((weak)) long long name(void) { \\
		winport::ReportUnimplemented(#dll, #name); \\
		return 0; \\
	}

""")
        for name, dll in sorted(stubs.items(), key=lambda kv: (kv[1], kv[0])):
            f.write(f"WINPORT_STUB({dll}, {name})\n")
    print(f"{len(stubs)} stubs -> {a.out}")


if __name__ == "__main__":
    main()
