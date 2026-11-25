# Desktop launcher

On macOS, double-click `launch.command` in the repository root, or run `bash launch.command`.
The script selects an existing Python environment with Tk support; the launcher adds no package dependencies.
On other desktops, run `python scripts/launcher.py` using a Python environment with Tk support.
The UI opens independently of compiled engine modules, so it can also launch a build.
Without saved preferences, the initially selected game is Wallace Mission 1 (Marching and Fighting).
See [building](building.md) for compiler, library and Python requirements.

## Shared controls

The launcher has Game, Tests and Converter tabs, with command preview, live output, stdin input, a stop button and a build-only button.
Execution uses argument lists without a shell, including when paths contain spaces.
The default executable is `bin/run` (`bin/run.exe` on Windows); the build directory defaults to `bin`.
Building before launch is not enabled by default; enable the build checkbox to run `cmake --build` before the selected task.
Failed build or test commands stop the remaining command sequence and preserve the failing exit code.
Stop terminates the launch's process group on POSIX and uses `taskkill /T /F` on Windows; it does not target other engine processes.

Runtime settings are read when Run is pressed, so changing fields during an active task affects the next run.
Save Settings writes `logs/launcher/settings.json`; settings are not saved automatically.
Pass `--settings PATH` to use another preferences file.
Complete per-run logs are saved under `logs/launcher/`, while the on-screen log retains at most 3000 lines.
The log folder button opens that directory.
For an interactive converter prompt, type a response in the input field and send it.

## Game and Wallace

Project Default invokes `bin/run main --no-convert --no-prompts` with the chosen window size/mode, VSync, OpenGL debugging, headless mode and modpacks.
Blank modpacks use the existing engine entry's selection behavior.
Window headless mode belongs to the existing game entry and is distinct from the [deterministic CPU snapshot tests](code/snapshots.md).
Enable “自动验证: 仅响应 Esc” to pass `--ignore-user-input`: mouse buttons/movement, wheel, key presses/releases, text input and touch/tablet events are consumed before gameplay and QML receive them.
Escape requests a normal window close; window resize and OS close events still work, and other applications keep receiving input normally.
Ordinary manual launches leave this switch off.
The Wallace startup verification script enables it automatically.

The Wallace choices reuse `scripts/campaign_launcher.py` and `scripts/run_ww_mission1.sh`:

- Marching and Fighting prepares and launches the existing first mission.
- Feeding the Army prepares and launches the existing second mission.
- Original Terrain Preview uses a local DE2 mission save and terrain texture directory, supplied explicitly or located by the existing campaign helper.
- Installed Wallace Scenario launches the currently installed campaign assets without preparing a different mission.

Selecting the first three choices invokes the existing installer, which updates `assets/converted/ww_mvp`.
Original scene export and installation retain the existing campaign helper's source-detection behavior and prerequisites.
The helper currently detects the macOS Steam location; alternative original-save/terrain paths can be supplied for the preview.
Only the existing first two playable mission choices are exposed; this does not implement the remaining original campaign missions.
Wallace scripts require Bash, the repository's native build and existing converted assets; the launcher rejects incompatible custom executable/asset/config paths instead of silently ignoring them.
They launch the windowed path, and headless regression belongs in the Tests tab.
The existing crash-marker audit remains active and window parameters are forwarded to the same game entry.

## Tests

The selectable suites are:

- Combined: registered automatic tests followed by the existing Wallace regression matrix.
- Automatic: `bin/run test --run-all-tests`, optionally including asset-dependent tests with `--have-assets`.
- Text Snapshots: `bin/run test testing.snapshot.headless_regression`.
- Wallace Headless Regression: `bash scripts/p4_4d_regression.sh --regression`.
- Wallace Startup Verification: `bash scripts/verify_ww_mission1.sh`, including a real window startup stage.
- Named Test/Prefix: pass a registered test name or prefix.
- Simulation Demo: run `gamestate.tests.simulation_demo` with the chosen ID.
- List Tests: show registered tests and demos in the log.

Combined covers the registered automatic suite and the campaign script's existing matrix; it does not discover arbitrary unregistered test files or interactive demos.
Wallace regressions require converted game assets and the currently installed campaign scene.
Updating golden files is an explicit checkbox and is only allowed for automatic, snapshot or named-test modes.
See [testing](code/testing.md) for registration, names and asset gating.

## Converter

Select the original game installation directory and an output asset root; the converter writes beneath its `converted/` subdirectory.
The launcher passes `convert --force --no-prompts` so pressing Run actually performs conversion rather than only displaying a prompt.
It exposes worker count, PNG compression, low-memory mode, skipping media/sound/graphics and nyan API export.
Conversion can update existing output files; it is never invoked implicitly by Game or Tests.
The shared asset/config directory fields do not apply to conversion, which uses its own source/output fields and the converter's normal config lookup.
See [asset conversion](media_convert.md) for supported original games and source requirements.

## Reusing converted campaign assets

Repeated launches of the same Wallace mission reuse its prepared files instead of running the installer again.
`assets/converted/.campaign-preparation.json` records the selected arguments, converter/template versions, scene/catalog inputs, relevant original graphics and generated outputs.
SHA-256 hashes identify file contents; unchanged size, nanosecond modification time and change time reuse the recorded hash rather than rereading large assets.
A changed input, missing/damaged output or different mission triggers preparation again.
The first launch after this change establishes the cache and may convert existing media once.

Terrain and original-unit graphics also have individual `.export-cache.json` stamps, so preparation can reuse unchanged media across mission changes.
Original campaign preparation also exports unit/building portraits, technology icons and supported command icons into the campaign's `hud/` directory.
The runtime uses subject IDs to locate these preconverted PNGs; it does not read the original DAT or decode DDS icons while playing.
Icon exports reuse source/version/output hashes, and the campaign cache includes the source UI images and technology catalog.
Missing optional icons retain text labels instead of substituting an unrelated unit or building image.
A media stamp checks the source content, exporter version and every PNG/descriptor output.
Deleting the campaign preparation manifest forces preparation again without removing those reusable media files.
These caches live with ignored converted assets and are not game saves or snapshot golden files.

The [minimap component](../assets/qml/Minimap.qml) retains its previous decoded image while loading the next one and coalesces intermediate updates.
This avoids displaying an empty image during changing PNG URLs and keeps image-cache memory bounded to the two active buffers.
