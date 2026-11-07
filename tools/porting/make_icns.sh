#!/bin/bash
# make_icns.sh <入力.ico> <出力.icns> — 上流のアイコンを macOS のアイコンにする
# 上流の .ico は 32x32 までしかないので、大きいサイズは拡大したものになる
set -euo pipefail
src=$1
out=$2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir "$work/sakura.iconset"
sips -s format png "$src" --out "$work/base.png" >/dev/null
for size in 16 32 128 256 512; do
	sips -z "$size" "$size" "$work/base.png" --out "$work/sakura.iconset/icon_${size}x${size}.png" >/dev/null
	double=$((size * 2))
	sips -z "$double" "$double" "$work/base.png" --out "$work/sakura.iconset/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$work/sakura.iconset" -o "$out"
