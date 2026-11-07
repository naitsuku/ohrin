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
codesign --force --deep --sign - "$stage/SakuraEditor.app"
ln -s /Applications "$stage/Applications"
rm -f "$dmg"
hdiutil create -volname "SakuraEditor $version" -srcfolder "$stage" -format UDZO -ov "$dmg" >/dev/null
echo "$dmg"
