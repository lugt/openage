# William Wallace Mission 1 MVP — macOS handoff

## Branch & commit

| Field | Value |
|---|---|
| Branch | `mvp/william-wallace` (new, force-push-free) |
| Branched from `mvp/tutorial-demo` HEAD | `d2c73106` |
| Forward commits (this PR) | `84d0a6ca` → `12d17538` (11 commits) |
| Final HEAD on `mvp/william-wallace` | `12d17538` |
| Working tree on that branch | 5 untracked files (5104aea7 residuals) — kept intentionally |

## Setup on Ubuntu 26.04 (this machine, where G0-G3 were produced)

```bash
# once: deps
sudo apt install build-essential cmake ninja-build python3.14-venv \
                    libepoxy-dev libglm-dev libsdl2-dev libsdl2-image-dev \
                    libopus-dev libopusfile-dev libvorbis-dev libvorbisfile-dev \
                    libnyan-converter-deps

# build dir
cmake -S . -B bin -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C bin
. .venv/bin/activate        # if not already
```

Build invocation verified to succeed clean (`ninja -C bin` exits 0).

## Setup on macOS (Apple Silicon / Intel, untested in this round)

```bash
# Qt 6.x is bundled by brew. openage uses Qt 6 EGL/GL / xcb,
# not native macOS Metal; macOS should fall back to Qt's cocoa
# platform plugin automatically. The renderer's OpenGL 3.3 Core
# profile requirement still applies -- macOS does ship a 3.3
# Core driver on every supported macOS, so this round expects
# a real `bin/run --scenario` window to come up clean.
brew install cmake ninja qt@6 eigen glm opusfile sdl2 sdl2_image

cmake -S . -B bin -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6)"
ninja -C bin
. .venv/bin/activate
```

If Qt's `libepoxy` glue on macOS lags behind, pass
`-DOpenGL_GL_PREFERENCE=LEGACY` to the cmake invocation; Linux
was tested without that flag and passed the OpenGL 3.3 context
creation gate that the Ubuntu round-trip could not (gcc-15 +
LLVMpipe does not expose a Core 3.3 context).

## ww_mvp install + run on a fresh checkout

```bash
# every checkout
git switch mvp/william-wallace
git pull --ff-only

# one-time
bash assets/test/nyan/testscene/install.sh   # overlays stopgap schemas + TestsceneGrass
bash assets/nyan/ww_mvp/install.sh           # overlays Mission 1 nyan

# fresh asset install (no api_export, no converter)
# When the user has already generated /assets/converted/{engine,de2_base,ww_mvp}/ on
# this machine, the launch entry point will pick them up.
bin/run --scenario --headless               # true headless smoke (Stage 4A)
bin/run --scenario --no-prompts --no-convert # Stage 4B offscreen smoke
# macOS only:
bin/run --scenario --no-convert --no-prompts # Stage 4C windowed FULL (must bring up a window)
```

## Official Mission + PlayerSetup FQONs (used by Stage 4A audit + demos)

| Object       | FQON |
|---|---|
| Mission      | `ww_mvp.campaign.william_wallace.mission_01.Mission` |
| PlayerSetup  | `ww_mvp.campaign.william_wallace.mission_01.PlayerSetup` |
| Initial PlacedEntities | `PlayerVillager`, `PlayerTownCenter`, `EnemyVillager` |
| Mission 1 objective controller | `WilliamWallaceMission01ObjectiveController` |

## Dependency closure that Phase G1.4 / `Engine::initialize_runtime()` verifies

```
requested modpacks  (passed via set_modpacks, which prepends "engine")
    de2_base
    ww_mvp
effective load_order after activate_modpacks
    engine
    de2_base
    ww_mvp
active modpacks after activate_modpacks invariant
    engine
    de2_base
    ww_mvp
NYAN FQON resolution for Villager / TownCenter is provided by de2_base,
which is present in /available/ whether or not it is in /active/.
Mission 1 references `de2_base.data.game_entity.generic.villager.villager.Villager`
through the same Game::Game::load_data path; that path is exercised by
Headless mode (verify stage 2) without touching Qt, the renderer, or
any OpenGL context.

The modpack.toml [dependency] chain does not auto-pull de2_base from
ww_mvp yet; the Engine pre-flight hands the explicit closed list
[de2_base, ww_mvp] to `set_modpacks`. A future dep-walker commit can
generalise this so `ww_mvp` declares its dependency and the launch
entry points only need to pass [ww_mvp].
```

## Tests confirmed passing on this branch (Ubuntu, gcc-15, libepoxy + llvmpipe)

| Stage / Test                                     | Result |
|---|---|
| clean incremental `ninja -C bin` (Phase G1 + G2) | PASS (no work after rebuild; 9-file diff rebuild) |
| `bin/run test --demo gamestate.tests.simulation_demo 1` (move) | PASS |
| `bin/run test --demo gamestate.tests.simulation_demo 5` (attack) | PASS |
| `bin/run test --demo gamestate.tests.simulation_demo 6` (victory) | PASS |
| generic `--scenario --headless` smoke            | PASS (3 initial entities spawn; lifecycle Initialized; no Presenter / Qt / OpenGL touch) |
| `scripts/verify_ww_mission1.sh` Stage 1 (build/import smoke) | PASS |
| `scripts/verify_ww_mission1.sh` Stage 2 (headless load, 0 nyan errors) | PASS |
| `scripts/verify_ww_mission1.sh` Stage 3 (Mission 1 precise media closure: 32 resolved, 2 excluded, 29 de2_base graphics deferred) | PASS |
| `scripts/check_no_tutorial_strings.sh` (4-check static guard) | PASS |

