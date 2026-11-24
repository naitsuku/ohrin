#!/bin/bash
# make_icns.sh <入力画像> <出力.icns> — 1 枚の画像から macOS のアイコン（16〜1024px）を作る
# 入力は 1024x1024 の PNG を想定（sakura_mac/resources/AppIcon.png）。sips が読める形式なら .ico なども可
set -euo pipefail
src=$1
out=$2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir "$work/AppIcon.iconset"
sips -s format png "$src" --out "$work/base.png" >/dev/null
for size in 16 32 128 256 512; do
	sips -z "$size" "$size" "$work/base.png" --out "$work/AppIcon.iconset/icon_${size}x${size}.png" >/dev/null
	double=$((size * 2))
	sips -z "$double" "$double" "$work/base.png" --out "$work/AppIcon.iconset/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$work/AppIcon.iconset" -o "$out"
