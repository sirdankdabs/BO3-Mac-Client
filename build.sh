#!/bin/bash
# Builds "BO3 Mac Client.app" into ./dist (engine dylib + SwiftUI launcher).
set -euo pipefail
cd "$(dirname "$0")"
ROOT="$(pwd)"
APP="dist/BO3 Mac Client.app"
BUILD="build"

make -C engine
rm -rf "$APP" "$BUILD"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources" "$BUILD"

# Launcher (universal)
for arch in arm64 x86_64; do
  swiftc -O -swift-version 5 -target "$arch-apple-macos14.0" \
    -file-prefix-map "$ROOT=." \
    app/Sources/*.swift -o "$BUILD/BO3MacClient-$arch"
done
lipo -create "$BUILD"/BO3MacClient-* -output "$APP/Contents/MacOS/BO3MacClient"
strip -x "$APP/Contents/MacOS/BO3MacClient"

# Engine (x86_64, the game runs under Rosetta)
cp engine/BO3MacFix.dylib "$APP/Contents/Resources/BO3MacFix.dylib"
strip -x "$APP/Contents/Resources/BO3MacFix.dylib"

# Icon
swift app/Resources/make_icon.swift "$BUILD/icon.png"
ICONSET="$BUILD/AppIcon.iconset"; mkdir -p "$ICONSET"
for s in 16 32 128 256 512; do
  sips -z $s $s "$BUILD/icon.png" --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
  sips -z $((s*2)) $((s*2)) "$BUILD/icon.png" --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/AppIcon.icns"

cp app/Resources/Info.plist "$APP/Contents/Info.plist"
cp engine/LICENSE.txt "$APP/Contents/Resources/LICENSE.txt"
codesign --force --deep --sign - "$APP"
echo "Built $APP"
