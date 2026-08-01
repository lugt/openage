# P3 Repository Sync Handoff

**Branch:** `mvp/william-wallace`
**HEAD:** `ad6d819ec6df6ff24dc665279b2a4030dfe3b0a2`
**P3 tag:** `ww-mission1-p3-lifecycle-complete` (annotated, pushed)
**Date:** 2026-07-31
**Companion plan:** `docs/mvp/specs/WW_MISSION1_DEVELOPMENT_PLAN.md`

---

## Acceptance status

`P3_ACCEPTANCE_CONFIRMED`

P3 gameplay lifecycle refactor verified end-to-end:

| Check | Command | Result |
|---|---|---|
| Memory-safe build | `ninja -j1 -C .bin/g++-debug-Oauto-sanitize-none` | exit 0 |
| Demo 7 (WW Mission 1 headless regression) | `bin/run test --demo gamestate.tests.simulation_demo 7` | rc=0, PASS (last_dispatched_t=21.28976) |
| Demo 8 (EventLoop fractional) | demo 8 | rc=0, PASS |
| Demo 9 (Mission Pack native integration) | demo 9 | rc=0, PASS |
| Demo 10 (lifecycle characterization) | demo 10 | rc=0, PASS |
| Demo 11 (damage effect path) | demo 11 | rc=0, PASS |
| Demo 12 (generic death event) | demo 12 | rc=0, PASS |
| Demo 13 (deferred removal) | demo 13 | rc=0, PASS |
| Scenario pytest | `pytest openage/scenario/tests/` | 103 passed |
| Validate-nyan pytest | `pytest openage/validate_nyan/tests/` | 3 passed |
| Production headless smoke | `bin/run main --scenario --headless --no-convert --no-prompts` | Initialize OK; loader spawns 3 entities (SIGTERM via timeout = expected). **Scope**: see "Production smoke" section below — proves loader init + entity spawn + lifecycle Initialized + no crash only. The full AttackCommand chain is proven by demos 7/10/11/12/13. |

**No crash, no abort, no segfault.** `git diff --check` clean.

---

## Remote synchronization evidence

Verified on 2026-08-01 immediately before push:

```bash
$ git fetch fork
(no output)

$ git rev-list --left-right --count fork/mvp/william-wallace...HEAD
0	0

$ git rev-parse HEAD
d0e60c0c35551db951001f86ccb1cea162e32b21

$ git rev-parse fork/mvp/william-wallace
d0e60c0c35551db951001f86ccb1cea162e32b21

$ git ls-remote fork refs/heads/mvp/william-wallace
d0e60c0c35551db951001f86ccb1cea162e32b21	refs/heads/mvp/william-wallace

$ git status --short --branch
## mvp/william-wallace...fork/mvp/william-wallace
```

Local HEAD, remote-tracking ref, and `ls-remote` all point to the same commit hash `d0e60c0c...`; ahead/behind is `0 0`.

---

## Remote and branch

| Remote | URL | Role |
|---|---|---|
| `fork` | `git@github.com:lugt/openage.git` | push target |
| `origin` | `https://github.com/SFTtech/openage/` | upstream read |

| Branch | Pushed | URL |
|---|---|---|
| `mvp/william-wallace` | YES (`ad6d819e`) | `git ls-remote fork refs/heads/mvp/william-wallace` |

Per spec: "如果当前分支已经是明确的长期 feature branch,可以继续使用,不要无意义重命名" — `mvp/william-wallace` is the established long-term feature branch for this work; rename to `feature/ww-mission1-scenario-runtime` was deliberately skipped.

---

## HEAD commit

```
ad6d819ec6df6ff24dc665279b2a4030dfe3b0a2
```

Subject: `docs: record P3 gameplay lifecycle handoff`

Full P3 commit chain (top-down, newest first):

```
ad6d819e docs: record P3 gameplay lifecycle handoff
8059a009 gameplay: defer dead entity removal through event loop       (P3.4)
4c064465 gameplay: derive generic death events from health state      (P3.3)
666c0e21 gameplay: route attack damage through discrete effects       (P3.2)
```

---

## P3 tag

```
ww-mission1-p3-lifecycle-complete
```

