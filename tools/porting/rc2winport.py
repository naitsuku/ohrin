#!/usr/bin/env python3
"""rc2winport.py — Windows リソーススクリプト（.rc）を winport 用の C++ リソース表に変換する。

上流コードはリソースを FindResource / LockResource で取り出し、Windows のバイナリ形式のまま
解析する（例: cxx::load_string は RT_STRING ブロックを直接読む）。そこで .rc をビルド時に
Windows と同じ形式のデータへ変換し、winport の FindResourceW 等から引けるようにする。

対応済みのリソース種別:
    STRINGTABLE … RT_STRING（16 文字列 1 ブロック、各文字列は「長さ + 本体」）
                  ※ 要素は WCHAR（winport では 4 バイト、中身は UTF-16 コード単位）
    ファイル参照 … `ID 型 "ファイル"`。独自型（数値）と RCDATA はファイルの中身そのまま、
                  BITMAP は BITMAPFILEHEADER（14 バイト）を除いた DIB（RT_BITMAP の形式）
                  ICON / CURSOR は未対応（グループ形式への変換が必要）
    DIALOG / DIALOGEX … RT_DIALOG（DLGTEMPLATE / DLGTEMPLATEEX。文字列は Windows と同じ 16 ビット）

手順:
    1. .rc（UTF-16LE）と、そこから #include される .rc / .rc2 を UTF-8 に変換して一時ディレクトリへ
    2. C プリプロセッサ（RC_INVOKED 定義）でマクロ ID を数値に展開
    3. リソース文を解析して C++ ソースを出力

使い方:
    python3 tools/porting/rc2winport.py --rc sakura_core/sakura_rc.rc --out <out.cpp>
        -I <include dir> ... [--mingw-include <dir>] [--symbol <配列名>]

SPDX-License-Identifier: Zlib
"""
import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

RT_STRING = 6
RT_BITMAP = 2
RT_RCDATA = 10
RT_DIALOG = 5
FILE_RESOURCE_TYPES = {"BITMAP": RT_BITMAP, "RCDATA": RT_RCDATA}
SKIPPED_TYPES = {"ICON", "CURSOR", "RT_MANIFEST", "24"}


def read_text(path):
    raw = open(path, "rb").read()
    if raw.startswith(b"\xff\xfe"):
        return raw[2:].decode("utf-16-le")
    if raw.startswith(b"\xfe\xff"):
        return raw[2:].decode("utf-16-be")
    if len(raw) > 1 and raw[1] == 0:
        return raw.decode("utf-16-le")
    if raw.startswith(b"\xef\xbb\xbf"):
        return raw[3:].decode("utf-8")
    return raw.decode("utf-8")


def stage_sources(rc_path, tmp):
    """rc と、同じディレクトリから #include される .rc/.rc2 を UTF-8 化して tmp に置く。"""
    src_dir = os.path.dirname(os.path.abspath(rc_path))
    pending = [os.path.basename(rc_path)]
    done = set()
    while pending:
        name = pending.pop()
        if name in done:
            continue
        done.add(name)
        text = read_text(os.path.join(src_dir, name)).replace("\r\n", "\n")
        with open(os.path.join(tmp, name), "w", encoding="utf-8") as f:
            f.write(text)
        for inc in re.findall(r'^\s*#\s*include\s+"([^"]+\.rc2?)"', text, re.M):
            if os.path.exists(os.path.join(src_dir, inc)):
                pending.append(inc)
    return os.path.join(tmp, os.path.basename(rc_path))


def preprocess(rc_utf8, includes, mingw_include):
    cmd = ["clang", "-E", "-P", "-x", "c", "-DRC_INVOKED", "-D_WIN32", "-D_WIN64", "-DSAKURA_WINPORT",
           "-DUNICODE", "-D_UNICODE", "-Wno-everything"]
    for inc in includes:
        cmd += ["-I", inc]
    if mingw_include:
        cmd += ["-idirafter", mingw_include]
    cmd.append(rc_utf8)
    p = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8")
    if p.returncode != 0:
        sys.stderr.write(p.stderr)
        raise SystemExit("preprocess failed")
    return p.stdout


RE_STRING_LIT = re.compile(r'L?"((?:[^"]|"")*)"')


