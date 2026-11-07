#!/bin/bash
# make_dmg.sh [ビルドディレクトリ] — Release ビルドから配布用の DMG を作る
#
# 署名はアドホックのみ。公証していないので、他の Mac で開くと Gatekeeper の確認が出る。
# 配布するには Developer ID での署名と notarytool が別途必要。
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
build=${1:-$root/build-release}
cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$build"
ctest --test-dir "$build" --output-on-failure

app=$build/bin/SakuraEditor.app
version=$(/usr/libexec/PlistBuddy -c "Print CFBundleShortVersionString" "$app/Contents/Info.plist")
dmg=$build/SakuraEditor-$version.dmg
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT

cp -R "$app" "$stage/SakuraEditor.app"
# ライセンス（zlib と、Onigmo の BSD・bregonig の Artistic は配布物に全文を付ける）。署名の前にアプリの中へ入れる
for f in LICENSE THIRD_PARTY_NOTICES.txt; do
    cp "$root/$f" "$stage/SakuraEditor.app/Contents/Resources/"
    cp "$root/$f" "$stage/"
done
cp "$root/README.md" "$stage/README.md"
codesign --force --deep --sign - "$stage/SakuraEditor.app"
ln -s /Applications "$stage/Applications"
rm -f "$dmg"
hdiutil create -volname "Sakura Editor for Mac $version" -srcfolder "$stage" -format UDZO -ov "$dmg" >/dev/null
echo "$dmg"
