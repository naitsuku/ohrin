#!/usr/bin/env python3
"""
analyze_win32_deps.py
sakura_core/ 配下の .cpp/.h を走査し、Win32 API 依存箇所を
カテゴリ別に集計して docs/porting/win32_dependency_analysis.md に出力する。

Phase 0 Task 0-2 の実装。

実行方法:
    python3 tools/porting/analyze_win32_deps.py

出力先:
    docs/porting/win32_dependency_analysis.md
"""

from __future__ import annotations

import re
import sys
from collections import defaultdict
from pathlib import Path

# ---------------------------------------------------------------------------
# Win32 API カテゴリ定義（phase0_prompt.md より）
# ---------------------------------------------------------------------------
CATEGORIES: dict[str, list[str]] = {
    "window_management": [
        "HWND", "CreateWindow", "WndProc", "DefWindowProc",
        "WM_CREATE", "WM_PAINT", "WM_COMMAND", "WM_DESTROY",
        "WM_SIZE", "WM_CLOSE", "WM_KEYDOWN", "WM_CHAR",
        "RegisterClass", "ShowWindow", "UpdateWindow",
        "GetDC", "ReleaseDC", "BeginPaint", "EndPaint",
    ],
    "ime": [
        "ImmGetContext", "ImmSetCompositionWindow", "ImmReleaseContext",
        "WM_IME_STARTCOMPOSITION", "WM_IME_COMPOSITION", "WM_IME_ENDCOMPOSITION",
        "ImmGetCompositionString", "ImmSetCandidateWindow",
    ],
    "file_io": [
        "CreateFile", "ReadFile", "WriteFile", "CloseHandle",
        "GetFileSize", "SetFilePointer", "FlushFileBuffers",
        "CreateDirectory", "DeleteFile", "MoveFile", "CopyFile",
    ],
    "registry": [
        "RegOpenKey", "RegCreateKey", "RegQueryValue", "RegSetValue",
        "RegDeleteKey", "RegDeleteValue", "RegCloseKey",
        "HKEY_CURRENT_USER", "HKEY_LOCAL_MACHINE",
    ],
    "ipc_shared_memory": [
        "CreateFileMapping", "MapViewOfFile", "OpenFileMapping",
        "UnmapViewOfFile", "DLLSHAREDATA",
    ],
    "gdi_drawing": [
        "HDC", "HFONT", "HBRUSH", "HPEN", "HBITMAP",
        "CreateFont", "SelectObject", "DeleteObject",
        "TextOut", "ExtTextOut", "DrawText",
        "BitBlt", "StretchBlt", "GetTextMetrics",
    ],
    "com_ole": [
        "CoCreateInstance", "CoInitialize", "IDispatch",
        "IUnknown", "BSTR", "VARIANT", "SAFEARRAY",
    ],
    "process_thread": [
        "CreateProcess", "CreateThread", "WaitForSingleObject",
        "CreateMutex", "CreateEvent", "OpenMutex",
    ],
    "clipboard": [
        "OpenClipboard", "CloseClipboard", "GetClipboardData",
        "SetClipboardData", "EmptyClipboard", "CF_TEXT", "CF_UNICODETEXT",
    ],
    "shell_integration": [
        "ShellExecute", "SHGetSpecialFolderPath", "SHBrowseForFolder",
        "GetOpenFileName", "GetSaveFileName",
    ],
}

# 移植難易度スコア（Phase 0 Task 0-2 の指定通り）
DIFFICULTY_WEIGHT: dict[str, int] = {
    "ime": 5,
    "ipc_shared_memory": 5,
    "gdi_drawing": 4,
    "window_management": 4,
    "com_ole": 3,
    "process_thread": 3,
    "file_io": 2,
    "registry": 2,
    "clipboard": 2,
    "shell_integration": 2,
}


def build_patterns() -> dict[str, re.Pattern[str]]:
    """カテゴリごとに 1 個の big-OR 正規表現を作る。
    \\b で単語境界。CF_TEXT のような識別子も末尾境界が効く。
    部分一致を避けるため、各キーワードは単語境界で囲む。
    """
    patterns: dict[str, re.Pattern[str]] = {}
    for cat, keywords in CATEGORIES.items():
        # 長いものから先にマッチさせる（CreateFile vs CreateFileMapping）
        kw_sorted = sorted(set(keywords), key=len, reverse=True)
        # \b はアンダースコアを単語文字として扱うので、識別子の境界として機能する
        alt = "|".join(re.escape(k) for k in kw_sorted)
        patterns[cat] = re.compile(rf"\b(?:{alt})\b")
    return patterns