## Known NOT PASSING on this branch (carried-over or environmental)

| Item | What | Why / Workaround |
|---|---|---|
| FULL/offscreen on Ubuntu 26.04 + llvmpipe | crashes at `OpenGL version 3.3 is not available` after `init_graphics()` reaches `init_final_render_pass()` | OpenGL 3.3 Core profile is not available on a software-rendered `QT_QPA_PLATFORM=offscreen`; closed as SKIP_ENVIRONMENT (not a simulation failure). macOS / real GL works here. |
| `simulation_demo_0` (Phase E0 empty scenario) | dies with `nyan::ObjectNotFoundError: object not found: ` when initialising an empty `GameSimulation` (no scenario FQON). | Phase E0 leftover demo; intentionally not in scope of Mission 1 MVP. Demos 1 / 5 / 6 pass. |
| de2_base sprites (FIX-3) | `villager.nyan` / `town_center.nyan` reference `graphics/attack_mediterranean_villager.sprite`, `graphics/idle_villager.sprite`, etc. that the converter has not exported yet. | FIX-3 DDS terrain deferred to Phase D. Headless simulation does not need them; Stage 3 audit marks them `DEFERRED` instead of `MISSING`. |
| `8710x8755 atlas OOM` (FIX-4) | a single texture compilation blows the address space on memory-constrained VMs | FIX-4 atlas OOM deferred to Phase D. macOS has more address space; should not reproduce there. |
| `ww_mvp modpack.toml [dependency]` is `engine` only | The script / Engine layer currently passes `[de2_base, ww_mvp]` explicitly so the launch works; a follow-up should teach ModManager to walk `[dependency]` chains so callers only need `[ww_mvp]`. | Tracked as a known scope-bound tightening; not blocking Mission 1 simulation. |
| `Engine::Engine` constructor still creates the simulation early (currently required for ctor-time side-effects like config loading) — `Engine::initialize_runtime()` only does the heavy init; the construction vs initialisation split is logged, not refactored. | G1 deliberately kept a minimal-but-correct shape. Full split is technical debt for Phase D / G4 once Phase D is open. |
| Five 5104aea7-residual files untracked in this branch (`openage/error/CMakeLists.txt`, `openage/exception_guard/CMakeLists.txt`, `openage/validate_nyan/CMakeLists.txt`, `openage/convert/processor/export/output_checks.py`) | These were restored as CMake registrations after a kernel panic wiped staged work and never picked up their source files. They have all been individually classified; none are required by the build and none are committed in this round. | Kept as a deliberate diff report (`/tmp/5104aea7_residual_report.md` not in repo). Pull / cleanup happens once the source files exist. |

## Items deferred (DDS terrain + Phase D gameplay refactor)

| Item | Plan |
|---|---|
| DDS real grass terrain | FIX-3. Until then, `TestsceneGrass` is a placeholder tile; the playable map renders correctly via the testscene_data fixture but the visual fidelity is a fixture, not a final asset. |
| damage channel rewire (ApplyDiscreteEffect vs Attack path) | Phase D gameplay refactor. The legacy damage flow lives in AttackHandler right now; this would be lifted into the Effect system. |
| Health ≤ 0 → Death event (single source of truth) | Phase D. Currently death is detected inline in AttackHandler. |
| Removal deferral (chained to Death event) | Phase D. Currently `AttackHandler` calls `GameState::remove_game_entity` synchronously. |
| Objective controller event notification (instead of polling) | Phase D. `WilliamWallaceMission01ObjectiveController::on_update(current_time)` polls the game state once per simulation step in `simulation.run()`. |
| Attack/Death/Remove path frozen for Phase E | as the user spec requires during this round. No gameplay refactor was touched here. |

## macOS-specific checklist (Stage 4C — true windowed FULL)

Run on a real Mac with `DISPLAY` (no offscreen) and a GPU:

1. Run verify stage 1-3 (build/headless/media closure) on macOS; expect PASS with
   the same Qt / OpenGL expectations documented for Linux in
   `docs/mvp/mvp-gameplay-spec.md` §4.
2. Run `bin/run --scenario --no-prompts --no-convert`; expect a real
   16x16 grass window with PlayerVillager, PlayerTownCenter,
   EnemyVillager at the start.
3. Drag-select a Villager, then right-click on a terrain tile, expect
   the villager to walk to it (move reaches its target within the
   physics simulation's event window).
4. Drag-select a Villager, then right-click on EnemyVillager with
   attack mode, expect enemy HP to drop, enemy removed when HP<0
   (current-damage-path is FIX-3/FIX-4 deferred; only the legacy
   path will run).
5. Confirm the engine's runtime cleanup joins the workers cleanly on
   Cmd-Q and that `logs/` does not accumulate any open file handles.

If any of those fail, classify the regression either as
Phase D gameplay refactor scope (combat semantics) or as a Linux
side-effect (renderer paths), and route accordingly.

## git fetch / pull on macOS

```bash
git fetch origin
git switch mvp/william-wallace  # or git checkout -b mvp/william-wallace origin/mvp/william-wallace
git pull --ff-only                # only if a shared history; otherwise just clone
```

This branch is not yet pushed (`git status` only saw local commits
in this round; the user explicitly forbade force-push). Once the
shared repo accepts the branch as a new one and pushes from Ubuntu,
macOS clones see the full G0-G3 history.
