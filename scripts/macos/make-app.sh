#!/bin/bash
# Builds LastChaos.app, a macOS application around scripts/macos/launcher.sh, to open from the
# Finder or the Dock. The app is "thin": it runs this repository's launcher, which uses the
# builds and servers here and the game installs in ~/Projects as they are (HANDOFF.md, "Fase F").
#
#   scripts/macos/make-app.sh [output directory]     default build-app/ in this repository
#
# The app's own output (the launcher's, the servers' start) goes to
# ~/Library/Logs/LastChaos/launcher.log; the client still writes stdout.log in its install.
# Signed ad hoc (codesign -s -), which is enough to run it on this Mac.
set -euo pipefail

HERE="$(cd "$(dirname "$0")/../.." && pwd)"
OUT=${1:-$HERE/build-app}
APP=$OUT/LastChaos.app
ICON="$HERE/v2015/Client Source/Nksp/res/LC.ico"   # the Windows LC.exe's (Last Chaos Brasil launcher)
VERSION=$(git -C "$HERE" describe --always --dirty 2>/dev/null || echo 0)

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"

# The executable. An app opened by the Finder gets a short PATH (no Homebrew) and no terminal.
# The client it ends in (Bin/Nksp, not inside the app) takes the app's Dock icon by LC_APP_ICON.
cat > "$APP/Contents/MacOS/LastChaos" <<EOF
#!/bin/bash
export PATH=/opt/homebrew/bin:/opt/homebrew/sbin:/usr/local/bin:\$PATH
export LC_APP_ICON="\$(cd "\$(dirname "\$0")/../Resources" && pwd)/LastChaos.icns"
LOGS=\$HOME/Library/Logs/LastChaos
mkdir -p "\$LOGS"
exec "$HERE/scripts/macos/launcher.sh" >> "\$LOGS/launcher.log" 2>&1
EOF
chmod +x "$APP/Contents/MacOS/LastChaos"

# The icon: the official one from Windows (48x48 down to 16x16), scaled into an iconset.
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
sips -s format png "$ICON" --out "$tmp/icon.png" >/dev/null
mkdir "$tmp/LastChaos.iconset"
for size in 16 32 128 256 512; do
  sips -z $size $size "$tmp/icon.png" --out "$tmp/LastChaos.iconset/icon_${size}x${size}.png" >/dev/null
  sips -z $((size * 2)) $((size * 2)) "$tmp/icon.png" --out "$tmp/LastChaos.iconset/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$tmp/LastChaos.iconset" -o "$APP/Contents/Resources/LastChaos.icns"

cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleName</key>
	<string>Last Chaos</string>
	<key>CFBundleDisplayName</key>
	<string>Last Chaos</string>
	<key>CFBundleIdentifier</key>
	<string>com.patrickschifter.lastchaos</string>
	<key>CFBundleExecutable</key>
	<string>LastChaos</string>
	<key>CFBundleIconFile</key>
	<string>LastChaos</string>
	<key>CFBundlePackageType</key>
	<string>APPL</string>
	<key>CFBundleShortVersionString</key>
	<string>1.0</string>
	<key>CFBundleVersion</key>
	<string>$VERSION</string>
	<key>LSMinimumSystemVersion</key>
	<string>13.0</string>
	<key>LSApplicationCategoryType</key>
	<string>public.app-category.role-playing-games</string>
	<key>NSHighResolutionCapable</key>
	<true/>
</dict>
</plist>
EOF

codesign --force -s - "$APP"
echo "$APP"