def scan_file(path: Path, patterns: dict[str, re.Pattern[str]]) -> dict[str, list[tuple[int, str, str]]]:
    """1 ファイルを走査し、カテゴリ別に (line_no, keyword, line_text) を返す。"""
    out: dict[str, list[tuple[int, str, str]]] = defaultdict(list)
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return out
    for lineno, line in enumerate(text.splitlines(), start=1):
        # コメント行は完全に無視（簡易判定）
        stripped = line.lstrip()
        if stripped.startswith("//") or stripped.startswith("*"):
            continue
        for cat, pat in patterns.items():
            for m in pat.finditer(line):
                out[cat].append((lineno, m.group(0), line.strip()))
    return out


def main() -> int:
    repo_root = Path(__file__).resolve().parents[2]
    target_root = repo_root / "sakura_core"
    out_path = repo_root / "docs" / "porting" / "win32_dependency_analysis.md"

    if not target_root.is_dir():
        print(f"ERROR: {target_root} not found", file=sys.stderr)
        return 1

    patterns = build_patterns()

    # ファイル単位で集計
    per_file_hits: dict[Path, dict[str, list[tuple[int, str, str]]]] = {}
    for ext in ("*.cpp", "*.h"):
        for f in target_root.rglob(ext):
            hits = scan_file(f, patterns)
            if any(hits.values()):
                per_file_hits[f] = hits

    # カテゴリ別合計
    cat_file_count: dict[str, int] = defaultdict(int)
    cat_hit_count: dict[str, int] = defaultdict(int)
    for f, hits in per_file_hits.items():
        for cat, lst in hits.items():
            if lst:
                cat_file_count[cat] += 1
                cat_hit_count[cat] += len(lst)

    # 難易度スコア（ファイルごと）
    file_score: dict[Path, int] = {}
    for f, hits in per_file_hits.items():
        score = 0
        for cat, lst in hits.items():
            score += DIFFICULTY_WEIGHT.get(cat, 0) * len(lst)
        file_score[f] = score

    top20 = sorted(file_score.items(), key=lambda kv: kv[1], reverse=True)[:20]

    # ----- レポート出力 -----
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w", encoding="utf-8") as w:
        w.write("# Win32 API 依存度分析\n\n")
        w.write(f"対象: `sakura_core/` 配下の .cpp / .h\n\n")
        w.write(f"生成: `tools/porting/analyze_win32_deps.py`\n\n")

        # ===== サマリ =====
        w.write("## カテゴリ別サマリ\n\n")
        w.write("| カテゴリ | 影響ファイル数 | 総出現回数 | 難易度重み |\n")
        w.write("|---|---:|---:|---:|\n")
        total_files = len(per_file_hits)
        for cat in CATEGORIES:
            w.write(
                f"| {cat} | {cat_file_count[cat]} | "
                f"{cat_hit_count[cat]} | "
                f"{DIFFICULTY_WEIGHT.get(cat, 0)} |\n"
            )
        w.write(f"\n影響を受けるファイル総数: **{total_files}**\n\n")

        # ===== Top 20 難易度 =====
        w.write("## 移植難易度スコア Top 20\n\n")
        w.write("スコア = Σ(出現回数 × カテゴリ重み)。値が大きいほど移植が困難。\n\n")
        w.write("| 順位 | スコア | ファイル | 主な依存カテゴリ |\n")
        w.write("|---:|---:|---|---|\n")
        for rank, (f, sc) in enumerate(top20, start=1):
            rel = f.relative_to(repo_root)
            cats_in_file = sorted(
                ((c, len(per_file_hits[f][c])) for c in CATEGORIES if per_file_hits[f][c]),
                key=lambda kv: kv[1],
                reverse=True,
            )
            top_cats = ", ".join(f"{c}({n})" for c, n in cats_in_file[:3])
            w.write(f"| {rank} | {sc} | `{rel}` | {top_cats} |\n")
        w.write("\n")

        # ===== カテゴリ別詳細 =====
        w.write("## カテゴリ別ヒット詳細\n\n")
        for cat in CATEGORIES:
            w.write(f"### {cat} (重み {DIFFICULTY_WEIGHT.get(cat, 0)})\n\n")
            entries: list[tuple[Path, int, str, str]] = []
            for f, hits in per_file_hits.items():
                for lineno, kw, txt in hits.get(cat, []):
                    entries.append((f, lineno, kw, txt))
            if not entries:
                w.write("_該当なし_\n\n")
                continue
            entries.sort(key=lambda e: (str(e[0]), e[1]))
            # 多すぎる場合は最初の 30 件 + 残りファイル名のみ
            limit = 30
            for f, lineno, kw, txt in entries[:limit]:
                rel = f.relative_to(repo_root)
                w.write(f"- `{rel}:{lineno}` `{kw}` — `{txt[:120]}`\n")
            if len(entries) > limit:
                w.write(f"\n…他 {len(entries) - limit} 件（省略）\n")
            w.write("\n")

    print(f"Wrote {out_path}")
    print(f"Total files with Win32 deps: {total_files}")
    print(f"Top file: {top20[0][0].relative_to(repo_root)} (score={top20[0][1]})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
