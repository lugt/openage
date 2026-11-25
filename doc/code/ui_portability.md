# UI portability principles

Share presentation data and semantic actions across desktop, WebAssembly (WASM), and future mini-app or mini-game hosts.
Keep Qt/QML as the desktop UI and add other presentation adapters only when a target needs them.
These principles guide new work; they do not imply that the engine already runs on those targets.

For subsystem details, see the [engine architecture](architecture.md), [input system](input/README.md), [renderer](renderer/README.md), and [GUI](gui.md).

## Shared state and platform adapters

Use ordinary C++ presentation snapshots with stable entity and action IDs for resources, selection, health, objectives, and available actions.
The UI reads these snapshots and submits actions through defined interfaces instead of mutating simulation objects.
Respect the existing simulation and presenter thread boundaries when publishing snapshots.
Keep `QObject`, `QVariant`, `QImage`, JavaScript objects, and host-specific handles inside presentation adapters.

The existing [SelectedEntityView](../../libopenage/presenter/viewmodel/selected_entity_view.h) and [MissionView](../../libopenage/presenter/viewmodel/mission_view.h) provide Qt-free presentation data.
Extend those boundaries and use [HudQmlAdapter](../../libopenage/presenter/hud_qml_adapter.h) for Qt-specific bindings.
Do not require every existing input or selection class to become platform-independent in one refactor.

```text
simulation -> presentation snapshot -> Qt/QML adapter -> desktop HUD
                                   -> future host adapter -> host UI
UI input -> semantic action -> existing command/event interfaces -> simulation
```

## Semantic actions and simulation

Translate mouse, touch, keyboard, and gamepad gestures into the same actions, such as selecting entities, moving, building, training, or researching.
Reuse the existing command and event system instead of adding a second gameplay path for each UI.
Keep local presentation actions, such as opening a panel or moving the camera, outside authoritative gameplay commands.
Adapters convert screen or normalized coordinates to the engine's coordinate types.
Simulation time and world coordinates remain fixed-point.

Preserve the event-driven simulation described in the [engine architecture](architecture.md).
Fixed sampling times in regression tests do not introduce a tick-based simulation loop.
Keep desktop threading until a concrete target requires a change.
If a host must drive execution, expose a bounded advance operation through the simulation interface and return control to the host between calls.

## Minimap data boundary

The shared minimap interface should describe pixels and overlays independently of Qt.
A minimal implementation can expose an RGBA pixel buffer, dimensions, explicit row layout, and a revision identifier, with clear ownership for the published buffer.
Entity markers and the camera viewport can remain separate presentation data.
Split static terrain, fog, and dynamic overlays or add dirty-region updates only when measurements justify the complexity.

The desktop currently sends PNG/Base64 image data to QML and retains the previous ready image while loading its replacement.
PNG remains suitable for stored assets and diagnostics, but encoding and decoding it is unnecessary for every in-process minimap update.
When replacing this transport, establish the portable data boundary first, then let the desktop adapter wrap or copy pixels into `QImage`.
A `QQuickPaintedItem`, image provider, or texture upload is a desktop implementation choice rather than the shared interface.
A future Canvas or WebGL adapter can consume the same portable data.

## Platform services and assets

Keep asset loading, persistent storage, audio, and host scheduling at platform adaptation points.
Extract a small interface when a second implementation needs it; avoid introducing a general platform framework in advance.
Use existing filesystem and resource abstractions where they already satisfy the requirement.

The converter and desktop launcher remain host-side tools.
WASM and mini-app clients should consume preconverted assets with version and content-hash metadata instead of running the full conversion toolchain.
Reuse cached exports when converter inputs, exporter version, and outputs still match, and invalidate affected exports when they change.
Do not bundle proprietary original-game assets without redistribution rights.

## Target validation and tests

UI separation alone does not establish engine portability.
Validate the C++ and nyan build, Python/Cython runtime boundary, graphics backend, threading, filesystem, and asset loading for each target.
Keep browser main-thread work bounded so the host can process input and rendering.
Qt for WASM is a possible browser presentation path, subject to its [platform limitations](https://doc.qt.io/qt-6/wasm.html).
Do not assume browser DOM APIs or desktop QML deployment are available in a mini-app or mini-game runtime.
Choose the concrete host and distinguish its application UI from its game runtime before selecting an adapter.

Start target validation with one unit, selection, movement, and a small HUD before committing to a UI framework or broad engine port.
Reuse [headless text snapshots](snapshots.md) for deterministic presentation data and command behavior, keeping them independent of Qt, windows, and GPU initialization.
Verify each platform adapter with a small interaction smoke test; text snapshots do not verify layout or host integration.

## Scope for current development

- Extend the existing Qt-free view models for new HUD features and keep Qt values in adapters.
- Route buttons and device input through the same semantic actions and existing command/event interfaces.
- Establish a portable minimap data contract when replacing the PNG/Base64 transport.

Defer a universal widget toolkit, wholesale UI rewrite, new rendering backend, and generalized host framework until a concrete target demonstrates the need.
New dependencies and broad interface changes require the normal project review rather than being prerequisites for these boundaries.
