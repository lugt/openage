#!/usr/bin/env bash
# Build and sign the helper with a stable local identity so macOS TCC keeps
# Screen Recording and Accessibility grants across binary rebuilds. Ad-hoc
# signing is deliberately not used: its designated requirement is the CDHash,
# which changes on every build and silently invalidates existing grants.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP="$HERE/OpenageWindowCapture.app"
BINARY="$APP/Contents/MacOS/OpenageWindowCapture"
SIGNING_IDENTITY="${OPENAGE_CAPTURE_SIGNING_IDENTITY:-Openage Window Capture Local}"

if ! security find-identity -v -p codesigning | grep -F "\"$SIGNING_IDENTITY\"" >/dev/null; then
	printf 'Missing stable code-signing identity: %s\n' "$SIGNING_IDENTITY" >&2
	printf 'Run scripts/macos_window_capture/create_signing_identity.sh first.\n' >&2
	exit 1
fi

mkdir -p "$APP/Contents/MacOS"
xcrun clang \
	-x objective-c -fobjc-arc \
	-std=c11 -Wall -Wextra -Werror \
	"$HERE/window_capture.c" \
	-framework ApplicationServices \
	-framework AppKit \
	-framework CoreFoundation \
	-framework CoreGraphics \
	-framework Foundation \
	-framework ImageIO \
	-framework ScreenCaptureKit \
	-o "$BINARY"
cp "$HERE/Info.plist" "$APP/Contents/Info.plist"
codesign --force --sign "$SIGNING_IDENTITY" \
	--timestamp=none \
	--identifier dev.openage.windowcapture \
	"$APP"
printf 'Built and signed with stable identity %s: %s\n' "$SIGNING_IDENTITY" "$BINARY"