def unescape_rc(s):
    """RC 文字列リテラルのエスケープ（"" と \\n \\t \\\\ \\x..）を解く。"""
    s = s.replace('""', '"')
    out = []
    i = 0
    while i < len(s):
        c = s[i]
        if c == "\\" and i + 1 < len(s):
            n = s[i + 1]
            if n == "n":
                out.append("\n"); i += 2; continue
            if n == "t":
                out.append("\t"); i += 2; continue
            if n == "r":
                out.append("\r"); i += 2; continue
            if n == "a":
                out.append("\a"); i += 2; continue
            if n == "\\":
                out.append("\\"); i += 2; continue
            if n == "0":
                out.append("\0"); i += 2; continue
            if n in "xX":
                m = re.match(r"[0-9a-fA-F]{1,4}", s[i + 2:])
                if m:
                    out.append(chr(int(m.group(0), 16))); i += 2 + len(m.group(0)); continue
        out.append(c)
        i += 1
    return "".join(out)


def eval_id(expr):
    expr = expr.strip()
    if not re.fullmatch(r"[0-9xXa-fA-F\s()+\-*|<>uUlL]+", expr):
        raise ValueError(f"unresolved resource id: {expr!r}")
    expr = re.sub(r"(?<=[0-9a-fA-F])[uUlL]+", "", expr)
    return int(eval(expr, {"__builtins__": {}}))


def parse_stringtables(text):
    """STRINGTABLE を解析する。ID と文字列が別の行に分かれる書き方、隣接リテラルの連結にも対応。"""
    strings = {}
    for m in re.finditer(r"\bSTRINGTABLE\b[^\n]*\n\s*(?:BEGIN|\{)(.*?)^\s*(?:END|\})", text, re.S | re.M):
        body = m.group(1)
        pos = 0
        last_id = None
        for lit in RE_STRING_LIT.finditer(body):
            ident = body[pos:lit.start()].strip().strip(",").strip()
            pos = lit.end()
            value = unescape_rc(lit.group(1))
            if ident:
                last_id = eval_id(ident)
                strings[last_id] = value
            elif last_id is not None:
                strings[last_id] += value
    return strings


RE_FILE_RES = re.compile(
    r'^\s*([0-9xXa-fA-F()+\-*| ]+?|[A-Za-z_]\w*)\s+([A-Za-z_]\w*|[0-9]+)\s+'
    r'(?:(?:DISCARDABLE|MOVEABLE|PURE|LOADONCALL|PRELOAD|FIXED|IMPURE)\s+)*"([^"]+)"\s*$', re.M)


def parse_file_resources(text, rc_dir):
    """`ID 型 "ファイル"` 形式のリソースを [(型, ID, データ)] にする。"""
    out = []
    skipped = []
    for m in RE_FILE_RES.finditer(text):
        ident, rtype, path = m.group(1).strip(), m.group(2), m.group(3)
        if rtype in SKIPPED_TYPES:
            skipped.append(f"{rtype} {path}")
            continue
        if rtype in FILE_RESOURCE_TYPES:
            type_id = FILE_RESOURCE_TYPES[rtype]
        elif rtype.isdigit():
            type_id = int(rtype)
        else:
            continue  # 文字列表やダイアログ内の行（リソース文ではない）
        try:
            res_id = eval_id(ident)
        except ValueError:
            continue
        full = os.path.normpath(os.path.join(rc_dir, path.replace("\\\\", "/").replace("\\", "/")))
        data = open(full, "rb").read()
        if type_id == RT_BITMAP and data[:2] == b"BM":
            data = data[14:]
        out.append((type_id, res_id, data))
    return out, skipped


def utf16_units(s):
    b = s.encode("utf-16-le")
    return [int.from_bytes(b[i:i + 2], "little") for i in range(0, len(b), 2)]


def build_string_blocks(strings):
    blocks = {}
    for sid, s in strings.items():
        blocks.setdefault((sid >> 4) + 1, {})[sid & 0xF] = s
    out = {}
    for block, entries in blocks.items():
        data = []
        for i in range(16):
            u = utf16_units(entries.get(i, ""))
            data.append(len(u))
            data += u
        out[block] = data
    return out


