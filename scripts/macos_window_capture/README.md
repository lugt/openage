# macOS window capture and input helper

`OpenageWindowCapture.app` is a small, separately signed command-line app for
capturing and clicking the real openage window. It uses ScreenCaptureKit and
ApplicationServices on macOS 14 or later. Screen Recording and Accessibility
can therefore be granted to this helper instead of the terminal or openage.

## Build

Create the local signing identity once, then build:

```sh
bash scripts/macos_window_capture/create_signing_identity.sh
bash scripts/macos_window_capture/build.sh
```

The build uses only Xcode's system frameworks and a self-signed certificate in
the login keychain. Its designated requirement combines the stable bundle
identifier `dev.openage.windowcapture` with the certificate hash, so rebuilding
the binary does not invalidate TCC grants. Do not use ad-hoc signing here: its
designated requirement is the binary CDHash, which changes on every rebuild.
The private key is not stored in the repository.

When migrating an older ad-hoc-signed helper, clear its stale grants once and
then authorize the stable identity again:

```sh
tccutil reset ScreenCapture dev.openage.windowcapture
tccutil reset Accessibility dev.openage.windowcapture
```

## LaunchServices and output

Launch the app through LaunchServices so macOS attributes permissions to
`OpenageWindowCapture.app`, rather than to the responsible terminal process.
The `-o`/`--stderr` options preserve output without `open -W` (which can race
with these short-lived commands):

```sh
APP="$PWD/scripts/macos_window_capture/OpenageWindowCapture.app"
LOG="$PWD/logs/window_helper.log"
open -n -a "$APP" -o "$LOG" --stderr "$LOG" --args permission
```

Running `Contents/MacOS/OpenageWindowCapture` directly may still be attributed
to the terminal by TCC, even when the app itself has been authorized.

## Authorize

Request Screen Recording, then enable **OpenageWindowCapture** under
**System Settings → Privacy & Security → Screen Recording**:

```sh
open -n -a "$APP" -o "$LOG" --stderr "$LOG" --args permission --request
# Expected after authorization: SCREEN_RECORDING_GRANTED
```

For synthetic mouse input, separately request Accessibility and enable the app
under **System Settings → Privacy & Security → Accessibility**:

```sh
open -n -a "$APP" -o "$LOG" --stderr "$LOG" --args accessibility --request
# Expected after authorization: ACCESSIBILITY_GRANTED
```

## Capture openage

Start the game with `scripts/run_ww_mission1.sh`, then run these commands from
the repository root:

```sh
open -n -a "$APP" -o "$LOG" --stderr "$LOG" --args list --owner run
open -n -a "$APP" -o "$LOG" --stderr "$LOG" --args \
  capture --owner run "$PWD/logs/ww_mission1.png"
```

The output path must be absolute. The capture command returns exit code 3 when
Screen Recording is not granted, 4 when no matching window exists, and 5 for a
capture or file-writing error. It never reports success without writing a PNG.

## Click openage

Coordinates are in logical window points relative to the bounds printed by
`list`, including the title-bar area. The helper activates the matching window
and refuses coordinates outside its bounds:

```sh
open -n -a "$APP" -o "$LOG" --stderr "$LOG" --args \
  click --owner run 320 450 left
open -n -a "$APP" -o "$LOG" --stderr "$LOG" --args \
  click --owner run 640 400 right
```

Clicking returns exit code 6 when Accessibility is not granted, 4 when no
window matches, 2 for invalid/out-of-bounds arguments, and 5 for an event
creation or window enumeration failure. A `CLICK_POSTED` line confirms that
events were posted; use game logs and a screenshot to verify their effects.
