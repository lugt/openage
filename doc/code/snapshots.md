# Text snapshot regression tests

Run `bin/run test testing.snapshot.headless_regression` after [building](../building.md).
Run the same command with `--update-golden` to replace `tests/golden/headless_{state,render}.golden` deliberately, then review the text changes.
The [test runner](testing.md) stops at the first snapshot mismatch and reports the case, line, sample Tick, Expected/Actual, and at most three surrounding lines on each side without a traceback.
Baselines use UTF-8 bytes, Git attributes enforce LF endings, and comparison includes the final newline.

## CPU scenario

`tests/golden/headless.json` contains a seed, rectangular synthetic map bounds, players, initial units and commands.
`openage.gamestate.command_source` exports `ICommandSource::poll_commands(tick)` and `JsonScenarioCommandSource`.
Ticks are absolute sample indices starting at 1; commands at the same Tick retain JSON array order, and polling is independent of a playback cursor.
A new source with the same script can therefore resume at the saved Tick without replaying earlier commands.
Supported commands are `move`, `stop`, `damage`, `spawn` and `add_resource`; field names are demonstrated in the fixture.
Missing targets, duplicate IDs, invalid numbers and unsupported operations raise errors.
Movement speed is in world units per sample.
Numeric scenario values are nonnegative, have at most two decimal places and do not exceed 1000000; map dimensions are in `[1,10000]`.
IDs, seeds and Ticks are nonnegative JSON integers; command Ticks must be positive.

`openage.gamestate.state_snapshot` exports `HeadlessWorld` with `from_scenario`, `step_ticks`, `serialize` and `deserialize`.
The driver schedules an `EventLoop` event for each sample at a fixed-point interval of 0.25 seconds; this does not change the engine's [event-driven simulation](game_simulation/README.md).
It creates real `GameState`, `GameEntity`, `Position`, `Ownership`, `Live` and `Player` objects without a Presenter, window, audio device or converted game assets.
A tiny nyan ability is loaded from memory; synthetic health and stockpiles use integer hundredths, movement uses integer arithmetic and positions are stored in `coord::phys_t` curves.
The synthetic `damage` command includes a seeded critical roll to exercise RNG restoration.
State output queries those components and stockpiles, sorts player/entity IDs and formats numbers without locale-dependent floating-point output.

The text stores the current Tick, map bounds, RNG state, next entity ID, resources, entities and active movement targets/speeds.
Deserialization accepts the canonical text emitted by `serialize`, reconstructs a fresh world and restores its event-loop sample time.
The test checks exact roundtrip equality at Tick 50 and exact state/render equality at Tick 100 after destroying the interrupted world and resuming with a fresh command source.
These checks also run before any golden update.
This codec covers the synthetic scenario domain; it does not yet persist arbitrary production components, nyan views, terrain or queued production events.
New command sources can implement the same interface; original AoE replay formats and network transport are outside this implementation.

## Render recording

`openage.renderer.recording` exports an opt-in `RecordingRenderer`, `RecordingCommand` and CPU `RecordingUniformInput`.
Select it by constructing this backend instead of a graphics backend; use its CPU bufferless quad and typed sprite/UI inputs in ordinary [render passes](renderer/level1.md).
`begin_frame()` clears the frame and `render(pass)` appends `DRAW_SPRITE`/`DRAW_UI` lines in existing layer and renderable order, across successive passes.
Uniform-only updates with no geometry do not produce draws.
Commands carry stable symbolic IDs, integer hundredths for positions/fill, integer UI rectangles and uppercase RGB tint strings; they never serialize pointers or GPU handles.
GPU resource operations and unsupported uniform inputs raise errors.
The example submits HUD before sprites into different layers to verify layer ordering.
It tests semantic CPU submissions through `Renderer::render`, not pixels, shaders or the existing OpenGL/Qt stages automatically.

The numeric representation and codecs avoid platform-sized serialized values and new dependencies, making them suitable for a future WASM build.
Running this test still uses the existing native build and its link dependencies; successful native execution does not establish that the complete engine builds for WASM.

## Campaign cases and computer players

`--golden-jobs N` runs independent campaign worlds in separate processes; comparison remains in filename order and a failed comparison terminates active siblings.
Completed workers release their slot immediately, even when an earlier case is still running.
`--golden-timings` reports loading, external commands, event processing, visibility, economy and snapshot costs separately.

The runner freezes converted metadata before starting workers and validates the source scenario filename and explicit variant, not only its numeric mission index.
AI cases compare submitted operations and sampled state, then reconstruct the initial world and replay those operations with decisions disabled before checking or updating a baseline.
Their total wall time includes both worlds.
See [Computer player design and test commands](game_simulation/ai.md) for profile options, the Wallace cases and the explicitly isolated Barbarossa map sandbox.
