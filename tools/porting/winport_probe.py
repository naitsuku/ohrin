#!/usr/bin/env python3
"""winport_probe.py — sakura_core 全 .cpp を winport ヘッダでコンパイル試行し、エラーを集計する。

使い方:
    python3 tools/porting/winport_probe.py --gen <Funccode 生成先> --out <ログ出力先> [--jobs N]
        [--syntax-only] [--filter <部分一致>]

出力:
    <out>/logs/<相対パス>.log … ファイル毎のコンパイラ出力
    <out>/objs/…              … オブジェクト（--syntax-only でなければ）
    <out>/summary.md          … 集計（成功数、エラー種別、未定義識別子、欠落ヘッダ）

SPDX-License-Identifier: Zlib
"""
import argparse
import collections
import concurrent.futures
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MINGW_INC = "/opt/homebrew/opt/mingw-w64/toolchain-x86_64/x86_64-w64-mingw32/include"

RE_ERROR = re.compile(r"^(?P<file>[^:\n]+):(?P<line>\d+):\d+: error: (?P<msg>.*)$", re.M)
RE_UNDECL = re.compile(r"use of undeclared identifier '([^']+)'|unknown type name '([^']+)'"
                       r"|no member named '([^']+)' in the global namespace")
RE_NOFILE = re.compile(r"'([^']+)' file not found")


def compiler_args(gen_dir, syntax_only, src, obj):
    args = [
        "clang++", "-std=c++20", "-fdeclspec",
        "-include", os.path.join(ROOT, "winport/include/winport_prelude.h"),
        "-I", os.path.join(ROOT, "winport/include"),
        "-I", os.path.join(ROOT, "src/main/cpp"),
        "-I", os.path.join(ROOT, "src/main/resources"),
        "-I", os.path.join(ROOT, "sakura_core"),
        "-I", gen_dir,
        "-idirafter", MINGW_INC,
        "-Wno-everything", "-ferror-limit=100",
    ]
    if syntax_only:
        args += ["-fsyntax-only", src]
    else:
        args += ["-c", src, "-o", obj]
    return args


def compile_one(src, gen_dir, out_dir, syntax_only):
    rel = os.path.relpath(src, ROOT)
    obj = os.path.join(out_dir, "objs", rel + ".o")
    log = os.path.join(out_dir, "logs", rel + ".log")
    os.makedirs(os.path.dirname(obj), exist_ok=True)
    os.makedirs(os.path.dirname(log), exist_ok=True)
    p = subprocess.run(compiler_args(gen_dir, syntax_only, src, obj),
                       cwd=ROOT, capture_output=True, text=True)
    with open(log, "w") as f:
        f.write(p.stderr)
    return rel, p.returncode, p.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gen", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--jobs", type=int, default=os.cpu_count())
    ap.add_argument("--syntax-only", action="store_true")
    ap.add_argument("--filter", default="")
    a = ap.parse_args()

    srcs = []
    for d, _, fs in list(os.walk(os.path.join(ROOT, "sakura_core"))) + list(os.walk(os.path.join(ROOT, "src/main/cpp"))):
        if os.sep + "platform" in d:
            continue
        srcs += [os.path.join(d, f) for f in fs if f.endswith(".cpp") and a.filter in os.path.join(d, f)]
    srcs.sort()

    results = []
    with concurrent.futures.ThreadPoolExecutor(a.jobs) as ex:
        for r in ex.map(lambda s: compile_one(s, a.gen, a.out, a.syntax_only), srcs):
            results.append(r)

    ok = [r for r in results if r[1] == 0]
    ng = [r for r in results if r[1] != 0]
    msgs = collections.Counter()
    idents = collections.Counter()
    nofile = collections.Counter()
    where = collections.Counter()
    for rel, _, err in ng:
        seen = set()
        for m in RE_ERROR.finditer(err):
            msg = m.group("msg")
            key = re.sub(r"'[^']*'", "'X'", msg)
            msgs[key] += 1
            where[os.path.relpath(m.group("file"), ROOT) if m.group("file").startswith("/") else m.group("file")] += 1
            u = RE_UNDECL.search(msg)
            if u:
                name = next(g for g in u.groups() if g)
                if name not in seen:
                    idents[name] += 1
                    seen.add(name)
            n = RE_NOFILE.search(msg)
            if n:
                nofile[n.group(1)] += 1

    with open(os.path.join(a.out, "summary.md"), "w") as f:
        f.write(f"# winport probe\n\n- 対象: {len(results)} files\n- 成功: {len(ok)}\n- 失敗: {len(ng)}\n\n")
        f.write("## 欠落ヘッダ\n\n" + "".join(f"- {k}: {v}\n" for k, v in nofile.most_common()) + "\n")
        f.write("## エラー種別（上位 40）\n\n" + "".join(f"- {v} × {k}\n" for k, v in msgs.most_common(40)) + "\n")
        f.write("## 未宣言識別子（ファイル数順、上位 150）\n\n" + "".join(f"- {k}: {v}\n" for k, v in idents.most_common(150)) + "\n")
        f.write("## エラー発生位置（ファイル別、上位 40）\n\n" + "".join(f"- {k}: {v}\n" for k, v in where.most_common(40)) + "\n")
        f.write("## 失敗ファイル\n\n" + "".join(f"- {r[0]}\n" for r in ng))
    print(f"total={len(results)} ok={len(ok)} ng={len(ng)} -> {a.out}/summary.md")


if __name__ == "__main__":
    sys.exit(main())
