# openage-plus Architecture & Full Diff vs. Upstream `SFTtech/openage`

> **Project Definition**: **`openage-plus = openage + playable function helpers`**
> **Language**: **English** | [简体中文 (README-PLUS.zh-CN.md)](README-PLUS.zh-CN.md)
> `openage-plus` is a playable-focused fork of [SFTtech/openage](https://github.com/SFTtech/openage) that bridges the gap between a low-level engine framework and a fully playable real-time strategy game.
>
> - Main Project Entry: [README.md](README.md) | [README.zh-CN.md (简体中文)](README.zh-CN.md)
> - Upstream Original README: [README.upstream.md](README.upstream.md)

---

## Table of Contents

1. [Why `openage-plus`?](#1-why-openage-plus)
2. [Overall Architecture Comparison](#2-overall-architecture-comparison)
3. [Complete Catalog of New Modules](#3-complete-catalog-of-new-modules)
4. [Deep Dive into Modified Core Engine Logic](#4-deep-dive-into-modified-core-engine-logic)
5. [Campaign Fidelity Breakdown (William Wallace Mission 1 & Mission 2)](#5-campaign-fidelity-breakdown-william-wallace-mission-1--mission-2)
6. [Cross-Platform Release & Packaging Roadmap (Linux / Windows / macOS)](#6-cross-platform-release--packaging-roadmap-linux--windows--macos)
7. [Automated Test & Regression Matrix](#7-automated-test--regression-matrix)

---

## 1. Why `openage-plus`?

Starting with `v0.5.0`, upstream `SFTtech/openage` embarked on a clean-sheet C++20 engine rewrite built around an event-driven simulation core, the `nyan` data language, and a modern rendering pipeline. However, upstream remains in an **infrastructure and tech-demo phase**:
- Upstream's own README states: *"At the moment, 'gameplay' is basically non-functional."*
- No scenario/campaign loader pipeline (`.aoe2scenario` / `.aoe2campaign` -> `nyan` Mission Pack -> C++ `GameState`).
- No scenario trigger runtime to execute campaign objectives, scripts, or cutscenes.
- No physical collision occupancy (`StaticOccupancyGrid` & `DynamicOccupancy`), multi-unit formation movement, melee `AttackApproach`, armor-class damage calculation, or death-to-removal lifecycle.
- No villager resource gathering, building construction (foundation -> progress -> completion), Town Center unit training, tech research, or per-scenario entity/tech restrictions.
- No Fog of War, minimap, sprite-accurate selection highlights, objectives panel, or command panel HUD.

**The mission of `openage-plus`** is to supply the complete set of **`playable function helpers`** on top of upstream's `nyan` and event-driven architecture—turning the engine from a technical demonstration into a **directly playable RTS capable of running original *Age of Empires II: Definitive Edition* campaigns**.

---

## 2. Overall Architecture Comparison

```
┌───────────────────────────────────────────────────────────────────────────────────┐
│                           openage-plus Playable Stack                             │
├───────────────────────────────────────────────────────────────────────────────────┤
│ [Launchers & Extraction Tooling]                                                  │
│  • scripts/campaign_launcher.py (Desktop Campaign Launcher GUI & CLI)             │
│  • scripts/export_wallace_scenarios.py (GPV campaign unpacker & scenario dumper)  │
│  • scripts/export_tech_trees.py (Full civilization tech tree & graphics exporter) │
├───────────────────────────────────────────────────────────────────────────────────┤
│ [Asset Conversion & Scenario Compilation (Python / Nyan)]                         │
│  • openage/convert/ (DE2 8.4/8.9 versioned dat profiles + media filter + SLD)     │
│  • openage/scenario/ (Scenario IR -> Validation -> Nyan Mission Pack compiler)    │
│  • assets/nyan/ww_mvp/ (Original 120×120 Mission 1 & 2 converter + 16×16 packs)   │
├───────────────────────────────────────────────────────────────────────────────────┤
│ [C++ Campaign & Trigger Runtime (libopenage/scenario/)]                           │
│  • MissionPackReader / ScenarioLoader: Native terrain chunks, diplomacy, entities │
│  • TriggerRuntime: Event-driven Condition / Effect state machine                  │
│  • OriginalScenario: AoE2 trigger executor (spawns, Gaia capture, forced tech)    │
│  • ObjectiveController / FeedingTheArmy / OutpostFire: Objectives & 3-stage fire  │
├───────────────────────────────────────────────────────────────────────────────────┤
│ [C++ Core Gameplay Systems (libopenage/gamestate/ & pathfinding/)]                │
│  • StaticOccupancyGrid + DynamicOccupancy + SweptAABB + MovementReservation       │
│  • FormationPlanner + GroupMoveHandler + AttackApproach (2D elevation-aware)      │
│  • DiscreteDamage + ArmorClass calculation + EventLoop deferred death removal     │
│  • Economy & Town Center: Gathering, construction foundations, villager/techs     │
│  • FogOfWar (3-state) + Ally shared vision + Diplomacy stance control             │
├───────────────────────────────────────────────────────────────────────────────────┤
│ [Presentation & Interactive HUD (libopenage/presenter/ & input/ & assets/qml/)]   │
│  • PointerEvent + ContextActionResolver (Right-click move/attack/gather/build)    │
│  • SelectionModel + SelectionOverlay (Sprite pixel picking, box select, footprints│
│  • HudQmlAdapter + MissionHud.qml (Resources, objectives, command buttons, hints) │
│  • World2D / Terrain / Minimap Shaders (Fog of War shading, HP & build bars)      │
└───────────────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Complete Catalog of New Modules

### 3.1 Python Scenario Compilation Pipeline (`openage/scenario/` & `openage/validate_nyan/`)

Upstream has no scenario ingestion pipeline. `openage-plus` introduces a full Intermediate Representation (`ScenarioIR`) and deterministic Mission Pack compiler:

- **[openage/scenario/ir.py](openage/scenario/ir.py)**: Defines normalized `ScenarioIR` structures (map dimensions, terrain grid, elevation tiles, starting player resources, diplomacy matrix, entity placements, and trigger graphs).
- **[openage/scenario/mission1_dump.py](openage/scenario/mission1_dump.py)**: Parses original scenario and save files into complete terrain and placement inventories.
- **[openage/scenario/validation.py](openage/scenario/validation.py)**: Validates every `nyan` Fully Qualified Object Name (FQON), coordinate bound, player ID, and trigger cross-reference in `ScenarioIR`.
- **[openage/scenario/writer.py](openage/scenario/writer.py)**: Deterministically compiles `ScenarioIR` into a native-readable Mission Pack (`manifest.toml`, `terrain.txt`, `placements.txt`, `triggers.txt`, `mission_01.nyan`).
- **[openage/scenario/loader.py](openage/scenario/loader.py)** & **[openage/scenario/terrain_loader.py](openage/scenario/terrain_loader.py)**: Python-side scenario and terrain snapshot loaders.
- **[openage/validate_nyan/main.py](openage/validate_nyan/main.py)**: Standalone static validator for `nyan` syntax and cross-file object closures.

### 3.2 Original Campaign Unpacker & Full Tech Tree Exporter (`scripts/` & `assets/nyan/ww_mvp/`)

- **[scripts/export_wallace_scenarios.py](scripts/export_wallace_scenarios.py)**:
  - Reads Steam *Age of Empires II: Definitive Edition* `cam1.aoe2campaign` packages directly (automatically detecting and stripping the `GPV` container header).
  - Extracts `1_Wallace_1.aoe2scenario` and `1_Wallace_2.aoe2scenario`, using `AoE2ScenarioParser` to export all 120×120 terrain tiles, starting resources, per-player disable lists (`disabled_techs`, `disabled_units`, `disabled_buildings`), 5,698 / 10,130 placed objects, and 31 / 27 triggers into structured JSON.
- **[assets/nyan/ww_mvp/original_scene.py](assets/nyan/ww_mvp/original_scene.py)**:
  - Maps exported scenario JSON objects to real `de2_base` `nyan` game entities, buildings, trees, forage bushes, gold mines, decorations, and terrain tiles.
  - **On-the-fly Missing Sprite Synthesis**: Automatically converts `.sld` graphics into `.png` textures and `.sprite` definitions for entities whose pre-converted `idle_*.sprite` is missing in `de2_base` (such as `349 OakTree` and `411 ForestTree`).
  - **Per-Scenario Restriction Alignment**: Enforces original scenario restrictions—disabling Villager training at the Town Center in Mission 1 while enabling Villager training and applying the exact tech/building disable list in Mission 2.
- **[scripts/export_tech_trees.py](scripts/export_tech_trees.py)** & **[scripts/tech_tree/](scripts/tech_tree/)**:
  - Streams `empires2_x2_p1.dat` to export all civilization tech trees, unit/building prerequisite graphs, and an interactive viewer at [scripts/tech_tree/preview.html](scripts/tech_tree/preview.html).
- **[scripts/campaign_launcher.py](scripts/campaign_launcher.py)** & **[scripts/run_campaign.sh](scripts/run_campaign.sh)**:
  - Dual GUI/CLI campaign launcher that automatically detects a local AoE2:DE installation and loads the full 120×120 original scenarios.

### 3.3 C++ Native Scenario & Trigger Subsystem (`libopenage/scenario/`)

Upstream does not have a `libopenage/scenario/` directory. `openage-plus` builds this subsystem from scratch:

- **[libopenage/scenario/mission_pack_reader.cpp](libopenage/scenario/mission_pack_reader.cpp)**: Native C++ reader for Mission Pack manifests, terrain chunks, player setups, entity instances, and trigger definitions.
- **[libopenage/scenario/trigger/runtime.cpp](libopenage/scenario/trigger/runtime.cpp)** & **[instance.cpp](libopenage/scenario/trigger/instance.cpp)**:
  - Event-driven trigger state machine supporting `TimerElapsed`, `EntityRemoved`, `TriggerFired`, `AreaReached`, and `ResourceStockpile` conditions, plus `CompleteObjective`, `FailObjective`, `ActivateTrigger`, and `SpawnEntity` effects.
- **[libopenage/scenario/trigger/original_scenario.cpp](libopenage/scenario/trigger/original_scenario.cpp)**:
  - Native executor for original `.aoe2scenario` trigger graphs.
  - Supported Conditions: Timer (`type 10`), Objects in Area / Bring Object to Area (`type 1/3/5/22`), Own Objects / Destroy Object (`type 2/4`), Technology State & Accumulate Attribute (`type 6/14`).
  - Supported Effects: Display Instructions / Send Chat (`effect 20/3`), Activate / Deactivate Trigger (`effect 8/9`), Create Object / Dynamic Reinforcements (`effect 11`), Remove / Kill Object (`effect 14/24`), Change Diplomacy (`effect 1`), Research Technology (`effect 2`, e.g., force-upgrading Militia to Man-at-Arms in Mission 1), Change View (`effect 19`), and Declare Victory (`effect 4`).
  - **Gaia Village Ownership Transfer (`capture_gaia`)**: Uses 2D horizontal distance (`std::hypot`, ignoring elevation Z deltas) plus target building footprint radius (`radius_x`) to cascade ownership transfer across connected Gaia village buildings (Town Center, Barracks, Walls, Watch Tower) when player units arrive.
- **[libopenage/scenario/feeding_the_army.cpp](libopenage/scenario/feeding_the_army.cpp)**: Economy gathering and stockpile objective controller for Mission 2.
- **[libopenage/scenario/outpost_fire.cpp](libopenage/scenario/outpost_fire.cpp)**: Dynamically attaches 3-stage original fire damage sprite overlays as building HP drops below 75%, 50%, and 25%.
- **[libopenage/scenario/shore_fishing.cpp](libopenage/scenario/shore_fishing.cpp)**: Villager shore fishing activity controller.

### 3.4 C++ Occupancy, Formations & Tactical Movement (`libopenage/pathfinding/`)

Upstream only provides a basic grid A* pathfinder. `openage-plus` adds a complete spatial collision and tactical movement stack:

- **[libopenage/pathfinding/dynamic_occupancy.cpp](libopenage/pathfinding/dynamic_occupancy.cpp)**: Tracks real-time spatial occupancy for all movable units and buildings with physical footprints (`has_footprint()`), while ignoring non-colliding eye-candy (grass patches, waypoint flags, hidden trigger helpers).
- **[libopenage/pathfinding/swept_aabb.cpp](libopenage/pathfinding/swept_aabb.cpp)**: Continuous segment collision detection (Swept AABB) to prevent unit overlapping during movement.
- **[libopenage/pathfinding/movement_reservation.cpp](libopenage/pathfinding/movement_reservation.cpp)**: Target cell reservation disambiguation manager for simultaneous multi-unit moves.
- **[libopenage/pathfinding/formation.cpp](libopenage/pathfinding/formation.cpp)**: `FormationPlanner` for computing stable spread destinations when moving groups of units.
- **[libopenage/pathfinding/attack_approach.cpp](libopenage/pathfinding/attack_approach.cpp)**: Melee and ranged `AttackApproach` service that finds reachable attack slots around target footprints and evaluates attack range using 2D horizontal distance (`std::hypot(dne, dse)`) across elevated terrain.

### 3.5 C++ Context Input & QML Campaign HUD (`libopenage/input/`, `libopenage/presenter/`, `assets/qml/`)

- **[libopenage/input/pointer_event.cpp](libopenage/input/pointer_event.cpp)** & **[context_action_resolver.cpp](libopenage/input/context_action_resolver.cpp)**:
  - Resolves left-click picking/box-selection and right-click context commands (right-click ground = Move, enemy = Attack Approach, resource = Gather, friendly incomplete foundation = Resume Construction).
- **[libopenage/presenter/hud/selection_model.cpp](libopenage/presenter/hud/selection_model.cpp)** & **[selection_overlay.cpp](libopenage/presenter/hud/selection_overlay.cpp)**:
  - Maintains stable multi-entity selections and renders world-space footprint selection highlight quads.
- **[libopenage/presenter/hud/hud_qml_adapter.cpp](libopenage/presenter/hud/hud_qml_adapter.cpp)** & **[assets/qml/MissionHud.qml](assets/qml/MissionHud.qml)**:
  - Binds live `GameState` data to the QtQuick QML HUD: top resource bar (Food, Wood, Gold, Stone, Population), objectives & tutorial prompt panel, bottom-left selected entity stats (icon, name, HP, attack, armor, carried resources), and bottom-right command panel (Train Villager, Research Loom / Feudal Age, Build House / Barracks / Mining Camp, Pause / Resume Construction, Diplomacy Stance).

---

## 4. Deep Dive into Modified Core Engine Logic

In addition to new modules, `openage-plus` modifies and hardens key upstream subsystems:

### 4.1 Asset Converter (`openage/convert/`)

1. **Multi-Version DE2 `empires2_x2_p1.dat` Compatibility ([dat_compatibility.py](openage/convert/service/init/dat_compatibility.py) & [version_hashes.toml](cfg/converter/games/de2/version_hashes.toml))**
   - Upstream's hardcoded `genieutils` layout assumptions fail on newer Steam DE2 builds (such as the DE2 8.9 dat layout).
   - Added versioned layout profiles (8.4 and 8.9) so modern Steam DE2 installations convert cleanly.
2. **On-Demand Media Filtering & Metadata Closure ([media_filter.py](openage/convert/processor/conversion/de2/media_filter.py))**
   - Full conversion of thousands of DE2 `.sld` files takes a long time. Added media export request filtering that preserves full `nyan` metadata closure while converting only the requested subset of media assets.

### 4.2 GameState & Event Simulation Core (`libopenage/gamestate/` & `libopenage/event/`)

1. **Deterministic Equal-Time Event Ordering ([event/eventloop.cpp](libopenage/event/eventloop.cpp))**
   - Fixed non-deterministic ordering for events scheduled at identical timestamps, ensuring headless regression tests and windowed runs behave identically.
2. **Armor-Class Discrete Damage Calculation ([gamestate/system/combat.cpp](libopenage/gamestate/system/combat.cpp) & [discrete_damage.cpp](libopenage/gamestate/discrete_damage.cpp))**
   - Reads `Attack` and `Armor` class tables from `nyan` and computes discrete per-hit damage using the AoE2 formula $\max(1, \sum \max(0, \text{Attack}_i - \text{Armor}_i))$, skipping fallback effects.
3. **Event-Driven Death & Deferred Entity Removal ([gamestate/system/death.cpp](libopenage/gamestate/system/death.cpp) & [removal.cpp](libopenage/gamestate/system/removal.cpp))**
   - When an entity reaches 0 HP, it transitions into a `death` activity and schedules a deferred `game.removal` event on the `EventLoop`, cleanly releasing its `DynamicOccupancy` cells and firing `TriggerRuntime` listeners.
4. **Dynamic Spawn Priming & Invisible Helper Entity Tolerance ([gamestate/system/idle.cpp](libopenage/gamestate/system/idle.cpp))**
   - Original scenarios include invisible keep-alive/helper entities without `Idle` abilities (e.g., `Unit1291`). Updated `Idle::idle()` to return `0` instead of throwing an exception when an entity lacks an `IDLE` component.
   - Units spawned dynamically via triggers (`effect_type == 11`) immediately invoke `run_activity_system(at)` so they respond to movement and combat in both headless and windowed modes.
5. **Footprint-Filtered Dynamic Occupancy ([gamestate/map.cpp](libopenage/gamestate/map.cpp))**
   - `register_entity_in_dynamic_occupancy` now returns early when `not entity.has_footprint()`, preventing thousands of decorative grass tiles (`Unit1358`) and waypoint flags from blocking unit pathfinding.

### 4.3 Renderer & Presentation Layer (`libopenage/renderer/` & `libopenage/presenter/`)

1. **Tolerant `.sprite` Empty-Angle Parsing ([renderer/resources/parser/parse_sprite.cpp](libopenage/renderer/resources/parser/parse_sprite.cpp))**
   - Some generated `de2_base` sprites (such as `idle_cliff.sprite`) declare `angle 255` with no corresponding `frame` lines, causing upstream `angles.at()` to throw `std::out_of_range`. Empty non-mirrored angles are now filtered out safely before sorting.
2. **Fog of War, Health Bar, Construction Bar & Minimap Shaders ([assets/shaders/world2d.frag.glsl](assets/shaders/world2d.frag.glsl) & [terrain.frag.glsl](assets/shaders/terrain.frag.glsl))**
   - Integrated 3-state Fog of War shading into terrain and 2D entity shaders, plus real-time HP bars and building construction progress bars.
3. **Smart Initial Camera Centering ([presenter/presenter.cpp](libopenage/presenter/presenter.cpp))**
   - Initial camera positioning prioritizes player 1 entities with `MOVE` or `BUILD` components so distant scenario flags (`Unit600`) do not pull the camera away from the starting scout or Town Center.

---

## 5. Campaign Fidelity Breakdown (William Wallace Mission 1 & Mission 2)

| Dimension | Mission 1: Marching and Fighting | Mission 2: Feeding the Army |
| :--- | :--- | :--- |
| **Source File** | `1_Wallace_1.aoe2scenario` | `1_Wallace_2.aoe2scenario` |
| **Map Size** | 120 × 120 original terrain & elevation | 120 × 120 original terrain & river shallows |
| **Placed Entities** | 5,698 original objects | 10,130 original objects |
| **Original Triggers** | 31 triggers imported | 27 triggers imported |
| **Town Center Restrictions** | Villager training disabled per `disabled_units` | Villager training enabled; scenario-disabled techs/buildings enforced |
| **End-to-End Gameplay Flow** | Starting Militia -> march along waypoint flags -> mid-route reinforcements -> approach Gaia village to transfer Town Center/Barracks/Tower ownership -> trigger force-researches Man-at-Arms -> defeat English raiding party & destroy English Outpost -> Victory | Starting 1 Villager -> gather from forage bushes -> triggers spawn Villagers 2–4 and prompt Mill/Lumber Camp/Mining Camp -> train additional Villagers at Town Center -> stockpile 50 Food, 50 Wood, 50 Gold -> delayed trigger chain declares Victory |

---

## 6. Cross-Platform Release & Packaging Roadmap (Linux / Windows / macOS)

To let players on **Linux, Windows, and macOS** download and play without manually setting up Python virtual environments, C++ compilers, or CLI conversion flags—while strictly respecting copyright by not redistributing proprietary AoE2:DE assets—`openage-plus` is designed around a **3-in-1 release bundle**:

```
┌─────────────────────────────────────────────────────────────────────────┐
│                openage-plus Cross-Platform Release Bundle               │
│                                                                         │
│   ┌────────────────────┐   ┌────────────────────┐   ┌────────────────┐ │
│   │ 1. Game Engine Core│ + │ 2. Visual Converter│ + │ 3. Campaign    │ │
│   │ (openage-plus bin) │   │    Wizard (GUI)    │   │    Launcher GUI│ │
│   └────────────────────┘   └────────────────────┘   └────────────────┘ │
└─────────────────────────────────────────────────────────────────────────┘
```

### 6.1 Why the Visual Converter Is the Key to Cross-Platform Releases

Because *Age of Empires II: Definitive Edition* graphics, sounds, and campaign files are proprietary, **open-source binary releases cannot bundle pre-converted textures or audio**. Every player must run a local **Asset Conversion** step on their first launch:
1. In development mode, this requires multi-step CLI commands (`openage convert`, `export_tech_trees.py`, `export_wallace_scenarios.py`, `install.sh`).
2. Full conversion of several gigabytes of DE2 `.sld` graphics takes significant time, and Steam installation paths differ across Windows Registry, macOS `~/Library/Application Support/Steam`, and Linux Steam/Proton directories.

### 6.2 Design of the Three Release Components

#### Component 1: Game Engine Core (`openage-plus` Binary + Built-in Nyan Logic)
- **Bundled Contents**:
  - Prebuilt `libopenage` shared library and executable binary (C++20 / Qt6 / OpenGL / Cython extensions).
  - Bundled open-source `nyan` runtime library, GLSL shaders (`assets/shaders/`), QML HUD (`assets/qml/`), and campaign logic definitions (`assets/nyan/ww_mvp/`).
- **Embedded Python Runtime**:
  - Packages an embedded Python runtime (Windows Embedded Python / macOS Framework / Linux AppImage Python) containing `genieutils`, `AoE2ScenarioParser`, and `openage.convert` so users never need to install Python or `pip` packages manually.

#### Component 2: Visual Converter Wizard (First-Run Asset Conversion GUI)
- **Automatic Steam Path Detection**:
  - **Windows**: Scans Steam Registry keys (`HKCU\Software\Valve\Steam`) and `SteamLibrary\steamapps\common\AoE2DE` across drives.
  - **macOS**: Scans `~/Library/Application Support/Steam/steamapps/common/AoE2DE`.
  - **Linux**: Scans `~/.steam/steam/steamapps/common/AoE2DE` and Flatpak Steam locations, with a manual *"Browse..."* fallback button.
- **Two-Tier Conversion Strategy (Fast Campaign Mode vs. Full Mode)**:
  - **Fast Campaign Mode (Recommended, ~30–60 seconds)**: Uses `openage-plus`'s on-demand media filter (`media_filter.py`) and scenario unpacker to convert only the core metadata plus the exact SLD sprites, terrains, and scenarios needed for the William Wallace campaign—getting players into the game in under a minute.
  - **Full Conversion Mode (Background Incremental)**: Converts all civilization buildings, units, and tech tree assets in a background worker thread with a real-time progress bar, current asset indicator, time estimate, and diagnostic log export.
- **Asset Cache & Version Validation**:
  - Stores converted assets in standard per-OS user data directories (Windows `%APPDATA%\openage-plus\assets`, macOS `~/Library/Application Support/openage-plus/assets`, Linux `~/.local/share/openage-plus/assets`) and validates `manifest.toml` on subsequent launches for instant startup.

#### Component 3: Desktop Campaign Launcher (Campaign Launcher GUI)
- **Unified Entry Point**: Opening `openage-plus` launches the Qt6 Campaign Launcher GUI (evolved from [scripts/campaign_launcher.py](scripts/campaign_launcher.py)).
- **Key Features**:
  - **Asset Status Indicator**: Verifies whether local DE2 assets are converted; automatically launches the **Visual Converter Wizard** on first run.
  - **Mission Selection Cards**: Presents visual cards for *William Wallace Mission 1: Marching and Fighting*, *Mission 2: Feeding the Army*, and future missions, with a toggle between *"Original 120×120 Full Scenario"* and *"16×16 Fast Tutorial Scenario"*.
  - **Display & Language Settings**: Configures window/fullscreen resolution, English/Chinese tutorial prompts, and debug overlays.

### 6.3 Platform Packaging Targets

| Platform | Target Package Format | Packaging Toolchain |
| :--- | :--- | :--- |
| **macOS** (Apple Silicon & Intel) | `openage-plus.app` + `.dmg` disk image | `macdeployqt` to bundle Qt6 frameworks & dylibs, embedded Python framework + `nyan`, and code-signing scripts |
| **Windows** (x86_64) | Portable `openage-plus-win64.zip` + Inno Setup `.exe` installer | Built with MSVC / vcpkg, packaged via `windeployqt` with Windows Embedded Python |
| **Linux** (x86_64) | `openage-plus-x86_64.AppImage` + Flatpak | Built on Ubuntu 24.04 LTS, packaged with `linuxdeploy` + `linuxdeploy-plugin-qt` into a single-file AppImage |

---

## 7. Automated Test & Regression Matrix

`openage-plus` maintains a three-tier automated verification matrix across Python unit tests, C++ headless simulation demos, and real windowed rendering checks:

1. **Original Campaign End-to-End Headless Regression (Demo 53)**
   ```bash
   ./bin/run test -d gamestate.tests.simulation_demo 53
   ```
   - Verifies full original Mission 1 (marching, Gaia village capture, forced Man-at-Arms upgrade, English raid combat, victory at simulation time `1081.0`) and full original Mission 2 (villager restriction check, forage gathering, dynamic villager spawns, Town Center villager training, 50 Food/Wood/Gold stockpile, victory at simulation time `345.0`).
2. **35-Demo Core Gameplay & Interaction Regression Suite (P4.4D Regression)**
   ```bash
   bash scripts/p4_4d_regression.sh --regression
   ```
   - Covers `TriggerRuntime` (Demos 16–20), footprints & placement validation (Demos 21–26), right-click `ContextActionResolver` & `AttackApproach` (Demos 27–35), multi-unit formations & `DynamicOccupancy` (Demos 36–45), and Fog of War / diplomacy / gathering / construction / tech research (Demos 46–52).
3. **Python Scenario Compiler & Tech Tree Unit Tests (129 Tests)**
   ```bash
   ./venv/bin/pytest openage/scenario/tests scripts/tests
   ```
   - Covers `ScenarioIR`, terrain extraction, Mission Pack reading/writing, DE2 8.4/8.9 dat layout compatibility, and full tech tree export.