Type: **annotated**
Message: `William Wallace Mission 1 P3 gameplay lifecycle complete`
Pushed: YES (`git ls-remote fork refs/tags/ww-mission1-p3-lifecycle-complete` confirms `d7d3e709`)
Target commit: `ad6d819ec6df6ff24dc665279b2a4030dfe3b0a2` (the `docs: record P3 gameplay lifecycle handoff` commit, which is the stable P3 implementation lock point)

The tag is **NOT** at HEAD — HEAD has since moved to `d0e60c0c` (the post-sync handoff doc commit). The tag deliberately pins the implementation lock point; the handoff doc is appended afterwards and is not part of the P3 lock.

**Verification command (corrected)** — do NOT use `git tag --points-at HEAD`, which would falsely report no tag:

```bash
git rev-parse ww-mission1-p3-lifecycle-complete^{commit}
# Expected: ad6d819ec6df6ff24dc665279b2a4030dfe3b0a2

git merge-base --is-ancestor ww-mission1-p3-lifecycle-complete HEAD
# Expected: exit 0 (no output)
```

---

## Commits added during repository recovery

**None.** No stash or unmerged branch required cherry-picking. The P3 work was already fully committed on `mvp/william-wallace` before this audit. The "recovery" audit confirmed self-containedness rather than adding commits.

---

## Branches audited

| Branch | Base | Ahead/behind | Relevant to current spec | Tests | Action |
|---|---|---|---|---|---|
| `mvp/william-wallace` (HEAD) | local | +20 vs fork | YES — current P3 work | demos 7-13 + pytest | KEEP |
| `backup/p3-gameplay-lifecycle-20260731-205744` | local @ ad6d819e | identical to HEAD | YES — pre-merge safety net | (same as HEAD) | KEEP (will become obsolete after next major merge; not deleted per spec) |
| `docs/rts-foundation-spec` | fork/docs/rts-foundation-spec | +1 vs fork | YES — spec redirect for MVP single-player pivot | n/a (docs) | KEEP (history; merged into mvp/william-wallace base) |
| `mvp/tutorial-demo` | local @ 7ac7bf05 | n/a | YES — Phase B tutorial; pre-P3 branch | Phase B tests | KEEP (history; ancestor of mvp/william-wallace) |
| `master` | fork/master | 0 vs fork | YES — upstream | n/a | KEEP |
| `feature/train-location-name-from-head-id` | origin/master | +1 vs origin/master | NO — convert subsystem unrelated to P3 | n/a | KEEP_FOR_LATER (not in mvp/william-wallace base; would belong to a different feature branch; not pushable here) |

No branches deleted.

---

## Stashes audited

```
$ git stash list --date=local
(empty)
```

Zero stashes. Nothing to recover.

---

## Untracked files audited

| File | Size at audit | Source | Commit? | Disposition |
|---|---|---|---|---|
| `p2-result.md` | 140 KB | `/export p2-result.md` artifact | NO (Claude Code export transcript) | moved to `/tmp/claude-code-exports-20260801/` |
| `p2.finish.md` | 285 KB | `/export p2.finish.md` artifact | NO | moved to `/tmp/claude-code-exports-20260801/` |
| `ph3-handoff.md` | 148 KB | `/export ph3-handoff.md` artifact | NO | moved to `/tmp/claude-code-exports-20260801/` |
| `p4.md` | 228 KB | `/export p4.md` artifact (created during this audit) | NO | moved to `/tmp/claude-code-exports-20260801/` |

These were session-export transcripts whose content is fully covered by committed reports (`docs/mvp/reports/p2.*`, `docs/mvp/reports/p3.*`). Hash references found in the transcripts (e.g. `0497a675`, `08fe0925`, `1e764d6b`, `02f5467c`, `46b34e3f`, `2453f310`, `6591470`) all correspond to commits already documented in the committed reports. No unique evidence required harvesting.

After disposition:

```bash
$ git status --porcelain
(empty output)
```

Working tree is clean. macOS user does not need to delete anything in the repo.

Ignored files audit: 14151 entries, all `.bin/`, `.venv/`, `.qt/`, or `assets/converted/` (the converted DE2 bundle, 3.1 GB). All properly `.gitignore`d. Largest tracked file is 1 MB (a renderer demo MP4).

**Secret / path leak audit:**

