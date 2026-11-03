#!/usr/bin/env python3
"""winport_inventory.py — リンカの未解決シンボル一覧から、winport が実装すべき API の棚卸し表を作る。

使い方:
    clang++ <全 .o> -o probe 2> link.txt
    python3 tools/porting/winport_inventory.py --link link.txt --out docs/porting/winport_api_inventory.md

各シンボルを mingw-w64 のインポートライブラリ（libuser32.a 等）と照合して DLL 別に分類し、
参照元ファイル（sakura_core のディレクトリ）別の件数も出す。

SPDX-License-Identifier: Zlib
"""
import argparse
import collections
import os
import re
import subprocess

MINGW_LIB = "/opt/homebrew/opt/mingw-w64/toolchain-x86_64/x86_64-w64-mingw32/lib"
DLLS = ["kernel32", "user32", "gdi32", "comctl32", "comdlg32", "shell32", "shlwapi", "ole32",
        "oleaut32", "imm32", "advapi32", "uxtheme", "winspool", "version", "msimg32", "dwmapi",
        "mpr", "winmm", "htmlhelp", "shcore", "uuid", "ntdll", "ucrt", "msvcrt"]
CRT_DLLS = {"ucrt", "msvcrt"}


def exports_of(dll):
    p = os.path.join(MINGW_LIB, f"lib{dll}.a")
    if not os.path.exists(p):
        return set()
    out = subprocess.run(["nm", p], capture_output=True, text=True).stdout
    syms = set()
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[-2] in ("T", "D", "R", "I"):
            name = parts[-1]
            if name.startswith("__imp_"):
                name = name[6:]
            syms.add(name)
    return syms


def parse_link(path):
    """{symbol: set(参照元 .o)}。ld64 は C++ 名をデマングル済みで出力する"""
    refs = collections.defaultdict(set)
    cur = None
    for line in open(path, encoding="utf-8", errors="replace"):
        m = re.match(r'^  "(.+)", referenced from:', line)
        if m:
            cur = m.group(1)
            continue
        m = re.match(r"^\s+.* in (\S+)\.o$", line)
        if m and cur:
            refs[cur].add(m.group(1))
    return refs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--link", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--objs", help="参照元 .o の所在を探すための objs ディレクトリ（領域別集計用）")
    a = ap.parse_args()

    refs = parse_link(a.link)
    exports = {d: exports_of(d) for d in DLLS}
    crt_decls = open(os.path.join(os.path.dirname(__file__), "..", "..", "winport", "include", "winport_crt.h")).read()
    mangled = sorted(refs)

    # 参照元ファイル名 → sakura_core 内のディレクトリ
    area_of = {}
    if a.objs:
        for d, _, fs in os.walk(a.objs):
            for f in fs:
                if f.endswith(".cpp.o"):
                    rel = os.path.relpath(d, a.objs)
                    area_of[f[:-2]] = rel.split(os.sep, 1)[1] if os.sep in rel else rel

    by_dll = collections.defaultdict(list)
    for sym in mangled:
        c_name = sym[1:] if sym.startswith("_") else sym
        dll = next((d for d in DLLS if c_name in exports[d]), None)
        if dll is None and re.search(r"\b" + re.escape(c_name) + r"\(", crt_decls):
            dll = "ucrt"
        if dll is None and sym.startswith("DarkMode::"):
            dll = "darkmodelib（winport 代替ヘッダ）"
        if dll is None:
            dll = "(C++ / sakura 内部)" if "(" in sym else "(不明: マクロ・GUID 等)"
        elif dll in CRT_DLLS:
            dll = "CRT (MSVC 拡張)"
        by_dll[dll].append(sym)

    area_count = collections.defaultdict(lambda: collections.Counter())
    for sym, objs in refs.items():
        for o in objs:
            area_count[area_of.get(o, "?")][sym] += 1

    order = sorted(by_dll, key=lambda d: -len(by_dll[d]))
    with open(a.out, "w") as f:
        f.write("# winport API 棚卸し（リンク未解決シンボル）\n\n")
        f.write("`tools/porting/winport_inventory.py` が生成。sakura_core 全 375 ファイルを winport ヘッダで\n"
                "コンパイルし、リンクしたときの未解決シンボルを mingw-w64 のインポートライブラリで DLL 別に分類した。\n\n")
        f.write(f"- 未解決シンボル総数: **{len(mangled)}**\n\n")
        f.write("## DLL 別件数\n\n| DLL | 件数 |\n|---|---:|\n")
        for d in order:
            f.write(f"| {d} | {len(by_dll[d])} |\n")
        f.write("\n## 参照元の領域別（ユニークシンボル数）\n\n| 領域 | シンボル数 |\n|---|---:|\n")
        for area, cnt in sorted(area_count.items(), key=lambda kv: -len(kv[1])):
            f.write(f"| {area} | {len(cnt)} |\n")
        f.write("\n## DLL 別一覧（参照ファイル数）\n")
        for d in order:
            f.write(f"\n### {d}（{len(by_dll[d])}）\n\n")
            items = sorted(by_dll[d], key=lambda s: (-len(refs[s]), s))
            f.write(", ".join(f"`{s if '(' in s else s[1:]}`({len(refs[s])})"
                              for s in items) + "\n")
    print(f"{len(mangled)} symbols -> {a.out}")
    for d in order:
        print(f"  {d}: {len(by_dll[d])}")


if __name__ == "__main__":
    main()