# ---- ダイアログ ----
WS_CHILD, WS_VISIBLE, WS_TABSTOP, WS_GROUP, WS_BORDER = 0x40000000, 0x10000000, 0x00010000, 0x00020000, 0x00800000
# 文 → (クラス, 既定スタイル)
DIALOG_CONTROLS = {
    "LTEXT": ("STATIC", WS_GROUP | 0x0), "CTEXT": ("STATIC", WS_GROUP | 0x1), "RTEXT": ("STATIC", WS_GROUP | 0x2),
    "PUSHBUTTON": ("BUTTON", WS_TABSTOP | 0x0), "DEFPUSHBUTTON": ("BUTTON", WS_TABSTOP | 0x1),
    "CHECKBOX": ("BUTTON", WS_TABSTOP | 0x2), "AUTOCHECKBOX": ("BUTTON", WS_TABSTOP | 0x3),
    "RADIOBUTTON": ("BUTTON", WS_TABSTOP | 0x4), "AUTORADIOBUTTON": ("BUTTON", WS_TABSTOP | 0x9),
    "GROUPBOX": ("BUTTON", 0x7),
    "EDITTEXT": ("EDIT", WS_TABSTOP | WS_BORDER), "COMBOBOX": ("COMBOBOX", WS_TABSTOP | 0x1),
    "LISTBOX": ("LISTBOX", WS_TABSTOP | WS_BORDER | 0x1), "SCROLLBAR": ("SCROLLBAR", 0),
    "ICON": ("STATIC", 0x3), "CONTROL": (None, 0),
}
NO_TEXT_CONTROLS = {"EDITTEXT", "COMBOBOX", "LISTBOX", "SCROLLBAR"}


def split_args(s):
    """カンマで分ける（文字列の中は無視）。"""
    args, cur, in_str = [], "", False
    for ch in s:
        if ch == '"':
            in_str = not in_str
        if ch == "," and not in_str:
            args.append(cur.strip())
            cur = ""
        else:
            cur += ch
    args.append(cur.strip())
    return args


def eval_style(expr, base=0):
    """`A | B | NOT C` を左から評価する。"""
    value = base
    for term in expr.split("|"):
        term = term.strip()
        if not term:
            continue
        m = re.match(r"NOT\s+(.*)", term)
        if m:
            value &= ~eval_id(m.group(1))
        else:
            value |= eval_id(term)
    return value & 0xFFFFFFFF


def rc_string(s):
    m = RE_STRING_LIT.fullmatch(s.strip())
    return unescape_rc(m.group(1)) if m else None


def parse_control(keyword, args):
    cls, style = DIALOG_CONTROLS[keyword]
    style |= WS_CHILD | WS_VISIBLE
    ex = 0
    text = ""
    if keyword == "CONTROL":
        text = rc_string(args[0])
        if text is None:
            text = eval_id(args[0])
        cid = eval_id(args[1])
        cls = rc_string(args[2])
        style = eval_style(args[3], style)
        x, y, w, h = (eval_id(a) for a in args[4:8])
        if len(args) > 8:
            ex = eval_style(args[8])
    else:
        if keyword not in NO_TEXT_CONTROLS:
            text = rc_string(args[0])
            if text is None:
                text = eval_id(args[0])
            args = args[1:]
        cid = eval_id(args[0])
        pos = [eval_id(a) for a in args[1:5]]
        x, y = pos[0], pos[1]
        w, h = (pos[2], pos[3]) if len(pos) >= 4 else (0, 0)
        if len(args) > 5:
            # rc.exe はスタイルが指定されると既定の CBS_SIMPLE を使わない（種別のビットが混ざるため）
            style = eval_style(args[5], style & ~0x3 if keyword == "COMBOBOX" else style)
        if len(args) > 6:
            ex = eval_style(args[6])
    return dict(cls=cls, text=text, id=cid & 0xFFFF, style=style, ex=ex, rect=(x, y, w, h))


def parse_dialogs(text):
    """[(id, dict)] を返す。"""
    dialogs = []
    lines = text.split("\n")
    i = 0
    while i < len(lines):
        m = re.match(r"\s*(.+?)\s+DIALOG(EX)?\s+(.*)$", lines[i])
        i += 1
        if not m or lines[i - 1].lstrip().startswith('"'):
            continue
        rect = [eval_id(a) for a in split_args(m.group(3))[:4]]
        d = dict(id=eval_id(m.group(1)), ex=bool(m.group(2)), rect=rect, style=0x80000000 | 0x00C00000 | 0x00080000, exstyle=0,
                 caption="", font=None, controls=[])
        while lines[i].strip() not in ("BEGIN", "{"):
            l = lines[i].strip()
            i += 1
            w = l.split(None, 1)
            if not w:
                continue
            arg = w[1] if len(w) > 1 else ""
            if w[0] == "STYLE":
                d["style"] = eval_style(arg)
            elif w[0] == "EXSTYLE":
                d["exstyle"] = eval_style(arg)
            elif w[0] == "CAPTION":
                d["caption"] = rc_string(arg)
            elif w[0] == "FONT":
                a = split_args(arg)
                d["font"] = (eval_id(a[0]), rc_string(a[1]))
        i += 1
        stmts = []
        while lines[i].strip() not in ("END", "}"):
            l = lines[i].strip()
            i += 1
            if not l:
                continue
            w = l.split(None, 1)
            if w[0] in DIALOG_CONTROLS:
                stmts.append([w[0], w[1] if len(w) > 1 else ""])
            elif stmts:
                stmts[-1][1] += " " + l
        i += 1
        for keyword, arg in stmts:
            d["controls"].append(parse_control(keyword, split_args(arg)))
        dialogs.append(d)
    return dialogs