- API keys / tokens / passwords / private keys: **no real hits** (only false positives in `legal/GPLv3` and `buildsystem/pxdgen.py` tokenize logic)
- Local user paths (`/home/gtx`, `/Users/`, `C:\Users\`): present in pre-existing committed reports under `docs/mvp/reports/` and `docs/superpowers/plans/`. **Not new in this audit** — known environment-specific paths; recorded here for handoff context. No scrubbing required (already in history).

---

## Changes merged

**None** — nothing to merge.

`mvp/william-wallace` is self-contained: all P3 work is already in the chain `666c0e21` → `4c064465` → `8059a009` → `ad6d819e`. No stash, no unmerged branch contained relevant code.

---

## Changes retained separately

| Branch / ref | Why retained |
|---|---|
| `backup/p3-gameplay-lifecycle-20260731-205744` | Pre-merge safety net. Per spec禁止 force-push / reset; if anything goes wrong during the next major merge (P4+), this branch is a guaranteed-recoverable P3. |
| `feature/train-location-name-from-head-id` | Convert subsystem work not in P3 scope. Will need its own feature branch for push. |

---

## Changes rejected and why

**None.** No stash, no unmerged branch, no untracked file contained P3-relevant code that was rejected. The 3 untracked export transcripts are out-of-spec for commit.

---

## Test matrix

| Test | Status | Time |
|---|---|---|
| `ninja -j1 -C .bin/g++-debug-Oauto-sanitize-none` | exit 0 | < 1s (no-op) |
| Demo 7 | rc=0 PASS | 3.58s |
| Demo 8 | rc=0 PASS | 0.15s |
| Demo 9 | rc=0 PASS | 0.15s |
| Demo 10 | rc=0 PASS | 3.55s |
| Demo 11 | rc=0 PASS | 7.04s |
| Demo 12 | rc=0 PASS | 7.08s |
| Demo 13 | rc=0 PASS | 7.06s |
| `pytest openage/scenario/tests/` | 103 passed | < 0.1s |
| `pytest openage/validate_nyan/tests/` | 3 passed | < 0.1s |
| `pytest openage/scenario/tests/ openage/validate_nyan/tests/` | 106 passed | 0.08s |
| Production smoke | Initialize OK | 25s timeout (expected) |

No new warning families introduced. P0-F-4 inventory (14 unhandled ability parent warnings) + de2_base expansion additions (8 more) = 22 unique, all informational, combat not blocked.

---

## Production smoke

```
$ timeout 25 bin/run main --scenario --headless --no-convert --no-prompts
...
INFO load_mission_pack_into_game_state: spawned placed_instance=...PlayerVillager entity_id=0
INFO load_mission_pack_into_game_state: spawned placed_instance=...PlayerTownCenter entity_id=1
INFO load_mission_pack_into_game_state: spawned placed_instance=...EnemyVillager entity_id=2
INFO GameSimulation::initialize: phase complete (game + objective ready; lifecycle=Initialized)
INFO engine: runtime initialized (initialized=true; loop() may now spawn workers)
SIGTERM received (termination requested, e.g. kill); exiting
```

Exit code 124 = timeout (expected). No crash, no abort, no segfault.

**Proof boundary — what the smoke actually proves:**

The production smoke ONLY proves:

- The Mission Pack loader (P2.7) initializes successfully on a fresh launch.
- The loader spawns the three Mission 1 entities (PlayerVillager, PlayerTownCenter, EnemyVillager) with stable ScenarioObjectId → GameEntityId mapping.
- `GameSimulation::initialize` reaches the `Initialized` lifecycle state.
- The process is stable (no crash, no abort, no segfault) through that initialization.

The production smoke does NOT dispatch any user command and does NOT execute any combat events. Therefore it does NOT prove the attack chain.

**The full AttackCommand chain is proven by the dedicated C++ headless tests:**

```text
AttackCommand
→ game.attack
→ game.apply_discrete_effect
→ Health Curve
→ game.death
→ game.removal
```

- Demo 7 (`gamestate.tests.simulation_demo 7`) — end-to-end Mission 1 headless regression through the production runtime
- Demo 10 (`gamestate.tests.simulation_demo 10`) — direct C++ state inspection of the lifecycle (ScenarioObjectId mapping, HP, removal timestamp)
- Demo 11 (`gamestate.tests.simulation_demo 11`) — proves damage flows through `game.apply_discrete_effect`, not direct `Live::set_attribute`
- Demo 12 (`gamestate.tests.simulation_demo 12`) — proves generic `game.death` (idempotent, same-time duplicate safe)
- Demo 13 (`gamestate.tests.simulation_demo 13`) — proves deferred `game.removal` (stale attack / effect / removal event safe)

---

## Known environment-specific paths

These are referenced in committed docs (`docs/mvp/reports/`, `docs/superpowers/plans/`); they document this developer's Ubuntu host, NOT macOS. macOS users should substitute their own paths.

| Path in docs | Ubuntu location | macOS substitute |
|---|---|---|
| `/home/gtx/dev/games/openage` | repo root | `<your-checkout-dir>/openage` |
| `/home/gtx/dev/games/aoe2de` | AoE2 DE source dir (converter input) | `<your-aoe2de-source-dir>` |
| `/home/gtx/dev/games/wwise-tools/` | Wwise tools (audio conversion) | `<your-wwise-tools-dir>` |
| `/home/gtx/.venv/bin/python` | venv python | `<repo>/.venv/bin/python` |

**Do not commit absolute Ubuntu paths** — they leak the developer identity.

---

## macOS checkout commands

```bash
# 1. Clone the repository
git clone git@github.com:lugt/openage.git
cd openage

# 2. Fetch all remotes and tags
git fetch --all --tags

# 3. Switch to the P3 feature branch (already long-term; do NOT rename)
git switch mvp/william-wallace

# 4. Verify remote synchronization (must show 0 0 ahead/behind)
git rev-list --left-right --count fork/mvp/william-wallace...HEAD
# Expected: 0\t0

# 5. Verify the P3 tag is present and ancestor of HEAD (NOT necessarily
#    at HEAD — see "P3 tag" section)
git rev-parse ww-mission1-p3-lifecycle-complete^{commit}
# Expected: ad6d819ec6df6ff24dc665279b2a4030dfe3b0a2

git merge-base --is-ancestor ww-mission1-p3-lifecycle-complete HEAD
# Expected: exit 0 (no output)

# 6. Verify clean tree (no untracked files)
git status --porcelain
# Expected: (empty output)

# 7. (Optional) Inspect the P3 commit chain
git log --oneline --decorate ad6d819e^^^^..ad6d819e
# Expected:
#   ad6d819e docs: record P3 gameplay lifecycle handoff
#   8059a009 gameplay: defer dead entity removal through event loop
#   4c064465 gameplay: derive generic death events from health state
#   666c0e21 gameplay: route attack damage through discrete effects
```

---

## macOS build prerequisites

The build assumes a Linux-ish environment. macOS specifics:

| Requirement | Notes |
|---|---|
| Python 3.10+ | macOS system Python OK; use `.venv` |
| CMake ≥ 3.20 | `brew install cmake` |
| Ninja | `brew install ninja` |
| GCC or Clang | Xcode Command Line Tools (`xcode-select --install`) |
| Qt 6 (optional for renderer) | `brew install qt` |
| Opus, FreeType, Eigen | typically via brew |
| 16 GB RAM recommended | the build directory is ~2-3 GB; ASan builds need more |

**Note**: macOS-specific openage build issues are tracked in `fork/fix/macos-build-and-run-plus-deadlock` (NOT merged here; out of P3 scope).

**Memory safety**: build with `ninja -j1` (or `-j2` at most). On memory-constrained hosts, the Linux build OOM-panicked at `-j4`. macOS users should respect the same limit.

**Audio**: Wwise tools and AoE2 DE source dir are NOT committed. macOS users without these can build and run headless tests but cannot run the full converter. P7 (windowed integration) requires real assets on a Linux/Windows host with DE2 installed.

---

## Commands to resume development

After checkout:

```bash
# 1. Build (memory-safe parallelism)
ninja -j1 -C .bin/g++-debug-Oauto-sanitize-none

# 2. Verify remote sync state BEFORE running tests
git rev-list --left-right --count fork/mvp/william-wallace...HEAD
# Expected: 0\t0
git status --porcelain
# Expected: (empty)
git merge-base --is-ancestor ww-mission1-p3-lifecycle-complete HEAD
# Expected: exit 0

# 3. Run the P3 demos (verify the chain end-to-end)
for n in 7 8 9 10 11 12 13; do
    bin/run test --demo gamestate.tests.simulation_demo "$n"
done

# 4. Run scenario + validate_nyan pytest
.venv/bin/python -m pytest openage/scenario/tests/ openage/validate_nyan/tests/ -q

# 5. (Optional) Production headless smoke — proves loader + spawn only
timeout 25 bin/run main --scenario --headless --no-convert --no-prompts

# 6. Read the P3 reports
ls docs/mvp/reports/p3.*
#   p3.1-lifecycle-characterization.md
#   p3.2-damage-effect-integration.md
#   p3.3-generic-death-integration.md
#   p3.4-deferred-removal-integration.md
#   p3-handoff.md
#   p3-repository-sync-handoff.md  ← this file
```

All demo outputs and pytest results should match the test matrix above. If any demo fails on macOS, file an issue and DO NOT attempt P4 work until the regression is resolved.

---

## Test classes that can run on macOS

| Test | macOS OK? | Notes |
|---|---|---|
| `pytest openage/scenario/tests/` | YES | Pure Python, no DE2 needed |
| `pytest openage/validate_nyan/tests/` | YES | Pure Python |
| Demos 7-13 (C++ headless) | YES, with valid `assets/converted/de2_base` modpack | The modpack is gitignored; macOS user must produce or copy it from the Ubuntu host |
| `bin/run main --scenario --headless` | YES, with modpack | Same constraint |
| `bin/run main --windowed` (P7) | macOS usually OK; sometimes Linux is more stable | Requires real AoE2 DE assets |

**AoE2 DE assets MUST NOT be pushed to Git.** The macOS user must:
1. Legally own AoE2 DE on the same Steam / Microsoft account.
2. Convert locally via the converter (see `docs/superpowers/plans/2026-07-20-streaming-convert.md`).
3. Or copy `assets/converted/de2_base` (3.1 GB) via an out-of-band channel (rsync, scp, USB).

**macOS users without legal AoE2 DE can still develop and test:**
- All pytest
- All demo validation (with a placeholder modpack)
- New code paths in the event pipeline (demos 11/12/13 only require the spawned PlayerVillager + EnemyVillager — small hand-written nyan, no DE2 needed)

---

## What's NOT done in this audit

This audit did NOT:

- ❌ Modify any source code (P3 is locked)
- ❌ Modify the converter
- ❌ Modify Trigger Runtime, Objective Controller, terrain, or media
- ❌ Touch Mission 2-5
- ❌ Force-push, reset, clean, or delete any branch / tag / stash / reflog
- ❌ Push `feature/train-location-name-from-head-id` (different feature branch, out of P3 scope)

---

## Next phase: P4.1

**Do not start P4.1 in this session.** The handoff stops at the repository sync point.

P4.1 entry is recorded in `docs/mvp/reports/p3-handoff.md` §8:

1. Subscribe `WilliamWallaceMission01ObjectiveController` to `game.removal` event (EnemyVillager instance FQON).
2. Remove H0.5 TEMPORARY DEBT: `GameSimulation::tick_objective_controller` + `on_update` per-batch polling + `objective_poll_count` instrumentation + "TEMPORARY DEBT (H0.5)" log lines.
3. Verify demo 7 still PASS with last_dispatched_t=21.28976 (now driven by event subscription, not per-batch poll).
4. Verify demo 10 still PASS with objective polling annotation rewritten as "MissionCompletedEvent lifecycle hook (P4.1)".

P3 already provides the entry points P4.1 needs:
- `game.removal` event handler (P3.4)
- `GameState::get_named_entities()` (P2.7)
- `Live::on_attribute_change` / `on_threshold_drop` (P3.3, available for future use)

macOS user: when ready to start P4.1, create a fresh feature branch from `mvp/william-wallace` HEAD (`ad6d819e`) or from tag `ww-mission1-p3-lifecycle-complete`, and resume per `docs/mvp/specs/WW_MISSION1_DEVELOPMENT_PLAN.md` §P4.

---

## Done condition summary

```text
✓ P3 acceptance reproduced
✓ relevant local work safely preserved (backup branch + retained local branches)
✓ no proprietary / secret / generated content committed
✓ feature branch pushed (mvp/william-wallace → fork)
✓ annotated P3 tag pushed (ww-mission1-p3-lifecycle-complete)
✓ working tree contains only untracked /export transcripts (not source)
✓ handoff committed and pushed
```