def build_dialog(d):
    """Windows の RT_DIALOG と同じ形式（文字列は 16 ビット）にする。"""
    def sz(s):
        return b"".join(struct.pack("<H", u) for u in utf16_units(s)) + b"\0\0"

    def align(b):
        return b + b"\0" * (-len(b) % 4)

    style = d["style"]
    x, y, w, h = d["rect"]
    n = len(d["controls"])
    if d["ex"]:
        out = struct.pack("<HHIIIHhhhh", 1, 0xFFFF, 0, d["exstyle"], style, n, x, y, w, h)
    else:
        out = struct.pack("<IIHhhhh", style, d["exstyle"], n, x, y, w, h)
    out += b"\0\0" + b"\0\0" + sz(d["caption"])  # メニュー、クラス、タイトル
    if style & 0x40:  # DS_SETFONT
        pt, name = d["font"] or (9, "")
        out += struct.pack("<H", pt)
        if d["ex"]:
            out += struct.pack("<HBB", 0, 0, 1)
        out += sz(name)
    for c in d["controls"]:
        out = align(out)
        cx, cy, cw, ch = c["rect"]
        if d["ex"]:
            out += struct.pack("<IIIhhhhI", 0, c["ex"], c["style"], cx, cy, cw, ch, c["id"])
        else:
            out += struct.pack("<IIhhhhH", c["style"], c["ex"], cx, cy, cw, ch, c["id"])
        out += sz(c["cls"])
        out += struct.pack("<HH", 0xFFFF, c["text"] & 0xFFFF) if isinstance(c["text"], int) else sz(c["text"])
        out += b"\0\0"  # 追加データなし
    return out


def emit(out_path, symbol, string_blocks, file_resources, rc_name):
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(f"// {os.path.basename(out_path)} — {rc_name} から rc2winport.py が生成。編集しないこと。\n")
        f.write('#include "winport_resource.h"\n\n')
        f.write("namespace {\n")
        for block, data in sorted(string_blocks.items()):
            vals = ",".join(f"0x{v:X}" for v in data)
            f.write(f"const wchar_t s_str{block}[] = {{{vals}}};\n")
        for i, (_, _, data) in enumerate(file_resources):
            f.write(f"alignas(8) const unsigned char s_bin{i}[] = {{")
            for j in range(0, len(data), 32):
                f.write("\n\t" + ",".join(str(b) for b in data[j:j + 32]) + ",")
            f.write("0};\n")  # 末尾の 0 は終端用（サイズには含めない）
        f.write("const winport::ResourceEntry s_entries[] = {\n")
        for block in sorted(string_blocks):
            f.write(f"\t{{ {RT_STRING}, {block}, s_str{block}, sizeof(s_str{block}) }},\n")
        for i, (type_id, res_id, data) in enumerate(file_resources):
            f.write(f"\t{{ {type_id}, {res_id}, s_bin{i}, {len(data)} }},\n")
        f.write("};\n}  // namespace\n\n")
        f.write(f"extern const winport::ResourceTable {symbol} = {{ s_entries, sizeof(s_entries) / sizeof(s_entries[0]) }};\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rc", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("-I", dest="includes", action="append", default=[])
    ap.add_argument("--mingw-include")
    ap.add_argument("--symbol", default="g_winportResources")
    a = ap.parse_args()

    tmp = tempfile.mkdtemp(prefix="rc2winport-")
    try:
        rc_utf8 = stage_sources(a.rc, tmp)
        text = preprocess(rc_utf8, [tmp] + a.includes, a.mingw_include)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    strings = parse_stringtables(text)
    blocks = build_string_blocks(strings)
    files, skipped = parse_file_resources(text, os.path.dirname(os.path.abspath(a.rc)))
    dialogs = parse_dialogs(text)
    files += [(RT_DIALOG, d["id"], build_dialog(d)) for d in dialogs]
    emit(a.out, a.symbol, blocks, files, os.path.basename(a.rc))
    print(f"{a.rc}: {len(strings)} strings / {len(blocks)} RT_STRING blocks / {len(files)} file resources "
          f"({len(dialogs)} dialogs, {len(skipped)} skipped) -> {a.out}")


if __name__ == "__main__":
    main()
