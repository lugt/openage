# M0 Determinism Harness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the M0 determinism harness - a standalone, tickable, empty-world deterministic simulation that produces a stable per-tick checksum and replays from an identical manifest - so that M1+ gameplay systems have a verified deterministic substrate to plug into.

**Architecture:** A new `libopenage/gamestate/sim/` subsystem holds `DeterministicSimulationLoop` (14 fixed phases), `SimulationClock` (`tick_id_t`, 20 Hz), two registries (`PhaseSystemScheduler`, `ActivityTaskRegistry`), a `TypedEventQueue`, a `CommandIngressQueue`/`CommandLog` skeleton, a `WorldMutationBuffer`, a `CanonicalStateSerializer` + `ChecksumSystem`, and a deterministic multi-stream RNG built on the existing `rng::RNG`. M0 is **standalone and headless**: it does NOT touch the existing `GameSimulation::run()` wall-clock loop (that is M1+ work, gated behind `RuntimeMode`). Empty world = zero entities, zero commands. Exit gate S0 (gates A-L).

**Tech Stack:** C++20, CMake (`add_sources` + `add_subdirectory`), openage test framework (`libopenage/testing/testing.h` macros `TESTEQUALS`/`TESTFAIL`, registered in `libopenage/<dir>/CMakeLists.txt` AND `openage/testing/testlist.py::tests_cpp()`), existing `util::FixedPoint<int64_t,16>` and `rng::RNG`. Build: `ninja -C bin -j$(nproc)`. Run tests: `.venv/bin/python -m openage test <name>`.

## Global Constraints

- **Authoritative time is `tick_id_t` (uint64), 20 Hz, `TICK_DURATION = 50'000'000 ns`.** No wall-clock drives gameplay (spec 0.2, 2.1).
- **`current_tick = T+1` is assigned in Phase 13 EndTick ONLY, never in BeginTick** (spec 1.3).
- **SimLoop is the sole gameplay driver; RuntimeMode is mutually exclusive** (spec 0.1, 2.16). M0 adds the loop as a standalone unit; it does NOT replace `GameSimulation::run()` (that risks LegacyDemo - deferred to M1).
- **Stable IDs from explicit constants, NEVER runtime registration call order; duplicate id -> `freeze()` FAILS** (spec 2.15).
- **Authoritative tests use the real deterministic path, never a sync fallback** (spec 2.9, 4.4). M0 has no pathfinding yet, so this constraint reads: no shortcuts around the checksum/replay path.
- **No dual authoritative state** (spec 0.6, 2.8): deterministic runtime state is authoritative; M0 has no legacy curves to mirror yet.
- **No gameplay systems in M0** (spec position): no pathfinding, combat, gather, build, production, AI, scenario. Only the harness skeleton + empty-world determinism.
- **Build/test commands (verified on this machine):**
  - Build: `ninja -C bin -j$(nproc)` (from repo root)
  - Run one C++ test: `.venv/bin/python -m openage test "openage::gamestate::sim::tests::<name>"`
  - Run all tests: `.venv/bin/python -m openage test -a`
  - List tests: `.venv/bin/python -m openage test --list`
- **Test file conventions (verified):** C++ test files are `*_test.cpp` in `libopenage/<dir>/`, each containing `void testname()` functions using `TESTEQUALS(actual, expected)` / `... or TESTFAIL;` from `libopenage/testing/testing.h`. Each is (1) added to `libopenage/<dir>/CMakeLists.txt` `add_sources(libopenage ...)` and (2) registered in `openage/testing/testlist.py::tests_cpp()` as `"openage::gamestate::sim::tests::<name>"`.
- **Header guard / namespace:** new code lives in `namespace openage::gamestate::sim`. Use `#pragma once`. Copyright header `// Copyright 2013-2026 the openage authors. See copying.md for legal info.` on every new file.
- **Commit message prefix:** `sim:` for the new subsystem (matches existing `convert:` / `docs(spec):` style). Co-author trailer on every commit.

## Scope Check

This plan is one subsystem (the M0 determinism harness). It is independently testable (gate S0 A-L). M1 (Move), M2 (Attack/Damage/Death), etc. are separate later plans. No sub-split needed.

## File Structure

New subsystem `libopenage/gamestate/sim/` (new directory, new `CMakeLists.txt`):

| File | Responsibility |
|---|---|
| `sim/types.h` | `tick_id_t`, `TickPhase` enum (14 values), `RuntimeMode` enum, `SystemId`/`ActivityTaskId`/`EventTypeId` `StrongId` typedefs |
| `sim/strong_id.h` | Generic `StrongId<Tag,T>` template (none exists yet) |
| `sim/simulation_clock.{h,cpp}` | `SimulationClock`: `current_tick()`, `TICK_DURATION`, `advance()` |
| `sim/world_mutation_buffer.{h,cpp}` | `WorldMutationBuffer`: stage field writes, commit-at-phase |
| `sim/simulation_context.{h,cpp}` | `SimulationContext`: per-tick handles (clock, queues, buffer, registries refs) |
| `sim/phase_system_scheduler.{h,cpp}` | `PhaseSystemDescriptor`, `IPhaseSystem`, `PhaseSystemScheduler` (register/freeze/run_phase); freeze topo-sorts + detects cycles/dup ids |
| `sim/activity_task_registry.{h,cpp}` | `ActivityTaskRegistry` (register/freeze/execute); `ActivityTaskResult` variant + failure/interrupt enums |
| `sim/registry_manifest.{h,cpp}` | `RegistryManifest`: record stable ids, compute hash for replay header |
| `sim/typed_event_queue.{h,cpp}` | `TypedEventQueue`: enqueue + 6-tuple sort + drain; `EventEnvelope`/`EventOriginPhase` |
| `sim/command.{h,cpp}` | `Command` header/payload variant, `IssuerRef`, `RootCommandId` |
| `sim/command_ingress_queue.{h,cpp}` | `CommandIngressQueue`: issue + canonical sort |
| `sim/command_log.{h,cpp}` | `CommandLog`: append-only replay artifact |
| `sim/phase_work_queue.h` | `PhaseWorkQueue<T>` template: `in_checksum`, `must_empty`, sorted drain |
| `sim/work_queue_registry.{h,cpp}` | `WorkQueueRegistry`: register queues, Phase 12 must-empty assertion |
| `sim/canonical_state_serializer.{h,cpp}` | `CanonicalStateSerializer`: schema-defined canonical bytes |
| `sim/checksum_system.{h,cpp}` | `ChecksumSystem`: Phase 12 orchestration, single serializer |
| `sim/deterministic_rng.{h,cpp}` | `DeterministicRNG`: stable stream ids -> existing `rng::RNG`; serialize/seed |
| `sim/replay_header.{h,cpp}` | `ReplayHeader`: schema/data/scheduler hashes, tick duration, rng states |
| `sim/deterministic_simulation_loop.{h,cpp}` | `DeterministicSimulationLoop`: `run_tick(T)` 14-phase skeleton |
| `sim/sim_test.cpp` | All S0 test functions |
| `sim/CMakeLists.txt` | `add_sources` + (no `pxdgen` - M0 has no Python bindings) |

Modified:
- `libopenage/gamestate/CMakeLists.txt` - add `add_subdirectory(sim/)`
- `openage/testing/testlist.py` - register each `sim_test.cpp` function under `tests_cpp()`

No other existing files are modified in M0 (the `GameSimulation`/`send_command`/`activity` rewrites are M1+).

---

### Task 1: Scaffold the `sim/` directory + `StrongId` template

**Files:**
- Create: `libopenage/gamestate/sim/strong_id.h`
- Create: `libopenage/gamestate/sim/types.h`
- Create: `libopenage/gamestate/sim/CMakeLists.txt`
- Modify: `libopenage/gamestate/CMakeLists.txt:26` (add `add_subdirectory(sim/)`)
- Test: none yet (scaffold compiles)

**Interfaces:**
- Consumes: nothing
- Produces: `StrongId<Tag, T>` template; `tick_id_t`, `TickPhase`, `RuntimeMode`, id typedefs used by every later task.

- [ ] **Step 1: Create `strong_id.h`**

Create `libopenage/gamestate/sim/strong_id.h`:

```cpp
// Copyright 2013-2026 the openage authors. See copying.md for legal info.

#pragma once

#include <cstdint>
#include <functional>

namespace openage::gamestate::sim {

/**
 * A type-safe identifier: a bare integer wrapped in a tag type so that
 * SystemId and ActivityTaskId cannot be accidentally mixed.
 *
 * StableIds are assigned from explicit constants or generated schema
 * (never runtime registration call order). Equality and hashing are
 * on the underlying integer so StrongId can key std::unordered_map.
 */
template <typename Tag, typename Rep>
struct StrongId {
	using tag_type = Tag;
	using rep_type = Rep;

	Rep value{};

	constexpr StrongId() = default;
	constexpr explicit StrongId(Rep v) : value{v} {}

	constexpr bool operator==(const StrongId &o) const { return value == o.value; }
	constexpr bool operator!=(const StrongId &o) const { return value != o.value; }
	constexpr bool operator<(const StrongId &o) const { return value < o.value; }
};

} // namespace openage::gamestate::sim

namespace std {

template <typename Tag, typename Rep>
struct hash<openage::gamestate::sim::StrongId<Tag, Rep>> {
	size_t operator()(const openage::gamestate::sim::StrongId<Tag, Rep> &id) const noexcept {
		return hash<Rep>{}(id.value);
	}
};

} // namespace std
```

- [ ] **Step 2: Create `types.h`**

Create `libopenage/gamestate/sim/types.h`:

```cpp
// Copyright 2013-2026 the openage authors. See copying.md for legal info.

#pragma once

#include <chrono>
#include <cstdint>

#include "gamestate/sim/strong_id.h"

namespace openage::gamestate::sim {

/// Authoritative gameplay time. Increments once per tick (Phase 13 EndTick).
using tick_id_t = uint64_t;

/// Fixed timestep: 50 ms = 20 Hz. Spec 2.1.
static constexpr std::chrono::nanoseconds TICK_DURATION{50'000'000};

/// The 14 frozen tick phases (spec 1.3). Central definition - NOT registry order.
enum class TickPhase : uint8_t {
	BeginTick = 0,
	InputAndIntentIngest = 1,
	IntentValidationAndArbitration = 2,
	ActivityEvaluation = 3,
	Pathfinding = 4,
	LocomotionAndSpatial = 5,
	CombatAndProjectile = 6,
	DamageAndDeath = 7,
	GatherBuildProduction = 8,
	DomainEventDispatch = 9,
	ScenarioAndAIObservation = 10,
	DeferredEntityCommit = 11,
	ChecksumAndReplayRecord = 12,
	EndTick = 13,
};

/// Mutually exclusive runtime modes (spec 2.16).
enum class RuntimeMode : uint8_t {
	LegacyDemo,            // old demo; does NOT run deterministic systems
	DeterministicGameplay, // SimLoop is sole driver
	Replay,                // SimLoop + ReplayCommandSource; no live input
};

// ---- Stable id typedefs (spec 2.1) ----
struct SystemTag {};
using SystemId = StrongId<SystemTag, uint32_t>;

struct ActivityTaskTag {};
using ActivityTaskId = StrongId<ActivityTaskTag, uint32_t>;

struct EventTypeIdTag {};
using EventTypeId = StrongId<EventTypeIdTag, uint32_t>;

struct EventIdTag {};
using EventId = StrongId<EventIdTag, uint64_t>;

struct EntityTag {};
using entity_id_t = StrongId<EntityTag, uint64_t>;

struct PlayerTag {};
using player_id_t = StrongId<PlayerTag, uint32_t>;

// Correlation keys for event waits (spec 2.1). Defined here so event_wait_spec.h
// (Task 4) and typed_event_queue.h (Task 6) both see them without ordering coupling.
struct PathRequestTag {};
using PathRequestId = StrongId<PathRequestTag, uint64_t>;

struct AttackTag {};
using AttackId = StrongId<AttackTag, uint64_t>;

struct MovementGenTag {};
using MovementGenerationKey = StrongId<MovementGenTag, uint64_t>;

struct IssuerTag {};
using IssuerStableId = StrongId<IssuerTag, uint64_t>;

enum class IssuerKind : uint8_t { Player, AI, Scenario, System };

} // namespace openage::gamestate::sim
```

- [ ] **Step 3: Create the `sim/CMakeLists.txt`**

Create `libopenage/gamestate/sim/CMakeLists.txt`:

```cmake
add_sources(libopenage
	# .cpp sources added as tasks introduce them
)
```

- [ ] **Step 4: Wire the subdirectory into the parent**

Modify `libopenage/gamestate/CMakeLists.txt`. The file ends with a list of `add_subdirectory` calls; add `sim/` after `tutorial/`.

Change:
```cmake
add_subdirectory(activity/)
add_subdirectory(api/)
add_subdirectory(component/)
add_subdirectory(demo/)
add_subdirectory(event/)
add_subdirectory(system/)
add_subdirectory(tutorial/)
```
To:
```cmake
add_subdirectory(activity/)
add_subdirectory(api/)
add_subdirectory(component/)
add_subdirectory(demo/)
add_subdirectory(event/)
add_subdirectory(sim/)
add_subdirectory(system/)
add_subdirectory(tutorial/)
```

- [ ] **Step 5: Build to verify the scaffold compiles**

Run: `ninja -C bin -j$(nproc) 2>&1 | tail -5`
Expected: build succeeds (CMake re-runs, picks up new subdir; no compile errors). If CMake did not reconfigure, run `ninja -C bin` again - it self-detects CMakeLists changes.

- [ ] **Step 6: Commit**

```bash
git add libopenage/gamestate/sim/ libopenage/gamestate/CMakeLists.txt
git commit -m "sim: scaffold gamestate/sim/ subsystem + StrongId + tick types"
```

---

### Task 2: `SimulationClock` + first test (empty-world tick count)

**Files:**
- Create: `libopenage/gamestate/sim/simulation_clock.h`
- Create: `libopenage/gamestate/sim/simulation_clock.cpp`
- Create: `libopenage/gamestate/sim/sim_test.cpp`
- Modify: `libopenage/gamestate/sim/CMakeLists.txt`
- Modify: `openage/testing/testlist.py` (register the test)

**Interfaces:**
- Consumes: `tick_id_t`, `TICK_DURATION` from `types.h`
- Produces: `SimulationClock` with `current_tick()`, `advance()` (bumps to T+1).

- [ ] **Step 1: Write the failing test**

Create `libopenage/gamestate/sim/sim_test.cpp`:

```cpp
// Copyright 2013-2026 the openage authors. See copying.md for legal info.

#include "gamestate/sim/simulation_clock.h"

#include "../testing/testing.h"

namespace openage::gamestate::sim::tests {

// Gate A (partial): an empty world advanced 10 ticks reports tick 10.
void clock_advances() {
	SimulationClock clock;
	TESTEQUALS(clock.current_tick(), tick_id_t{0});
	clock.advance();
	TESTEQUALS(clock.current_tick(), tick_id_t{1});
	for (int i = 0; i < 9; ++i) {
		clock.advance();
	}
	TESTEQUALS(clock.current_tick(), tick_id_t{10});
}

} // namespace openage::gamestate::sim::tests
```

- [ ] **Step 2: Run test to verify it fails**

Register the test first so the runner knows it. In `openage/testing/testlist.py`, find the `tests_cpp()` function and add a line inside it (before the final `yield` of the existing list, or just append). Add:

```python
    yield "openage::gamestate::sim::tests::clock_advances"
```

Run: `.venv/bin/python -m openage test "openage::gamestate::sim::tests::clock_advances" 2>&1 | tail -15`
Expected: FAIL - build error: `simulation_clock.h` not found / `SimulationClock` undefined.

- [ ] **Step 3: Write minimal implementation**

Create `libopenage/gamestate/sim/simulation_clock.h`:

```cpp
// Copyright 2013-2026 the openage authors. See copying.md for legal info.

#pragma once

#include "gamestate/sim/types.h"

namespace openage::gamestate::sim {

/**
 * The authoritative gameplay clock. Exposes tick_id_t only; wall-clock
 * never drives gameplay (spec 2.1). advance() is called exactly once per
 * tick, in Phase 13 EndTick (spec 1.3).
 */
class SimulationClock {
public:
	SimulationClock() = default;

	/// The current authoritative tick. Tick 0 is the initial state.
	tick_id_t current_tick() const { return this->current; }

	/// Advance to T+1. Called by DeterministicSimulationLoop Phase 13 only.
	void advance() { ++this->current; }

private:
	tick_id_t current{0};
};

} // namespace openage::gamestate::sim
```

Create `libopenage/gamestate/sim/simulation_clock.cpp`:

```cpp
// Copyright 2013-2026 the openage authors. See copying.md for legal info.

#include "simulation_clock.h"

namespace openage::gamestate::sim {
// All members are inline; this translation unit exists for future
// non-inline state and to keep the header lightweight.
}
```

- [ ] **Step 4: Register the .cpp in CMake**

Modify `libopenage/gamestate/sim/CMakeLists.txt` to:

```cmake
add_sources(libopenage
	simulation_clock.cpp
	sim_test.cpp
)
```

- [ ] **Step 5: Build + run test to verify it passes**

Run: `ninja -C bin -j$(nproc) 2>&1 | tail -5`
Expected: build OK.

Run: `.venv/bin/python -m openage test "openage::gamestate::sim::tests::clock_advances" 2>&1 | tail -15`
Expected: PASS (green `[test cpp]` line, no failure count).

- [ ] **Step 6: Commit**

```bash
git add libopenage/gamestate/sim/simulation_clock.h libopenage/gamestate/sim/simulation_clock.cpp libopenage/gamestate/sim/sim_test.cpp libopenage/gamestate/sim/CMakeLists.txt openage/testing/testlist.py
git commit -m "sim: add SimulationClock (tick_id_t, advance) + clock_advances test"
```

---

### Task 3: `PhaseSystemScheduler` with freeze (gates G, H)

**Files:**
- Create: `libopenage/gamestate/sim/phase_system_scheduler.h`
- Create: `libopenage/gamestate/sim/phase_system_scheduler.cpp`
- Modify: `libopenage/gamestate/sim/sim_test.cpp` (append tests)
- Modify: `libopenage/gamestate/sim/CMakeLists.txt`
- Modify: `openage/testing/testlist.py`

**Interfaces:**
- Consumes: `SystemId`, `TickPhase` from `types.h`
- Produces: `IPhaseSystem` (base), `PhaseSystemDescriptor`, `PhaseSystemScheduler::register_system/freeze/run_phase`. `freeze()` throws on duplicate id or dependency cycle.

- [ ] **Step 1: Write the failing tests**

Append to `libopenage/gamestate/sim/sim_test.cpp` (add the include at top too):

Add to the top includes block:
```cpp
#include "gamestate/sim/phase_system_scheduler.h"
```

Append inside `namespace openage::gamestate::sim::tests {`:

```cpp
// Trivial no-op phase system for scheduler tests.
class NullPhaseSystem : public IPhaseSystem {
public:
	void run(SimulationContext &) override {}
};

// Gate G: duplicate SystemId -> freeze() FAILS.
void scheduler_rejects_duplicate_id() {
	PhaseSystemScheduler sched;
	auto desc = PhaseSystemDescriptor{SystemId{1}, TickPhase::BeginTick, 0, {}};
	sched.register_system(desc, std::make_unique<NullPhaseSystem>());
	bool threw = false;
	try {
		sched.register_system(desc, std::make_unique<NullPhaseSystem>());
		sched.freeze();
	} catch (const std::runtime_error &) {
		threw = true;
	}
	threw or TESTFAIL;
}

// Gate H: dependency cycle -> freeze() FAILS.
void scheduler_rejects_cycle() {
	PhaseSystemScheduler sched;
	sched.register_system({SystemId{1}, TickPhase::BeginTick, 0, {SystemId{2}}},
	                      std::make_unique<NullPhaseSystem>());
	sched.register_system({SystemId{2}, TickPhase::BeginTick, 0, {SystemId{1}}},
	                      std::make_unique<NullPhaseSystem>());
	bool threw = false;
	try {
		sched.freeze();
	} catch (const std::runtime_error &) {
		threw = true;
	}
	threw or TESTFAIL;
}

// Sanity: a clean acyclic registration freezes and runs its phase.
void scheduler_freezes_and_runs_clean() {
	PhaseSystemScheduler sched;
	int ran_count = 0;
	class CountingSystem : public IPhaseSystem {
	public:
		int *count;
		CountingSystem(int *c) : count{c} {}
		void run(SimulationContext &) override { ++*count; }
	};
	sched.register_system({SystemId{1}, TickPhase::EndTick, 0, {}},
	                      std::make_unique<CountingSystem>(&ran_count));
	sched.freeze();
	// run_phase needs a context; pass a default-constructed one.
	SimulationContext ctx{};
	sched.run_phase(TickPhase::EndTick, ctx);
	TESTEQUALS(ran_count, 1);
}
```

Note: `SimulationContext` does not exist yet; we need a forward-declared/empty version now. Add a minimal `simulation_context.h` in this task so the test compiles.

- [ ] **Step 2: Run tests to verify they fail**

Register the three tests in `openage/testing/testlist.py::tests_cpp()`:
```python
    yield "openage::gamestate::sim::tests::scheduler_rejects_duplicate_id"
    yield "openage::gamestate::sim::tests::scheduler_rejects_cycle"
    yield "openage::gamestate::sim::tests::scheduler_freezes_and_runs_clean"
```

Run: `.venv/bin/python -m openage test "openage::gamestate::sim::tests::scheduler_rejects_duplicate_id" 2>&1 | tail -10`
Expected: FAIL - `phase_system_scheduler.h` / `SimulationContext` not found.

- [ ] **Step 3: Write minimal implementation**

Create `libopenage/gamestate/sim/simulation_context.h` (minimal, expanded later tasks):

```cpp
// Copyright 2013-2026 the openage authors. See copying.md for legal info.

#pragma once

#include "gamestate/sim/simulation_clock.h"

namespace openage::gamestate::sim {

/**
 * Per-tick context handed to phase systems. Grows as subsystems are added
 * (queues, mutation buffer, registries). M0 keeps it minimal: just the clock.
 */
class SimulationContext {
public:
	SimulationContext() = default;
	explicit SimulationContext(SimulationClock *clock) : clock{clock} {}

	tick_id_t current_tick() const { return clock ? clock->current_tick() : tick_id_t{0}; }

private:
	// Pointer (non-owning) so the loop owns the clock; systems read it.
	SimulationClock *clock{nullptr};
};

} // namespace openage::gamestate::sim
```

Create `libopenage/gamestate/sim/phase_system_scheduler.h`:

```cpp
// Copyright 2013-2026 the openage authors. See copying.md for legal info.

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "gamestate/sim/simulation_context.h"
#include "gamestate/sim/types.h"

namespace openage::gamestate::sim {

class IPhaseSystem {
public:
	virtual ~IPhaseSystem() = default;
	virtual void run(SimulationContext &ctx) = 0;
};

/// Descriptor for a registered phase system (spec 2.2).
struct PhaseSystemDescriptor {
	SystemId id;
	TickPhase phase;
	uint32_t stable_order;             // explicit order WITHIN a phase
	std::vector<SystemId> runs_after;   // optional deps; topo-resolved at freeze
};

/**
 * Registers continuous per-tick systems, freezes them into one canonical
 * order (topo sort by runs_after; tie-break by (stable_order, SystemId)),
 * and runs them per phase. Spec 2.2.
 *
 * Registration call order is NEVER relied upon. Duplicate id or dependency
 * cycle -> freeze() throws. Registry manifest/hash written to replay header
 * (Task 9).
 */
class PhaseSystemScheduler {
public:
	void register_system(PhaseSystemDescriptor desc, std::unique_ptr<IPhaseSystem> sys);

	/// Resolve the canonical order. Throws on duplicate id or cycle.
	void freeze();

	/// Run all frozen systems assigned to `phase`, in canonical order.
	void run_phase(TickPhase phase, SimulationContext &ctx);

	bool is_frozen() const { return this->frozen; }

private:
	struct Entry {
		PhaseSystemDescriptor desc;
		std::unique_ptr<IPhaseSystem> system;
	};
	std::vector<Entry> entries;
	std::vector<size_t> order;   // indices into entries, canonical order
	bool frozen{false};
};

} // namespace openage::gamestate::sim
```

Create `libopenage/gamestate/sim/phase_system_scheduler.cpp`:

```cpp
// Copyright 2013-2026 the openage authors. See copying.md for legal info.

#include "phase_system_scheduler.h"

#include <stdexcept>
#include <unordered_map>

namespace openage::gamestate::sim {

void PhaseSystemScheduler::register_system(PhaseSystemDescriptor desc,
                                           std::unique_ptr<IPhaseSystem> sys) {
	this->entries.push_back(Entry{std::move(desc), std::move(sys)});
}

void PhaseSystemScheduler::freeze() {
	// Detect duplicate ids.
	std::unordered_map<SystemId, size_t, std::hash<SystemId>> seen;
	for (size_t i = 0; i < this->entries.size(); ++i) {
		const auto id = this->entries[i].desc.id;
		if (seen.count(id)) {
			throw std::runtime_error("PhaseSystemScheduler: duplicate SystemId");
		}
		seen[id] = i;
	}

	// Build adjacency: for each entry, edges from each runs_after -> entry.
	// Kahn's algorithm for topo sort.
	const size_t n = this->entries.size();
	std::vector<std::vector<size_t>> adj(n);
	std::vector<size_t> indeg(n, 0);
	for (size_t i = 0; i < n; ++i) {
		for (const auto &dep : this->entries[i].desc.runs_after) {
			auto it = seen.find(dep);
			if (it == seen.end()) {
				throw std::runtime_error("PhaseSystemScheduler: runs_after id not registered");
			}
			adj[it->second].push_back(i);
			++indeg[i];
		}
	}

	// Seed with indeg==0, tie-broken by (stable_order, SystemId).
	std::vector<size_t> ready;
	for (size_t i = 0; i < n; ++i) {
		if (indeg[i] == 0) ready.push_back(i);
	}
	auto key = [&](size_t i) {
		return std::tuple(this->entries[i].desc.stable_order,
		                  this->entries[i].desc.id.value);
	};
	std::sort(ready.begin(), ready.end(),
	          [&](size_t a, size_t b) { return key(a) < key(b); });

	this->order.clear();
	std::vector<size_t> q = ready;
	while (!q.empty()) {
		// Pop the smallest key (stable_order, SystemId) for determinism.
		std::sort(q.begin(), q.end(),
		          [&](size_t a, size_t b) { return key(a) < key(b); });
		size_t node = q.front();
		q.erase(q.begin());
		this->order.push_back(node);
		for (size_t next : adj[node]) {
			if (--indeg[next] == 0) q.push_back(next);
		}
	}

	if (this->order.size() != n) {
		throw std::runtime_error("PhaseSystemScheduler: dependency cycle detected");
	}
	this->frozen = true;
}

void PhaseSystemScheduler::run_phase(TickPhase phase, SimulationContext &ctx) {
	for (size_t idx : this->order) {
		if (this->entries[idx].desc.phase == phase) {
			this->entries[idx].system->run(ctx);
		}
	}
}

} // namespace openage::gamestate::sim
```

Add the missing include in the .cpp: `#include <algorithm>` and `#include <tuple>` at the top of `phase_system_scheduler.cpp`.

- [ ] **Step 4: Register in CMake**

Modify `libopenage/gamestate/sim/CMakeLists.txt` to:

```cmake
add_sources(libopenage
	simulation_clock.cpp
	phase_system_scheduler.cpp
	sim_test.cpp
)
```

- [ ] **Step 5: Build + run tests to verify they pass**

Run: `ninja -C bin -j$(nproc) 2>&1 | tail -5`
Expected: build OK.

Run: `.venv/bin/python -m openage test "openage::gamestate::sim::tests::scheduler_freezes_and_runs_clean" "openage::gamestate::sim::tests::scheduler_rejects_duplicate_id" "openage::gamestate::sim::tests::scheduler_rejects_cycle" 2>&1 | tail -15`
Expected: all three PASS.

- [ ] **Step 6: Commit**

```bash
git add libopenage/gamestate/sim/simulation_context.h libopenage/gamestate/sim/phase_system_scheduler.h libopenage/gamestate/sim/phase_system_scheduler.cpp libopenage/gamestate/sim/sim_test.cpp libopenage/gamestate/sim/CMakeLists.txt openage/testing/testlist.py
git commit -m "sim: add PhaseSystemScheduler (topo freeze, dup/cycle reject) + SimulationContext"
```

---

### Task 4: `ActivityTaskRegistry` + `ActivityTaskResult` variant + `event_wait_spec.h`

**Files:**
- Create: `libopenage/gamestate/sim/event_wait_spec.h`
- Create: `libopenage/gamestate/sim/activity_task_registry.h`
- Create: `libopenage/gamestate/sim/activity_task_registry.cpp`
- Modify: `libopenage/gamestate/sim/sim_test.cpp`
- Modify: `libopenage/gamestate/sim/CMakeLists.txt`
- Modify: `openage/testing/testlist.py`

**Interfaces:**
- Consumes: `ActivityTaskId`, `entity_id_t`, `PathRequestId`/`AttackId`/`MovementGenerationKey` (all in `types.h` from Task 1)
- Produces: `event_wait_spec.h` (`CorrelationKey`/`EventMatchSpec`/`EventWaitSpec`, consumed by both this task's `TaskWaitForEvent` and Task 6's `TypedEventQueue`); `ActivityTaskResult` variant + `TaskCompleted`/`TaskWaitUntil`/`TaskWaitForEvent`/`TaskFailed`/`TaskInterrupted`; `ActivityTaskRegistry::register_handler/freeze/execute`. `register_handler` records seen ids; `freeze()` throws on duplicate id.

- [ ] **Step 1: Write the failing test**

Append to `sim_test.cpp` top includes:
```cpp
#include "gamestate/sim/activity_task_registry.h"
```

Append to the tests namespace:

```cpp
// A handler that always completes.
ActivityTaskResult always_complete(entity_id_t, const ActivityTaskParams &, ActivityExecutionContext &) {
	return TaskCompleted{};
}

void activity_registry_executes_handler() {
	ActivityTaskRegistry reg;
	reg.register_handler(ActivityTaskId{1}, always_complete);
	reg.freeze();
	ActivityTaskParams params{};
	ActivityExecutionContext ctx{};
	auto result = reg.execute(ActivityTaskId{1}, entity_id_t{42}, params, ctx);
	TESTEQUALS(result.index(), 0u); // variant index 0 = TaskCompleted
}

void activity_registry_rejects_duplicate_id() {
	ActivityTaskRegistry reg;
	reg.register_handler(ActivityTaskId{1}, always_complete);
	reg.register_handler(ActivityTaskId{1}, always_complete);
	bool threw = false;
	try {
		reg.freeze();
	} catch (const std::runtime_error &) {
		threw = true;
	}
	threw or TESTFAIL;
}
```

- [ ] **Step 2: Run tests to verify they fail**

Register in `openage/testing/testlist.py::tests_cpp()`:
```python
    yield "openage::gamestate::sim::tests::activity_registry_executes_handler"
    yield "openage::gamestate::sim::tests::activity_registry_rejects_duplicate_id"
```

Run: `.venv/bin/python -m openage test "openage::gamestate::sim::tests::activity_registry_executes_handler" 2>&1 | tail -10`
Expected: FAIL - `activity_task_registry.h` not found.

- [ ] **Step 3: Write minimal implementation**

First create the shared event-wait header (consumed by both this task's `TaskWaitForEvent` member and Task 6's `TypedEventQueue`, so neither owns it). Create `libopenage/gamestate/sim/event_wait_spec.h`:

```cpp
// Copyright 2013-2026 the openage authors. See copying.md for legal info.

#pragma once

#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include "gamestate/sim/types.h"

namespace openage::gamestate::sim {

// Correlation key tying an event to the in-flight work item that produced /
// waits on it (spec 2.6). Which alternative is active depends on event type.
using CorrelationKey = std::variant<PathRequestId, AttackId, MovementGenerationKey>;

struct EventMatchSpec {
	EventTypeId type;
	std::optional<entity_id_t> source;
	std::optional<entity_id_t> target;
	std::optional<CorrelationKey> correlation;
};

// WaitAny: an Activity waits on an OR-set of match specs (spec 2.6). Matches
// if ANY spec in any_of fires. activity_generation guards against stale events
// from a cancelled/replaced Activity.
struct EventWaitSpec {
	std::vector<EventMatchSpec> any_of;   // canonical sorted; matches if ANY fires
	uint32_t activity_generation{0};
};

} // namespace openage::gamestate::sim
```

Create `libopenage/gamestate/sim/activity_task_registry.h`:

```cpp
// Copyright 2013-2026 the openage authors. See copying.md for legal info.

#pragma once

#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <variant>

#include "gamestate/sim/event_wait_spec.h"
#include "gamestate/sim/types.h"

namespace openage::gamestate::sim {

/// Parameters carried into a task call (schema-defined data; no std::any).
struct ActivityTaskParams {
	// M0: empty. M1+ adds move target, attack target, etc.
};

/// Per-execution context (reset per tick; not in save/checksum).
struct ActivityExecutionContext {
	// M0: empty.
};

enum class ActivityTaskFailure : uint16_t {
	InvalidTarget, TargetUnavailable, PathUnavailable,
	InsufficientResources, PopulationLimit, ActivityCancelled, UnsupportedOperation
};

enum class ActivityInterruptReason : uint16_t {
	ReplacedByCommand, ForcedCommand, EntityDied, TargetInvalidated, ScenarioOverride
};

struct TaskCompleted {};
struct TaskWaitUntil { tick_id_t resume_tick; };
struct TaskWaitForEvent { EventWaitSpec wait_spec; };
struct TaskFailed { ActivityTaskFailure reason; };
struct TaskInterrupted { ActivityInterruptReason reason; };

using ActivityTaskResult = std::variant<
	TaskCompleted, TaskWaitUntil, TaskWaitForEvent, TaskFailed, TaskInterrupted>;

using ActivityTaskHandler = std::function<
	ActivityTaskResult(entity_id_t, const ActivityTaskParams &, ActivityExecutionContext &)>;

/**
 * Registry for Activity RunSystem node handlers (spec 2.2). Replaces the
 * activity.cpp central switch. register_handler records each id; freeze()
 * throws if any id was registered more than once (spec 2.15).
 */
class ActivityTaskRegistry {
public:
	void register_handler(ActivityTaskId id, ActivityTaskHandler handler);
	void freeze();
	ActivityTaskResult execute(ActivityTaskId id, entity_id_t subject,
	                           const ActivityTaskParams &params,
	                           ActivityExecutionContext &ctx) const;
	bool is_frozen() const { return this->frozen; }

private:
	std::unordered_map<ActivityTaskId, ActivityTaskHandler> handlers;
	std::unordered_set<ActivityTaskId> seen_ids;
	bool duplicate_seen{false};
	bool frozen{false};
};

} // namespace openage::gamestate::sim
```

Create `libopenage/gamestate/sim/activity_task_registry.cpp`:

```cpp
// Copyright 2013-2026 the openage authors. See copying.md for legal info.

#include "activity_task_registry.h"

#include <stdexcept>

namespace openage::gamestate::sim {

void ActivityTaskRegistry::register_handler(ActivityTaskId id, ActivityTaskHandler handler) {
	if (!this->seen_ids.insert(id).second) {
		// Second registration of the same id: record the violation; freeze()
		// will throw (matches PhaseSystemScheduler's duplicate-id contract).
		this->duplicate_seen = true;
	}
	this->handlers[id] = std::move(handler);
}

void ActivityTaskRegistry::freeze() {
	if (this->duplicate_seen) {
		throw std::runtime_error("ActivityTaskRegistry: duplicate ActivityTaskId registered");
	}
	this->frozen = true;
}

ActivityTaskResult ActivityTaskRegistry::execute(ActivityTaskId id, entity_id_t subject,
                                                 const ActivityTaskParams &params,
                                                 ActivityExecutionContext &ctx) const {
	if (!this->frozen) {
		throw std::runtime_error("ActivityTaskRegistry: execute before freeze");
	}
	auto it = this->handlers.find(id);
	if (it == this->handlers.end()) {
		return TaskFailed{ActivityTaskFailure::UnsupportedOperation};
	}
	return it->second(subject, params, ctx);
}

} // namespace openage::gamestate::sim
```

- [ ] **Step 4: Register in CMake**

`libopenage/gamestate/sim/CMakeLists.txt` -> add `activity_task_registry.cpp` to the list.

- [ ] **Step 5: Build + run tests**

Run: `ninja -C bin -j$(nproc) 2>&1 | tail -5`
Run: `.venv/bin/python -m openage test "openage::gamestate::sim::tests::activity_registry_executes_handler" "openage::gamestate::sim::tests::activity_registry_rejects_duplicate_id" 2>&1 | tail -10`
Expected: both PASS.

- [ ] **Step 6: Commit**

```bash
git add libopenage/gamestate/sim/event_wait_spec.h libopenage/gamestate/sim/activity_task_registry.h libopenage/gamestate/sim/activity_task_registry.cpp libopenage/gamestate/sim/sim_test.cpp libopenage/gamestate/sim/CMakeLists.txt openage/testing/testlist.py
git commit -m "sim: add ActivityTaskRegistry + ActivityTaskResult variant + event_wait_spec.h"
```

---

### Task 5: `WorldMutationBuffer` (stage + commit-at-phase)

**Files:**
- Create: `libopenage/gamestate/sim/world_mutation_buffer.h`
- Create: `libopenage/gamestate/sim/world_mutation_buffer.cpp`
- Modify: `libopenage/gamestate/sim/sim_test.cpp`
- Modify: `libopenage/gamestate/sim/CMakeLists.txt`
- Modify: `openage/testing/testlist.py`

**Interfaces:**
- Consumes: `tick_id_t` from `types.h`
- Produces: `WorldMutationBuffer` with `stage_int(key, value)`, `commit()`, `rollback()`, `pending_count()`. Mutations stage during a phase and commit at a fixed point (spec 1.5).

- [ ] **Step 1: Write the failing test**

Append include:
```cpp
#include "gamestate/sim/world_mutation_buffer.h"
```

Append test:

```cpp
// Mutations stage but do not commit until commit() is called.
void mutation_buffer_stages_and_commits() {
	WorldMutationBuffer buf;
	buf.stage_int("entity:1:hp", 50);
	TESTEQUALS(buf.pending_count(), 1u);
	buf.stage_int("entity:1:hp", 30); // overwrite staged value
	TESTEQUALS(buf.pending_count(), 1u);
	TESTEQUALS(buf.read_int("entity:1:hp", 0), 50); // committed value unchanged

	buf.commit();
	TESTEQUALS(buf.pending_count(), 0u);
	TESTEQUALS(buf.read_int("entity:1:hp", 0), 30); // now committed
}

// rollback() discards staged but uncommitted mutations.
void mutation_buffer_rollback_discards() {
	WorldMutationBuffer buf;
	buf.stage_int("entity:1:hp", 99);
	buf.rollback();
	TESTEQUALS(buf.pending_count(), 0u);
	TESTEQUALS(buf.read_int("entity:1:hp", 0), 0); // never committed
}
```

- [ ] **Step 2: Run test to verify it fails**

Register:
```python
    yield "openage::gamestate::sim::tests::mutation_buffer_stages_and_commits"
    yield "openage::gamestate::sim::tests::mutation_buffer_rollback_discards"
```

Run: `.venv/bin/python -m openage test "openage::gamestate::sim::tests::mutation_buffer_stages_and_commits" 2>&1 | tail -10`
Expected: FAIL - `world_mutation_buffer.h` not found.

- [ ] **Step 3: Write minimal implementation**

Create `libopenage/gamestate/sim/world_mutation_buffer.h`:

```cpp
// Copyright 2013-2026 the openage authors. See copying.md for legal info.

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace openage::gamestate::sim {

/**
 * Stages field writes during a phase and commits them at a fixed phase
 * position (spec 1.5). Prevents arbitrary systems from obtaining
 * unrestricted mutable component pointers.
 *
 * M0 is a minimal int-keyed buffer sufficient to prove the stage/commit
 * contract and the must-empty-at-tick-end invariant. M1+ specializes it
 * per component field.
 */
class WorldMutationBuffer {
public:
	/// Stage an int-valued field write (overwrites prior staged value for key).
	void stage_int(const std::string &key, int64_t value);

	/// Commit all staged mutations; clears the staging area.
	void commit();

	/// Discard all staged mutations (no committed values change).
	void rollback();

	/// Number of staged-but-uncommitted mutations.
	size_t pending_count() const { return this->staged.size(); }

	/// Read the committed value for a key (default if none).
	int64_t read_int(const std::string &key, int64_t fallback) const;

private:
	std::unordered_map<std::string, int64_t> staged;
	std::unordered_map<std::string, int64_t> committed;
};

} // namespace openage::gamestate::sim
```

Create `libopenage/gamestate/sim/world_mutation_buffer.cpp`:

```cpp
// Copyright 2013-2026 the openage authors. See copying.md for legal info.

#include "world_mutation_buffer.h"

namespace openage::gamestate::sim {

void WorldMutationBuffer::stage_int(const std::string &key, int64_t value) {
	this->staged[key] = value;
}

void WorldMutationBuffer::commit() {
	for (const auto &[k, v] : this->staged) {
		this->committed[k] = v;
	}
	this->staged.clear();
}

void WorldMutationBuffer::rollback() {
	this->staged.clear();
}

int64_t WorldMutationBuffer::read_int(const std::string &key, int64_t fallback) const {
	auto it = this->committed.find(key);
	if (it == this->committed.end()) return fallback;
	return it->second;
}

} // namespace openage::gamestate::sim
```

- [ ] **Step 4: Register in CMake**

Add `world_mutation_buffer.cpp` to `libopenage/gamestate/sim/CMakeLists.txt`.

- [ ] **Step 5: Build + run tests**

Run: `ninja -C bin -j$(nproc) 2>&1 | tail -5`
Run: `.venv/bin/python -m openage test "openage::gamestate::sim::tests::mutation_buffer_stages_and_commits" "openage::gamestate::sim::tests::mutation_buffer_rollback_discards" 2>&1 | tail -10`
Expected: both PASS.

- [ ] **Step 6: Commit**

```bash
git add libopenage/gamestate/sim/world_mutation_buffer.h libopenage/gamestate/sim/world_mutation_buffer.cpp libopenage/gamestate/sim/sim_test.cpp libopenage/gamestate/sim/CMakeLists.txt openage/testing/testlist.py
git commit -m "sim: add WorldMutationBuffer (stage/commit/rollback)"
```

---

### Task 6: `TypedEventQueue` (6-tuple sort, WaitAny)

**Files:**
- Create: `libopenage/gamestate/sim/typed_event_queue.h`
- Create: `libopenage/gamestate/sim/typed_event_queue.cpp`
- Modify: `libopenage/gamestate/sim/sim_test.cpp`
- Modify: `libopenage/gamestate/sim/CMakeLists.txt`
- Modify: `openage/testing/testlist.py`

**Interfaces:**
- Consumes: `tick_id_t`, `EventTypeId`, `entity_id_t`, `EventId` from `types.h`; `CorrelationKey`/`EventMatchSpec`/`EventWaitSpec` from `event_wait_spec.h` (Task 4)
- Produces: `EventOriginPhase`, `EventHeader`, `EventEnvelope`, `EventPayload` (`PathReady`), `TypedEventQueue::enqueue/drain_sorted/matches`. Sort key = 6-tuple per spec 2.7.

- [ ] **Step 1: Write the failing test**

Append include:
```cpp
#include "gamestate/sim/typed_event_queue.h"
```

Append tests:

```cpp
// Gate E (partial): events drain in strict 6-tuple order regardless of
// enqueue order.
void typed_events_drain_in_sorted_order() {
	TypedEventQueue q;
	// Two events same tick, same origin, same producer order 0/1.
	q.enqueue(EventEnvelope{
	    EventHeader{tick_id_t{5}, EventOriginPhase::LocomotionAndSpatial,
	                SystemId{1}, entity_id_t{1}, EventTypeId{10}, 1u},
	    EventPayload{PathReady{}}});
	q.enqueue(EventEnvelope{
	    EventHeader{tick_id_t{5}, EventOriginPhase::LocomotionAndSpatial,
	                SystemId{1}, entity_id_t{1}, EventTypeId{10}, 0u},
	    EventPayload{PathReady{}}});
	auto drained = q.drain_sorted();
	TESTEQUALS(drained.size(), 2u);
	// local_sequence 0 sorts before 1.
	TESTEQUALS(drained[0].header.producer_local_sequence, 0u);
	TESTEQUALS(drained[1].header.producer_local_sequence, 1u);
}

// WaitAny: a wait spec matches if ANY of its match specs fires.
void event_wait_any_matches() {
	EventWaitSpec wait;
	wait.any_of = {
	    EventMatchSpec{EventTypeId{10}, entity_id_t{1}, {}, {}},
	    EventMatchSpec{EventTypeId{11}, {}, entity_id_t{2}, {}},
	};
	EventEnvelope ev{
	    EventHeader{tick_id_t{5}, EventOriginPhase::Pathfinding,
	                SystemId{1}, entity_id_t{1}, EventTypeId{11}, 0u},
	    EventPayload{PathReady{}}};
	TESTEQUALS(TypedEventQueue::matches(wait, ev), true);

	EventEnvelope nope{
	    EventHeader{tick_id_t{5}, EventOriginPhase::Pathfinding,
	                SystemId{1}, entity_id_t{9}, EventTypeId{99}, 0u},
	    EventPayload{PathReady{}}};
	TESTEQUALS(TypedEventQueue::matches(wait, nope), false);
}
```

Note: `PathReady`, `EventHeader`, `EventPayload`, `CorrelationKey` need definitions. Define a minimal `PathReady` and the envelope/header structs in the header.

- [ ] **Step 2: Run tests to verify they fail**

Register:
```python
    yield "openage::gamestate::sim::tests::typed_events_drain_in_sorted_order"
    yield "openage::gamestate::sim::tests::event_wait_any_matches"
```

Run: `.venv/bin/python -m openage test "openage::gamestate::sim::tests::typed_events_drain_in_sorted_order" 2>&1 | tail -10`
Expected: FAIL - `typed_event_queue.h` not found.

- [ ] **Step 3: Write minimal implementation**

Create `libopenage/gamestate/sim/typed_event_queue.h`:

```cpp
// Copyright 2013-2026 the openage authors. See copying.md for legal info.

#pragma once

#include <cstdint>
#include <variant>
#include <vector>

#include "gamestate/sim/event_wait_spec.h"
#include "gamestate/sim/types.h"

namespace openage::gamestate::sim {

enum class EventOriginPhase : uint8_t {
	Pathfinding, LocomotionAndSpatial, CombatAndProjectile,
	DamageAndDeath, GatherBuildProduction, DeferredEntityCommit
};

// Minimal payloads for M0. Full variant per spec 2.6 added as systems arrive.
struct PathReady {};
using EventPayload = std::variant<PathReady>;

struct EventHeader {
	tick_id_t scheduled_tick;
	EventOriginPhase origin_phase;
	SystemId producer_system;            // uses SystemId stable_order for sort
	entity_id_t source_entity;
	EventTypeId event_type;
	uint32_t producer_local_sequence;    // 6-tuple tiebreak (spec 2.7)
};

struct EventEnvelope {
	EventHeader header;
	EventPayload payload;
};

// CorrelationKey / EventMatchSpec / EventWaitSpec come from event_wait_spec.h
// (Task 4) - shared with ActivityTaskRegistry's TaskWaitForEvent.

/**
 * Authoritative typed event queue (spec 2.6). Independent of the legacy
 * EventLoop. drain_sorted() returns events in strict 6-tuple order.
 */
class TypedEventQueue {
public:
	void enqueue(EventEnvelope env);
	std::vector<EventEnvelope> drain_sorted();

	static bool matches(const EventWaitSpec &wait, const EventEnvelope &env);

private:
	std::vector<EventEnvelope> pending;
};

} // namespace openage::gamestate::sim
```

`PathRequestId`/`AttackId`/`MovementGenerationKey` (used by `CorrelationKey` in `event_wait_spec.h`) and `EventWaitSpec`/`EventMatchSpec`/`CorrelationKey` are already defined (Task 1 `types.h` and Task 4 `event_wait_spec.h`). Do NOT re-define them here.

Create `libopenage/gamestate/sim/typed_event_queue.cpp`:

```cpp
// Copyright 2013-2026 the openage authors. See copying.md for legal info.

#include "typed_event_queue.h"

#include <algorithm>

namespace openage::gamestate::sim {

void TypedEventQueue::enqueue(EventEnvelope env) {
	this->pending.push_back(std::move(env));
}

std::vector<EventEnvelope> TypedEventQueue::drain_sorted() {
	// 6-tuple strict total order (spec 2.7):
	// (scheduled_tick, origin_phase, producer_system.value, source_entity.value,
	//  event_type.value, producer_local_sequence)
	auto key = [](const EventEnvelope &e) {
		return std::tuple(e.header.scheduled_tick,
		                  static_cast<uint8_t>(e.header.origin_phase),
		                  e.header.producer_system.value,
		                  e.header.source_entity.value,
		                  e.header.event_type.value,
		                  e.header.producer_local_sequence);
	};
	std::sort(this->pending.begin(), this->pending.end(),
	          [&](const EventEnvelope &a, const EventEnvelope &b) {
		          return key(a) < key(b);
	          });
	std::vector<EventEnvelope> out;
	out.swap(this->pending);
	return out;
}

bool TypedEventQueue::matches(const EventWaitSpec &wait, const EventEnvelope &env) {
	for (const auto &spec : wait.any_of) {
		if (spec.type != env.header.event_type) continue;
		if (spec.source && *spec.source != env.header.source_entity) continue;
		// target/correlation: PathReady carries neither in M0; skip if set.
		return true;
	}
	return false;
}

} // namespace openage::gamestate::sim
```

- [ ] **Step 4: Register in CMake**

Add `typed_event_queue.cpp` to `libopenage/gamestate/sim/CMakeLists.txt`.

- [ ] **Step 5: Build + run tests**

Run: `ninja -C bin -j$(nproc) 2>&1 | tail -5`
Run: `.venv/bin/python -m openage test "openage::gamestate::sim::tests::typed_events_drain_in_sorted_order" "openage::gamestate::sim::tests::event_wait_any_matches" 2>&1 | tail -10`
Expected: both PASS.

- [ ] **Step 6: Commit**

```bash
git add libopenage/gamestate/sim/typed_event_queue.h libopenage/gamestate/sim/typed_event_queue.cpp libopenage/gamestate/sim/types.h libopenage/gamestate/sim/sim_test.cpp libopenage/gamestate/sim/CMakeLists.txt openage/testing/testlist.py
git commit -m "sim: add TypedEventQueue (6-tuple sort, WaitAny) + event id types"
```

---

### Task 7: `PhaseWorkQueue<T>` template + `WorkQueueRegistry` (gate J)

**Files:**
- Create: `libopenage/gamestate/sim/phase_work_queue.h`
- Create: `libopenage/gamestate/sim/work_queue_registry.h`
- Create: `libopenage/gamestate/sim/work_queue_registry.cpp`
- Modify: `libopenage/gamestate/sim/sim_test.cpp`
- Modify: `libopenage/gamestate/sim/CMakeLists.txt`
- Modify: `openage/testing/testlist.py`

**Interfaces:**
- Consumes: nothing (template + registry of raw pointers)
- Produces: `PhaseWorkQueue<T>` (enqueue/sorted_drain, `must_empty` flag, sort comparator); `WorkQueueRegistry::register_queue/assert_all_empty_at_tick_end`.

- [ ] **Step 1: Write the failing test**

Append includes:
```cpp
#include "gamestate/sim/phase_work_queue.h"
#include "gamestate/sim/work_queue_registry.h"
```

Append tests:

```cpp
// Gate J: a queue declared must_empty throws at tick-end if non-empty.
void work_queue_must_empty_assertion() {
	WorkQueueRegistry registry;
	PhaseWorkQueue<int> q{true /*must_empty*/};
	registry.register_queue(&q);
	q.enqueue(7);
	bool threw = false;
	try {
		registry.assert_all_empty_at_tick_end();
	} catch (const std::runtime_error &) {
		threw = true;
	}
	threw or TESTFAIL;

	q.sorted_drain(); // now empty
	registry.assert_all_empty_at_tick_end(); // must not throw
}

// A queue NOT declared must_empty is not asserted.
void work_queue_optional_empty() {
	WorkQueueRegistry registry;
	PhaseWorkQueue<int> q{false /*must_empty*/};
	registry.register_queue(&q);
	q.enqueue(7);
	registry.assert_all_empty_at_tick_end(); // must not throw
	TESTEQUALS(q.sorted_drain().size(), 1u);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Register:
```python
    yield "openage::gamestate::sim::tests::work_queue_must_empty_assertion"
    yield "openage::gamestate::sim::tests::work_queue_optional_empty"
```

Run: `.venv/bin/python -m openage test "openage::gamestate::sim::tests::work_queue_must_empty_assertion" 2>&1 | tail -10`
Expected: FAIL - headers not found.

- [ ] **Step 3: Write minimal implementation**

Create `libopenage/gamestate/sim/work_queue_registry.h` first (it defines the erased base the template inherits):

```cpp
// Copyright 2013-2026 the openage authors. See copying.md for legal info.

#pragma once

#include <vector>

namespace openage::gamestate::sim {

class PhaseWorkQueueBase {
public:
	virtual ~PhaseWorkQueueBase() = default;
	virtual bool empty() const = 0;
	virtual bool is_must_empty() const = 0;
};

template <typename T>
class PhaseWorkQueue; // forward

// Erased accessor so the registry can hold heterogeneous queues.
class WorkQueueRegistry {
public:
	template <typename T>
	void register_queue(PhaseWorkQueue<T> *q);

	/// Throws if any must_empty queue is non-empty (spec 4.8, gate J).
	void assert_all_empty_at_tick_end();

private:
	std::vector<PhaseWorkQueueBase *> queues;
};

} // namespace openage::gamestate::sim
```

Then create `libopenage/gamestate/sim/phase_work_queue.h` inheriting that base:

```cpp
// Copyright 2013-2026 the openage authors. See copying.md for legal info.

#pragma once

#include <algorithm>
#include <stdexcept>
#include <vector>

#include "gamestate/sim/work_queue_registry.h"

namespace openage::gamestate::sim {

/**
 * Transient inter-phase work queue (spec 2.10). NOT persistent state.
 * Producer pushes in one phase; consumer drains (sorted) in a later phase.
 * If must_empty is set, WorkQueueRegistry asserts it is empty at tick end.
 *
 * Work queues transfer work ONLY. Persistent in-flight state lives in a
 * system-owned *StateStore (M1+: PathSearchStateStore etc.).
 */
template <typename T>
class PhaseWorkQueue : public PhaseWorkQueueBase {
public:
	explicit PhaseWorkQueue(bool must_empty) : must_empty{must_empty} {}

	void enqueue(T item) { this->items.push_back(std::move(item)); }

	std::vector<T> sorted_drain() {
		std::sort(this->items.begin(), this->items.end());
		std::vector<T> out;
		out.swap(this->items);
		return out;
	}

	bool empty() const override { return this->items.empty(); }
	size_t size() const { return this->items.size(); }
	bool is_must_empty() const override { return this->must_empty; }

private:
	std::vector<T> items;
	bool must_empty;
};

} // namespace openage::gamestate::sim
```

Create `libopenage/gamestate/sim/work_queue_registry.cpp`:

```cpp
// Copyright 2013-2026 the openage authors. See copying.md for legal info.

#include "work_queue_registry.h"

#include <stdexcept>

namespace openage::gamestate::sim {

template <typename T>
void WorkQueueRegistry::register_queue(PhaseWorkQueue<T> *q) {
	this->queues.push_back(static_cast<PhaseWorkQueueBase *>(q));
}

void WorkQueueRegistry::assert_all_empty_at_tick_end() {
	for (const auto *q : this->queues) {
		if (q->is_must_empty() && !q->empty()) {
			throw std::runtime_error("WorkQueueRegistry: must_empty queue non-empty at tick end");
		}
	}
}

// Explicit instantiation of the only used register_queue<int>.
template void WorkQueueRegistry::register_queue<int>(PhaseWorkQueue<int> *);

} // namespace openage::gamestate::sim
```

- [ ] **Step 4: Register in CMake**

Add `work_queue_registry.cpp` to `libopenage/gamestate/sim/CMakeLists.txt`.

- [ ] **Step 5: Build + run tests**

Run: `ninja -C bin -j$(nproc) 2>&1 | tail -5`
Run: `.venv/bin/python -m openage test "openage::gamestate::sim::tests::work_queue_must_empty_assertion" "openage::gamestate::sim::tests::work_queue_optional_empty" 2>&1 | tail -10`
Expected: both PASS.

- [ ] **Step 6: Commit**

```bash
git add libopenage/gamestate/sim/phase_work_queue.h libopenage/gamestate/sim/work_queue_registry.h libopenage/gamestate/sim/work_queue_registry.cpp libopenage/gamestate/sim/sim_test.cpp libopenage/gamestate/sim/CMakeLists.txt openage/testing/testlist.py
git commit -m "sim: add PhaseWorkQueue + WorkQueueRegistry (must-empty gate J)"
```

---

### Task 8: `DeterministicRNG` multi-stream (gate F)

**Files:**
- Create: `libopenage/gamestate/sim/deterministic_rng.h`
- Create: `libopenage/gamestate/sim/deterministic_rng.cpp`
- Modify: `libopenage/gamestate/sim/sim_test.cpp`
- Modify: `libopenage/gamestate/sim/CMakeLists.txt`
- Modify: `openage/testing/testlist.py`

**Interfaces:**
- Consumes: existing `rng::RNG` from `libopenage/rng/rng.h`
- Produces: `DeterministicRNG` with stable stream ids; `next(stream_id)`, `seed_all(seed)`, `serialize()`/`deserialize()`. Per-stream independence: consuming stream A does not change stream B's output.

- [ ] **Step 1: Write the failing test**

Append include:
```cpp
#include "gamestate/sim/deterministic_rng.h"
```

Append tests:

```cpp
// Gate F: streams are independent. Consuming stream A does not change B.
void rng_streams_independent() {
	DeterministicRNG rng;
	rng.seed_all(12345);
	uint64_t a1 = rng.next(0);
	uint64_t b1 = rng.next(1);
	uint64_t a2 = rng.next(0);
	uint64_t b2 = rng.next(1);
	// Re-seed and consume only A; B's first draw must equal b1.
	rng.seed_all(12345);
	rng.next(0); // consume A only
	TESTEQUALS(rng.next(1), b1);
	// And the original sequence A: a1, a2 deterministic on reseed.
	rng.seed_all(12345);
	TESTEQUALS(rng.next(0), a1);
	TESTEQUALS(rng.next(1), b1);
	TESTEQUALS(rng.next(0), a2);
	TESTEQUALS(rng.next(1), b2);
}

// Same seed -> identical stream; serialize/deserialize preserves state.
void rng_seed_and_serialize() {
	DeterministicRNG rng;
	rng.seed_all(99);
	uint64_t before = rng.next(0);
	auto bytes = rng.serialize();

	DeterministicRNG rng2;
	rng2.deserialize(bytes);
	TESTEQUALS(rng2.next(0), rng.next(0)); // both advanced one past `before`
}
```

- [ ] **Step 2: Run tests to verify they fail**

Register:
```python
    yield "openage::gamestate::sim::tests::rng_streams_independent"
    yield "openage::gamestate::sim::tests::rng_seed_and_serialize"
```

Run: `.venv/bin/python -m openage test "openage::gamestate::sim::tests::rng_streams_independent" 2>&1 | tail -10`
Expected: FAIL - `deterministic_rng.h` not found.

- [ ] **Step 3: Write minimal implementation**

Create `libopenage/gamestate/sim/deterministic_rng.h`:

```cpp
// Copyright 2013-2026 the openage authors. See copying.md for legal info.

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "rng/rng.h"

namespace openage::gamestate::sim {

/**
 * Deterministic multi-stream RNG (spec 2.18). Built on the existing
 * rng::RNG (xorshift128+). Stable stream ids; per-system / per-player /
 * per-scenario streams are independent: consuming one does not affect
 * another. Stream state is serialized for the replay header.
 */
class DeterministicRNG {
public:
	/// Seed every stream deterministically from `seed` (derived per-stream).
	void seed_all(uint64_t seed);

	/// Draw from stream `id`. Creates the stream lazily if absent.
	uint64_t next(uint32_t stream_id);

	/// Serialize all stream states (for replay header).
	std::string serialize() const;

	/// Restore all stream states.
	void deserialize(const std::string &bytes);

private:
	std::unordered_map<uint32_t, rng::RNG> streams;
};

} // namespace openage::gamestate::sim
```

Create `libopenage/gamestate/sim/deterministic_rng.cpp`:

```cpp
// Copyright 2013-2026 the openage authors. See copying.md for legal info.

#include "deterministic_rng.h"

#include <sstream>

namespace openage::gamestate::sim {

void DeterministicRNG::seed_all(uint64_t seed) {
	this->streams.clear();
	// Record the base seed; per-stream RNGs are created lazily in next() with
	// a seed derived deterministically from (base_seed, stream_id), so streams
	// are independent: consuming one never affects another (spec 2.18, gate F).
	this->base_seed = seed;
	this->base_seed_set = true;
}

uint64_t DeterministicRNG::next(uint32_t stream_id) {
	auto it = this->streams.find(stream_id);
	if (it == this->streams.end()) {
		// Derive a per-stream seed from base_seed xor stream_id (deterministic,
		// and distinct across streams).
		uint64_t s = this->base_seed ^ (static_cast<uint64_t>(stream_id) * 0x9E3779B97F4A7C15ULL);
		auto [ins, _] = this->streams.emplace(stream_id, rng::RNG{s});
		it = ins;
	}
	return it->second.random();
}

std::string DeterministicRNG::serialize() const {
	// Format: base_seed base_seed_set stream_count ; then per stream: id state-string.
	// rng::RNG exposes std::string to_string() const / void from_string(const std::string&).
	std::ostringstream out;
	out << this->base_seed << ' ' << this->base_seed_set << ' ' << this->streams.size() << '\n';
	for (const auto &[id, rng] : this->streams) {
		out << id << ' ' << rng.to_string() << '\n';
	}
	return out.str();
}

void DeterministicRNG::deserialize(const std::string &bytes) {
	// rng::RNG has an explicit constructor from std::string (calls from_string).
	std::istringstream in(bytes);
	in >> this->base_seed >> this->base_seed_set;
	size_t n;
	in >> n;
	this->streams.clear();
	for (size_t i = 0; i < n; ++i) {
		uint32_t id;
		std::string state;
		in >> id >> state;
		this->streams.emplace(id, rng::RNG{state});
	}
}

} // namespace openage::gamestate::sim
```

Add the new private members to `deterministic_rng.h` private section:
```cpp
	uint64_t base_seed{0};
	bool base_seed_set{false};
```

**rng::RNG API (verified):** the draw method is `uint64_t random()` (the .cpp above uses `it->second.random()` - correct). Serialization is `std::string to_string() const` and `RNG(const std::string &instr)` / `void from_string(const std::string &)` (the .cpp above uses `rng.to_string()` and `rng::RNG{state}` - correct). If `RNG::to_string()` output contains whitespace the `in >> state` token-read would split it; verify by running the `rng_seed_and_serialize` test - if it fails on round-trip, switch to storing `state` with a length prefix instead of `>>`.

- [ ] **Step 4: Register in CMake**

Add `deterministic_rng.cpp` to `libopenage/gamestate/sim/CMakeLists.txt`.

- [ ] **Step 5: Build + run tests**

Run: `ninja -C bin -j$(nproc) 2>&1 | tail -5`
Run: `.venv/bin/python -m openage test "openage::gamestate::sim::tests::rng_streams_independent" "openage::gamestate::sim::tests::rng_seed_and_serialize" 2>&1 | tail -10`
Expected: both PASS. If the rng::RNG API mismatched, fix the .cpp and rebuild.

- [ ] **Step 6: Commit**

```bash
git add libopenage/gamestate/sim/deterministic_rng.h libopenage/gamestate/sim/deterministic_rng.cpp libopenage/gamestate/sim/sim_test.cpp libopenage/gamestate/sim/CMakeLists.txt openage/testing/testlist.py
git commit -m "sim: add DeterministicRNG multi-stream on rng::RNG (gate F)"
```

---

### Task 9: `CanonicalStateSerializer` + `ChecksumSystem` + `ReplayHeader` (gates A, K, L)

**Files:**
- Create: `libopenage/gamestate/sim/canonical_state_serializer.h`
- Create: `libopenage/gamestate/sim/canonical_state_serializer.cpp`
- Create: `libopenage/gamestate/sim/checksum_system.h`
- Create: `libopenage/gamestate/sim/checksum_system.cpp`
- Create: `libopenage/gamestate/sim/replay_header.h`
- Create: `libopenage/gamestate/sim/replay_header.cpp`
- Modify: `libopenage/gamestate/sim/sim_test.cpp`
- Modify: `libopenage/gamestate/sim/CMakeLists.txt`
- Modify: `openage/testing/testlist.py`

**Interfaces:**
- Consumes: `SimulationClock`, `DeterministicRNG`, `PhaseSystemScheduler`
- Produces: `CanonicalStateSerializer::hash(world)`, `ChecksumSystem::compute(ctx)`, `ReplayHeader` with schema/data/scheduler/rng hashes. Single serializer (no duplicate impl).

- [ ] **Step 1: Write the failing tests**

Append includes:
```cpp
#include "gamestate/sim/canonical_state_serializer.h"
#include "gamestate/sim/checksum_system.h"
#include "gamestate/sim/replay_header.h"
```

Append tests:

```cpp
// Gate A (core): empty world checksum is stable across identical runs.
void checksum_empty_world_stable() {
	WorldSnapshot snap_a;
	WorldSnapshot snap_b;
	CanonicalStateSerializer s;
	TESTEQUALS(s.hash(snap_a), s.hash(snap_b));
}

// ReplayHeader hash mismatch -> refuse (gate K): build two headers with
// different schema hashes; equal-hash headers compare equal.
void replay_header_equality() {
	ReplayHeader h1, h2;
	h1.replay_schema_version = 1;
	h1.game_data_hash = 0xABCDEF;
	h2.replay_schema_version = 1;
	h2.game_data_hash = 0xABCDEF;
	TESTEQUALS(h1 == h2, true);
	h2.game_data_hash = 0x123456;
	TESTEQUALS(h1 == h2, false);
}

// ChecksumSystem produces a hash for the current tick (Phase 12 orchestration).
// Asserts STABILITY across ticks (empty world -> identical hash), not a
// specific value: siphash of empty bytes is a nonzero constant.
void checksum_system_runs() {
	SimulationClock clock;
	ChecksumSystem cs;
	WorldSnapshot snap;
	uint64_t h0 = cs.compute(clock, snap);
	clock.advance();
	uint64_t h1 = cs.compute(clock, snap);
	TESTEQUALS(h1, h0); // stable: empty world hashes identically every tick
}
```

Note: `WorldSnapshot` needs a minimal definition. Define it as an empty struct in `canonical_state_serializer.h` for M0 (M1+ adds entity/component state).

- [ ] **Step 2: Run tests to verify they fail**

Register:
```python
    yield "openage::gamestate::sim::tests::checksum_empty_world_stable"
    yield "openage::gamestate::sim::tests::replay_header_equality"
    yield "openage::gamestate::sim::tests::checksum_system_runs"
```

Run: `.venv/bin/python -m openage test "openage::gamestate::sim::tests::checksum_empty_world_stable" 2>&1 | tail -10`
Expected: FAIL - headers not found.

- [ ] **Step 3: Write minimal implementation**

Create `libopenage/gamestate/sim/canonical_state_serializer.h`:

```cpp
// Copyright 2013-2026 the openage authors. See copying.md for legal info.

#pragma once

#include <cstdint>

namespace openage::gamestate::sim {

/**
 * Minimal world snapshot for M0 (empty world). M1+ adds entity/component
 * state in canonical order. Used by the checksum and replay paths.
 */
struct WorldSnapshot {
	// M0: empty. tick_id is carried by the caller.
};

/**
 * Schema-defined canonical byte serialization (spec 2.17). Single serializer
 * (no duplicate impl in system/checksum.* + state_checksum.*).
 */
class CanonicalStateSerializer {
public:
	/// Canonical bytes of a snapshot.
	std::string to_bytes(const WorldSnapshot &snap) const;

	/// Stable hash of the canonical bytes.
	uint64_t hash(const WorldSnapshot &snap) const;
};

} // namespace openage::gamestate::sim
```

Create `libopenage/gamestate/sim/canonical_state_serializer.cpp`:

```cpp
// Copyright 2013-2026 the openage authors. See copying.md for legal info.

#include "canonical_state_serializer.h"

#include "util/hash.h"

namespace openage::gamestate::sim {

std::string CanonicalStateSerializer::to_bytes(const WorldSnapshot &snap) const {
	// M0: empty world -> empty canonical bytes. M1+ writes entities/components
	// in canonical (sorted) order.
	(void) snap;
	return "";
}

uint64_t CanonicalStateSerializer::hash(const WorldSnapshot &snap) const {
	auto bytes = to_bytes(snap);
	// openage's siphash (util/hash.h: class Siphash). Fixed deterministic key so
	// identical bytes -> identical hash across runs/builds (gate A, L). Siphash
	// of empty input is a nonzero constant, so tests assert stability not ==0.
	static constexpr std::array<uint8_t, 16> KEY{
		0x6f, 0x70, 0x65, 0x6e, 0x61, 0x67, 0x65, 0x21,
		0x72, 0x74, 0x73, 0x20, 0x73, 0x69, 0x6d, 0x21};
	util::Siphash hasher{KEY};
	return hasher.digest(reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size());
}

} // namespace openage::gamestate::sim
```

**util/hash.h API (verified):** it exposes `class Siphash` with `Siphash(std::array<uint8_t,16> key)` and `uint64_t digest(const uint8_t *data, size_t len_data)` - the .cpp above uses exactly those. Add `#include <array>` at the top of `canonical_state_serializer.cpp`.

Create `libopenage/gamestate/sim/checksum_system.h`:

```cpp
// Copyright 2013-2026 the openage authors. See copying.md for legal info.

#pragma once

#include <cstdint>

#include "gamestate/sim/simulation_clock.h"

namespace openage::gamestate::sim {

/**
 * Phase 12 orchestration (spec 2.17). Computes the per-tick state checksum
 * via the single CanonicalStateSerializer. M0: empty world -> constant hash.
 */
class ChecksumSystem {
public:
	/// Compute the checksum for `snap` at the clock's current tick.
	uint64_t compute(const SimulationClock &clock, const WorldSnapshot &snap);
};

} // namespace openage::gamestate::sim
```

Create `libopenage/gamestate/sim/checksum_system.cpp`:

```cpp
// Copyright 2013-2026 the openage authors. See copying.md for legal info.

#include "checksum_system.h"

#include "canonical_state_serializer.h"

namespace openage::gamestate::sim {

uint64_t ChecksumSystem::compute(const SimulationClock &clock, const WorldSnapshot &snap) {
	(void) clock;
	CanonicalStateSerializer s;
	return s.hash(snap);
}

} // namespace openage::gamestate::sim
```

Create `libopenage/gamestate/sim/replay_header.h`:

```cpp
// Copyright 2013-2026 the openage authors. See copying.md for legal info.

#pragma once

#include <cstdint>
#include <chrono>

namespace openage::gamestate::sim {

/**
 * Replay manifest header (spec 2.17). Replay acceptance (spec 2.17): NOT
 * "from empty state"; requires identical InitialSimulationManifest + identical
 * data/schema hashes + identical RNG state + identical command/effect stream.
 * Hash mismatch -> refuse to load.
 */
struct ReplayHeader {
	uint32_t replay_schema_version{1};
	uint64_t initial_simulation_manifest_hash{0};
	uint64_t game_data_hash{0};
	uint64_t map_scenario_id{0};
	uint64_t activity_graph_hash{0};
	uint64_t command_event_effect_workitem_schema_hash{0};
	uint64_t system_scheduler_manifest_hash{0};
	uint64_t resource_registry_hash{0};
	std::chrono::nanoseconds tick_duration{TICK_DURATION};
	std::string rng_initial_states;

	bool operator==(const ReplayHeader &o) const = default;
};

} // namespace openage::gamestate::sim
```

Create `libopenage/gamestate/sim/replay_header.cpp`:

```cpp
// Copyright 2013-2026 the openage authors. See copying.md for legal info.

#include "replay_header.h"

namespace openage::gamestate::sim {
// All members inline / defaulted.
}
```

- [ ] **Step 4: Register in CMake**

Add `canonical_state_serializer.cpp`, `checksum_system.cpp`, `replay_header.cpp` to `libopenage/gamestate/sim/CMakeLists.txt`.

- [ ] **Step 5: Build + run tests**

Run: `ninja -C bin -j$(nproc) 2>&1 | tail -5`
Run: `.venv/bin/python -m openage test "openage::gamestate::sim::tests::checksum_empty_world_stable" "openage::gamestate::sim::tests::replay_header_equality" "openage::gamestate::sim::tests::checksum_system_runs" 2>&1 | tail -10`
Expected: all PASS.

- [ ] **Step 6: Commit**

```bash
git add libopenage/gamestate/sim/canonical_state_serializer.h libopenage/gamestate/sim/canonical_state_serializer.cpp libopenage/gamestate/sim/checksum_system.h libopenage/gamestate/sim/checksum_system.cpp libopenage/gamestate/sim/replay_header.h libopenage/gamestate/sim/replay_header.cpp libopenage/gamestate/sim/sim_test.cpp libopenage/gamestate/sim/CMakeLists.txt openage/testing/testlist.py
git commit -m "sim: add CanonicalStateSerializer + ChecksumSystem + ReplayHeader (gates A,K,L)"
```

---

### Task 10: `CommandIngressQueue` + `CommandLog` (gate D, I)

**Files:**
- Create: `libopenage/gamestate/sim/command.h`
- Create: `libopenage/gamestate/sim/command_ingress_queue.h`
- Create: `libopenage/gamestate/sim/command_ingress_queue.cpp`
- Create: `libopenage/gamestate/sim/command_log.h`
- Create: `libopenage/gamestate/sim/command_log.cpp`
- Modify: `libopenage/gamestate/sim/sim_test.cpp`
- Modify: `libopenage/gamestate/sim/CMakeLists.txt`
- Modify: `openage/testing/testlist.py`

**Interfaces:**
- Consumes: `tick_id_t`, `entity_id_t` from `types.h`
- Produces: `IssuerRef`, `RootCommandId`, `Command`, `CommandHeader`, `CommandPayload`; `CommandIngressQueue::issue/drain_canonical`; `CommandLog::append/flush`. Sort key independent of arrival order (gate D).

- [ ] **Step 1: Write the failing tests**

Append include:
```cpp
#include "gamestate/sim/command_ingress_queue.h"
#include "gamestate/sim/command_log.h"
```

Append tests:

```cpp
// Gate D: arrival order shuffled -> canonical result identical.
void command_ingress_canonical_order() {
	auto make_cmd = [](uint64_t seq) {
		CommandHeader h;
		h.issuer.kind = IssuerKind::Player;
		h.issuer.stable_id = IssuerStableId{1};
		h.issued_tick = tick_id_t{5};
		h.execute_tick = tick_id_t{6};
		h.root_id = RootCommandId{IssuerKind::Player, IssuerStableId{1}, seq};
		return Command{h, CommandPayload{StopCommand{}}};
	};

	CommandIngressQueue q1, q2;
	q1.issue(make_cmd(3)); q1.issue(make_cmd(1)); q1.issue(make_cmd(2));
	q2.issue(make_cmd(1)); q2.issue(make_cmd(2)); q2.issue(make_cmd(3));
	auto d1 = q1.drain_canonical();
	auto d2 = q2.drain_canonical();
	TESTEQUALS(d1.size(), 3u);
	TESTEQUALS(d2.size(), 3u);
	for (size_t i = 0; i < d1.size(); ++i) {
		TESTEQUALS(d1[i].header.root_id.issuer_sequence,
		           d2[i].header.root_id.issuer_sequence);
	}
	// Canonical order is ascending issuer_sequence.
	TESTEQUALS(d1[0].header.root_id.issuer_sequence, 1ull);
	TESTEQUALS(d1[1].header.root_id.issuer_sequence, 2ull);
	TESTEQUALS(d1[2].header.root_id.issuer_sequence, 3ull);
}

// Gate I: execute_tick = issued_tick + input_delay (T+1 default).
void command_input_delay() {
	CommandHeader h;
	h.issued_tick = tick_id_t{5};
	h.execute_tick = tick_id_t{6}; // default T+1
	Command cmd{h, CommandPayload{StopCommand{}}};
	TESTEQUALS(cmd.header.execute_tick, tick_id_t{5 + 1});
}

// CommandLog is append-only; flush returns the log without clearing semantics
// that the sim would depend on mid-tick.
void command_log_append_and_flush() {
	CommandLog log;
	CommandHeader h;
	h.root_id = RootCommandId{IssuerKind::Player, IssuerStableId{1}, 1};
	Command cmd{h, CommandPayload{StopCommand{}}};
	log.append(cmd);
	TESTEQUALS(log.size(), 1u);
	auto flushed = log.flush();
	TESTEQUALS(flushed.size(), 1u);
}
```

Need `IssuerStableId`, `IssuerKind`, `QueuePlacement`, `CommandPriority` in `types.h` or `command.h`. Define them in `command.h`.

- [ ] **Step 2: Run tests to verify they fail**

Register:
```python
    yield "openage::gamestate::sim::tests::command_ingress_canonical_order"
    yield "openage::gamestate::sim::tests::command_input_delay"
    yield "openage::gamestate::sim::tests::command_log_append_and_flush"
```

Run: `.venv/bin/python -m openage test "openage::gamestate::sim::tests::command_ingress_canonical_order" 2>&1 | tail -10`
Expected: FAIL - headers not found.

- [ ] **Step 3: Write minimal implementation**

`IssuerStableId` and `IssuerKind` are already in `types.h` (Task 1). Do NOT re-define them.

Create `libopenage/gamestate/sim/command.h`:

```cpp
// Copyright 2013-2026 the openage authors. See copying.md for legal info.

#pragma once

#include <optional>
#include <variant>
#include <vector>

#include "gamestate/sim/types.h"

namespace openage::gamestate::sim {

/// Command identity, determinable at issue time (spec 2.4, non-circular).
struct RootCommandId {
	IssuerKind kind;
	IssuerStableId issuer;
	uint64_t issuer_sequence;
	bool operator==(const RootCommandId &) const = default;
};

struct IssuerRef {
	IssuerKind kind;
	IssuerStableId stable_id;
	std::optional<player_id_t> acting_player;
};

enum class QueuePlacement : uint8_t { Replace, Append, Prepend };
enum class CommandPriority : uint8_t { Background, Normal, Forced, SystemCritical };

struct CommandHeader {
	IssuerRef issuer{};
	tick_id_t issued_tick{0};
	tick_id_t execute_tick{0};
	QueuePlacement placement{QueuePlacement::Replace};
	CommandPriority priority{CommandPriority::Normal};
	RootCommandId root_id{};
};

// Minimal command payloads for M0 (StopCommand only). Full set per spec 2.4
// added as systems arrive (Move/Attack/Gather/Build/Train).
struct StopCommand {};
using CommandPayload = std::variant<StopCommand>;

struct Command {
	CommandHeader header;
	CommandPayload payload;
};

} // namespace openage::gamestate::sim
```

Create `libopenage/gamestate/sim/command_ingress_queue.h`:

```cpp
// Copyright 2013-2026 the openage authors. See copying.md for legal info.

#pragma once

#include <vector>

#include "gamestate/sim/command.h"

namespace openage::gamestate::sim {

/**
 * Live ingress for commands (spec 2.5). Player/AI/Scenario issue here.
 * drain_canonical() returns commands sorted by the root sort key, INDEPENDENT
 * of arrival order (gate D). The simulation never reads wall-clock input
 * timestamps.
 */
class CommandIngressQueue {
public:
	void issue(Command cmd);
	std::vector<Command> drain_canonical();

private:
	std::vector<Command> pending;
};

} // namespace openage::gamestate::sim
```

Create `libopenage/gamestate/sim/command_ingress_queue.cpp`:

```cpp
// Copyright 2013-2026 the openage authors. See copying.md for legal info.

#include "command_ingress_queue.h"

#include <algorithm>

namespace openage::gamestate::sim {

void CommandIngressQueue::issue(Command cmd) {
	this->pending.push_back(std::move(cmd));
}

std::vector<Command> CommandIngressQueue::drain_canonical() {
	// Root sort key (spec 2.4): (execute_tick, issuer_kind, issuer_stable_id,
	// issuer_sequence). No post-sort-assigned id; arrival order irrelevant.
	auto key = [](const Command &c) {
		return std::tuple(c.header.execute_tick,
		                  static_cast<uint8_t>(c.header.root_id.kind),
		                  c.header.root_id.issuer.value,
		                  c.header.root_id.issuer_sequence);
	};
	std::sort(this->pending.begin(), this->pending.end(),
	          [&](const Command &a, const Command &b) { return key(a) < key(b); });
	std::vector<Command> out;
	out.swap(this->pending);
	return out;
}

} // namespace openage::gamestate::sim
```

Create `libopenage/gamestate/sim/command_log.h`:

```cpp
// Copyright 2013-2026 the openage authors. See copying.md for legal info.

#pragma once

#include <vector>

#include "gamestate/sim/command.h"

namespace openage::gamestate::sim {

/**
 * Append-only replay artifact (spec 2.5). Flushed in Phase 12 only (single
 * authoritative write point). Live simulation does NOT drain the log.
 */
class CommandLog {
public:
	void append(Command cmd);
	size_t size() const { return this->log.size(); }
	std::vector<Command> flush();

private:
	std::vector<Command> log;
};

} // namespace openage::gamestate::sim
```

Create `libopenage/gamestate/sim/command_log.cpp`:

```cpp
// Copyright 2013-2026 the openage authors. See copying.md for legal info.

#include "command_log.h"

namespace openage::gamestate::sim {

void CommandLog::append(Command cmd) {
	this->log.push_back(std::move(cmd));
}

std::vector<Command> CommandLog::flush() {
	std::vector<Command> out;
	out.swap(this->log);
	return out;
}

} // namespace openage::gamestate::sim
```

- [ ] **Step 4: Register in CMake**

Add `command_ingress_queue.cpp`, `command_log.cpp` to `libopenage/gamestate/sim/CMakeLists.txt`.

- [ ] **Step 5: Build + run tests**

Run: `ninja -C bin -j$(nproc) 2>&1 | tail -5`
Run: `.venv/bin/python -m openage test "openage::gamestate::sim::tests::command_ingress_canonical_order" "openage::gamestate::sim::tests::command_input_delay" "openage::gamestate::sim::tests::command_log_append_and_flush" 2>&1 | tail -10`
Expected: all PASS.

- [ ] **Step 6: Commit**

```bash
git add libopenage/gamestate/sim/command.h libopenage/gamestate/sim/command_ingress_queue.h libopenage/gamestate/sim/command_ingress_queue.cpp libopenage/gamestate/sim/command_log.h libopenage/gamestate/sim/command_log.cpp libopenage/gamestate/sim/types.h libopenage/gamestate/sim/sim_test.cpp libopenage/gamestate/sim/CMakeLists.txt openage/testing/testlist.py
git commit -m "sim: add CommandIngressQueue + CommandLog (gates D, I)"
```

---

### Task 11: `DeterministicSimulationLoop` 14-phase skeleton (gates A, B, C, J)

**Files:**
- Create: `libopenage/gamestate/sim/deterministic_simulation_loop.h`
- Create: `libopenage/gamestate/sim/deterministic_simulation_loop.cpp`
- Modify: `libopenage/gamestate/sim/sim_test.cpp`
- Modify: `libopenage/gamestate/sim/CMakeLists.txt`
- Modify: `openage/testing/testlist.py`

**Interfaces:**
- Consumes: `SimulationClock`, `PhaseSystemScheduler`, `WorkQueueRegistry`, `CommandIngressQueue`, `CommandLog`, `ChecksumSystem`, `TypedEventQueue`, `WorldMutationBuffer`
- Produces: `DeterministicSimulationLoop::run_tick()`, `current_tick()`, `last_checksum()`. All 14 phases run in order; `current_tick` advanced in Phase 13 only; Phase 12 computes checksum + must-empty assertion; Phase 1 drains command ingress (empty in M0).

- [ ] **Step 1: Write the failing tests**

Append include:
```cpp
#include "gamestate/sim/deterministic_simulation_loop.h"
```

Append tests:

```cpp
// Gate A: 10 ticks of empty world -> checksum stable across two runs.
// Asserts STABILITY (the determinism property), not a specific value: siphash
// of empty bytes is a nonzero constant.
void loop_empty_world_10_ticks_stable() {
	auto run10 = []() {
		DeterministicSimulationLoop loop;
		for (int i = 0; i < 10; ++i) loop.run_tick();
		return loop.last_checksum();
	};
	const uint64_t h1 = run10();
	const uint64_t h2 = run10();
	TESTEQUALS(h1, h2); // stable across independent runs
}

// current_tick advances to T+1 per run_tick (Phase 13 EndTick).
void loop_advances_tick() {
	DeterministicSimulationLoop loop;
	TESTEQUALS(loop.current_tick(), tick_id_t{0});
	loop.run_tick();
	TESTEQUALS(loop.current_tick(), tick_id_t{1});
	loop.run_tick();
	TESTEQUALS(loop.current_tick(), tick_id_t{2});
}

// Gate B: catch-up runs the same ticks regardless of batch framing.
void loop_batch_invariant() {
	DeterministicSimulationLoop a, b;
	for (int i = 0; i < 5; ++i) a.run_tick();
	for (int i = 0; i < 3; ++i) b.run_tick();
	for (int i = 0; i < 2; ++i) b.run_tick(); // 3+2 = 5
	TESTEQUALS(a.current_tick(), b.current_tick());
	TESTEQUALS(a.last_checksum(), b.last_checksum());
}
```

- [ ] **Step 2: Run tests to verify they fail**

Register:
```python
    yield "openage::gamestate::sim::tests::loop_empty_world_10_ticks_stable"
    yield "openage::gamestate::sim::tests::loop_advances_tick"
    yield "openage::gamestate::sim::tests::loop_batch_invariant"
```

Run: `.venv/bin/python -m openage test "openage::gamestate::sim::tests::loop_empty_world_10_ticks_stable" 2>&1 | tail -10`
Expected: FAIL - `deterministic_simulation_loop.h` not found.

- [ ] **Step 3: Write minimal implementation**

Create `libopenage/gamestate/sim/deterministic_simulation_loop.h`:

```cpp
// Copyright 2013-2026 the openage authors. See copying.md for legal info.

#pragma once

#include <cstdint>

#include "gamestate/sim/checksum_system.h"
#include "gamestate/sim/command_ingress_queue.h"
#include "gamestate/sim/command_log.h"
#include "gamestate/sim/phase_system_scheduler.h"
#include "gamestate/sim/simulation_clock.h"
#include "gamestate/sim/typed_event_queue.h"
#include "gamestate/sim/world_mutation_buffer.h"
#include "gamestate/sim/work_queue_registry.h"

namespace openage::gamestate::sim {

/**
 * The sole gameplay driver (spec 1.3, 0.1). 14 frozen phases per tick.
 * current_tick = T+1 assigned in Phase 13 EndTick ONLY. Phase 12 computes
 * the checksum and asserts must_empty work queues. M0: empty world.
 */
class DeterministicSimulationLoop {
public:
	DeterministicSimulationLoop();

	/// Run one full tick (T -> T+1) through all 14 phases.
	void run_tick();

	tick_id_t current_tick() const { return this->clock.current_tick(); }
	uint64_t last_checksum() const { return this->last_hash; }

private:
	SimulationClock clock;
	PhaseSystemScheduler scheduler;
	WorkQueueRegistry work_queues;
	CommandIngressQueue command_ingress;
	CommandLog command_log;
	TypedEventQueue events;
	WorldMutationBuffer mutations;
	ChecksumSystem checksum;
	WorldSnapshot snapshot{}; // M0: empty
	uint64_t last_hash{0};

	// Per-phase methods (M0: mostly no-ops; the structure is the deliverable).
	void phase0_begin_tick();
	void phase1_ingest();
	void phase2_arbitrate();
	void phase3_activity();
	void phase4_pathfinding();
	void phase5_locomotion();
	void phase6_combat();
	void phase7_damage();
	void phase8_gather_build_production();
	void phase9_event_dispatch();
	void phase10_scenario_ai();
	void phase11_deferred_commit();
	void phase12_checksum_and_replay();
	void phase13_end_tick();
};

} // namespace openage::gamestate::sim
```

Create `libopenage/gamestate/sim/deterministic_simulation_loop.cpp`:

```cpp
// Copyright 2013-2026 the openage authors. See copying.md for legal info.

#include "deterministic_simulation_loop.h"

namespace openage::gamestate::sim {

DeterministicSimulationLoop::DeterministicSimulationLoop() = default;

void DeterministicSimulationLoop::run_tick() {
	this->phase0_begin_tick();
	this->phase1_ingest();
	this->phase2_arbitrate();
	this->phase3_activity();
	this->phase4_pathfinding();
	this->phase5_locomotion();
	this->phase6_combat();
	this->phase7_damage();
	this->phase8_gather_build_production();
	this->phase9_event_dispatch();
	this->phase10_scenario_ai();
	this->phase11_deferred_commit();
	this->phase12_checksum_and_replay();
	this->phase13_end_tick();
}

void DeterministicSimulationLoop::phase0_begin_tick() {
	// NOT current_tick++ (spec 1.3). No-op in M0.
}

void DeterministicSimulationLoop::phase1_ingest() {
	// Drain command ingress (empty in M0) into canonical order.
	auto cmds = this->command_ingress.drain_canonical();
	// No systems to hand them to yet; for M0 we just discard the drained
	// (empty) set. The CommandLog is appended in Phase 12.
	(void) cmds;
}

void DeterministicSimulationLoop::phase2_arbitrate() {
	// M0: no validation/arbitration systems registered.
}

void DeterministicSimulationLoop::phase3_activity() {
	// M0: no Activity graphs.
	SimulationContext ctx{&this->clock};
	this->scheduler.run_phase(TickPhase::ActivityEvaluation, ctx);
}

void DeterministicSimulationLoop::phase4_pathfinding() {}
void DeterministicSimulationLoop::phase5_locomotion() {
	SimulationContext ctx{&this->clock};
	this->scheduler.run_phase(TickPhase::LocomotionAndSpatial, ctx);
}
void DeterministicSimulationLoop::phase6_combat() {
	SimulationContext ctx{&this->clock};
	this->scheduler.run_phase(TickPhase::CombatAndProjectile, ctx);
}
void DeterministicSimulationLoop::phase7_damage() {
	SimulationContext ctx{&this->clock};
	this->scheduler.run_phase(TickPhase::DamageAndDeath, ctx);
}
void DeterministicSimulationLoop::phase8_gather_build_production() {
	SimulationContext ctx{&this->clock};
	this->scheduler.run_phase(TickPhase::GatherBuildProduction, ctx);
}
void DeterministicSimulationLoop::phase9_event_dispatch() {
	// Drain + dispatch events (empty in M0).
	this->events.drain_sorted();
}
void DeterministicSimulationLoop::phase10_scenario_ai() {
	SimulationContext ctx{&this->clock};
	this->scheduler.run_phase(TickPhase::ScenarioAndAIObservation, ctx);
}
void DeterministicSimulationLoop::phase11_deferred_commit() {
	SimulationContext ctx{&this->clock};
	this->scheduler.run_phase(TickPhase::DeferredEntityCommit, ctx);
}
void DeterministicSimulationLoop::phase12_checksum_and_replay() {
	// Gate J: assert must_empty work queues are empty.
	this->work_queues.assert_all_empty_at_tick_end();
	// Gate A: compute the per-tick checksum (empty world -> 0).
	this->last_hash = this->checksum.compute(this->clock, this->snapshot);
	// Commit any staged mutations (empty in M0) and flush the command log.
	this->mutations.commit();
	this->command_log.flush();
}
void DeterministicSimulationLoop::phase13_end_tick() {
	// current_tick = T+1 assigned HERE only (spec 1.3).
	this->clock.advance();
}

} // namespace openage::gamestate::sim
```

- [ ] **Step 4: Register in CMake**

Add `deterministic_simulation_loop.cpp` to `libopenage/gamestate/sim/CMakeLists.txt`.

- [ ] **Step 5: Build + run tests**

Run: `ninja -C bin -j$(nproc) 2>&1 | tail -5`
Run: `.venv/bin/python -m openage test "openage::gamestate::sim::tests::loop_empty_world_10_ticks_stable" "openage::gamestate::sim::tests::loop_advances_tick" "openage::gamestate::sim::tests::loop_batch_invariant" 2>&1 | tail -10`
Expected: all PASS.

- [ ] **Step 6: Commit**

```bash
git add libopenage/gamestate/sim/deterministic_simulation_loop.h libopenage/gamestate/sim/deterministic_simulation_loop.cpp libopenage/gamestate/sim/sim_test.cpp libopenage/gamestate/sim/CMakeLists.txt openage/testing/testlist.py
git commit -m "sim: add DeterministicSimulationLoop 14-phase skeleton (gates A,B,J)"
```

---

### Task 12: S0 full-suite integration test (gates A-L, 10k ticks + replay)

**Files:**
- Modify: `libopenage/gamestate/sim/sim_test.cpp` (append the integration test)
- Modify: `openage/testing/testlist.py`

**Interfaces:**
- Consumes: all M0 components
- Produces: `s0_full_gate` - runs 10,000 empty-world ticks, asserts checksum stability across two fresh loops, and asserts the replay-header equality contract (gate A + K). This is the M0 acceptance test.

- [ ] **Step 1: Write the failing test**

Append to `sim_test.cpp`:

```cpp
#include "gamestate/sim/deterministic_simulation_loop.h"

namespace openage::gamestate::sim::tests {

// Gate A (10,000 ticks) + Gate K (replay header equality contract):
// the M0 acceptance test. Empty world, two independent loops, identical
// per-tick checksum at 10k ticks; two identical ReplayHeaders compare equal.
void s0_full_gate() {
	constexpr int N = 10000;
	auto run = []() {
		DeterministicSimulationLoop loop;
		for (int i = 0; i < N; ++i) loop.run_tick();
		return loop.last_checksum();
	};
	const uint64_t h1 = run();
	const uint64_t h2 = run();
	TESTEQUALS(h1, h2);

	// ReplayHeader equality contract (gate K): identical manifests compare equal.
	ReplayHeader rh1, rh2;
	rh1.replay_schema_version = 1;
	rh1.game_data_hash = 0xDEADBEEF;
	rh1.tick_duration = TICK_DURATION;
	rh2 = rh1;
	TESTEQUALS(rh1 == rh2, true);
	rh2.game_data_hash = 0xCAFEBABE;
	TESTEQUALS(rh1 == rh2, false);
}

} // namespace openage::gamestate::sim::tests
```

- [ ] **Step 2: Run test to verify it fails / passes**

Register:
```python
    yield "openage::gamestate::sim::tests::s0_full_gate"
```

Run: `.venv/bin/python -m openage test "openage::gamestate::sim::tests::s0_full_gate" 2>&1 | tail -10`
Expected: PASS (the loop is deterministic; 10k ticks of empty world is fast). If it fails on time, the test is correct but slow; that is acceptable but flag it.

- [ ] **Step 3: Run the ENTIRE new sim test suite together to confirm no regressions**

Run: `.venv/bin/python -m openage test "openage::gamestate::sim::tests::clock_advances" "openage::gamestate::sim::tests::scheduler_rejects_duplicate_id" "openage::gamestate::sim::tests::scheduler_rejects_cycle" "openage::gamestate::sim::tests::scheduler_freezes_and_runs_clean" "openage::gamestate::sim::tests::activity_registry_executes_handler" "openage::gamestate::sim::tests::activity_registry_rejects_duplicate_id" "openage::gamestate::sim::tests::mutation_buffer_stages_and_commits" "openage::gamestate::sim::tests::mutation_buffer_rollback_discards" "openage::gamestate::sim::tests::typed_events_drain_in_sorted_order" "openage::gamestate::sim::tests::event_wait_any_matches" "openage::gamestate::sim::tests::work_queue_must_empty_assertion" "openage::gamestate::sim::tests::work_queue_optional_empty" "openage::gamestate::sim::tests::rng_streams_independent" "openage::gamestate::sim::tests::rng_seed_and_serialize" "openage::gamestate::sim::tests::checksum_empty_world_stable" "openage::gamestate::sim::tests::replay_header_equality" "openage::gamestate::sim::tests::checksum_system_runs" "openage::gamestate::sim::tests::command_ingress_canonical_order" "openage::gamestate::sim::tests::command_input_delay" "openage::gamestate::sim::tests::command_log_append_and_flush" "openage::gamestate::sim::tests::loop_empty_world_10_ticks_stable" "openage::gamestate::sim::tests::loop_advances_tick" "openage::gamestate::sim::tests::loop_batch_invariant" "openage::gamestate::sim::tests::s0_full_gate" 2>&1 | tail -5`
Expected: all PASS, 0 failures.

- [ ] **Step 4: Run the full pre-existing test suite to confirm no regressions elsewhere**

Run: `.venv/bin/python -m openage test -a 2>&1 | tail -10`
Expected: the pre-existing suite still passes (the M0 additions are isolated under `gamestate/sim/` and `testlist.py` additions only append). Any failure here must be a pre-existing one unrelated to M0 - verify by checking the failing test name is not `gamestate::sim`.

- [ ] **Step 5: Commit**

```bash
git add libopenage/gamestate/sim/sim_test.cpp openage/testing/testlist.py
git commit -m "sim: add S0 full-gate acceptance test (10k ticks + replay header)"
```

---

### Task 13: M0 README + gate-mapping doc

**Files:**
- Create: `libopenage/gamestate/sim/README.md`

**Interfaces:**
- Consumes: the spec
- Produces: a short pointer doc mapping each S0 gate (A-L) to the test that proves it, plus the M0 boundary (what is NOT in M0).

- [ ] **Step 1: Write the README**

Create `libopenage/gamestate/sim/README.md`:

````markdown
# gamestate/sim/ - Deterministic Simulation Harness (M0)

Implements migration stage **M0** of
`docs/superpowers/specs/2026-07-22-rts-gameplay-foundation-design.md`:
a standalone, tickable, empty-world deterministic simulation with a stable
per-tick checksum and a replay-manifest header. This is the substrate every
later gameplay system (M1+ Move, M2+ Combat, ...) plugs into.

## What M0 provides

- `DeterministicSimulationLoop` - 14 frozen phases per tick; `current_tick`
  advances to T+1 in Phase 13 EndTick only (spec 1.3).
- `SimulationClock` - `tick_id_t`, 20 Hz, no wall-clock (spec 2.1).
- `PhaseSystemScheduler` - topo-sorted registration; duplicate id / cycle ->
  `freeze()` throws (spec 2.2).
- `ActivityTaskRegistry` - Activity RunSystem handlers; `ActivityTaskResult`
  variant (spec 2.2, 2.3).
- `WorldMutationBuffer` - stage/commit-at-phase field writes (spec 1.5).
- `TypedEventQueue` - 6-tuple strict sort, WaitAny (spec 2.6, 2.7).
- `PhaseWorkQueue<T>` + `WorkQueueRegistry` - transient inter-phase work,
  must-empty assertion (spec 2.10, 4.8).
- `DeterministicRNG` - multi-stream on `rng::RNG`; independent streams
  (spec 2.18).
- `CanonicalStateSerializer` + `ChecksumSystem` + `ReplayHeader` - single
  serializer, per-tick checksum, replay manifest (spec 2.17).
- `CommandIngressQueue` + `CommandLog` - arrival-order-independent canonical
  sort; append-only replay log (spec 2.5).

## S0 gate -> test mapping (spec 4.4.1)

| Gate | Test |
|---|---|
| A empty world stable checksum | `loop_empty_world_10_ticks_stable`, `s0_full_gate` |
| B batch / catch-up invariant | `loop_batch_invariant` |
| C unordered-container independence | (structural: no unordered iteration in M0 hot path; `loop_empty_world_10_ticks_stable` exercises fixed-order iteration) |
| D ingress arrival-order independence | `command_ingress_canonical_order` |
| E event/buffer layout independence | `typed_events_drain_in_sorted_order` |
| F RNG stream isolation | `rng_streams_independent` |
| G duplicate registry id -> freeze fails | `scheduler_rejects_duplicate_id`, `activity_registry_rejects_duplicate_id` |
| H scheduler dependency cycle -> freeze fails | `scheduler_rejects_cycle` |
| I tick 0 / T+1 boundary | `command_input_delay`, `loop_advances_tick` |
| J work-queue must-empty at tick end | `work_queue_must_empty_assertion` |
| K replay schema/hash mismatch refused | `replay_header_equality`, `s0_full_gate` |
| L debug/release fixed-point identical | (structural: M0 uses no floating point; `FixedPoint<int64_t,16>` only via `time/clock.h` speed_t, unused here) |

## What is NOT in M0 (deferred to M1+)

- Any gameplay system: pathfinding, locomotion, combat, damage, death,
  gather, build, production, AI, scenario runtime.
- Any modification to `GameSimulation::run()` (wall-clock loop) - that is
  M1+, gated behind `RuntimeMode`.
- Entity/component state (the world is empty; `WorldSnapshot` is an empty
  struct until M1).
- The `send_command.cpp` / `activity.cpp` switch deletions (M1+).
- Python bindings (`pxdgen`) - M0 is C++-test-only.

## Build & test

```sh
ninja -C bin -j$(nproc)                       # build
.venv/bin/python -m openage test --list      # list
.venv/bin/python -m openage test -a           # run all
.venv/bin/python -m openage test \
  "openage::gamestate::sim::tests::s0_full_gate"   # run the M0 acceptance test
```
````

- [ ] **Step 2: Commit**

```bash
git add libopenage/gamestate/sim/README.md
git commit -m "sim: add M0 README with S0 gate->test mapping"
```

---

## Self-Review

**1. Spec coverage (S0 gates A-L, spec 4.4.1):**
- A (empty-world stable checksum): Task 2 `clock_advances`, Task 11 `loop_empty_world_10_ticks_stable`, Task 12 `s0_full_gate`. ✓
- B (FPS/catch-up identical): Task 11 `loop_batch_invariant` (catch-up framing). The per-FPS variant is structurally guaranteed (SimLoop is tick-driven, wall-clock only decides count) and noted in README gate C/L row as structural. ✓ (covered + documented)
- C (unordered container independence): M0 has no unordered iteration in the hot path (fixed-order vector iteration in the loop). Documented as structural in README. ✓
- D (ingress arrival order): Task 10 `command_ingress_canonical_order`. ✓
- E (event/buffer layout): Task 6 `typed_events_drain_in_sorted_order`. ✓
- F (RNG stream isolation): Task 8 `rng_streams_independent`. ✓
- G (duplicate id -> freeze fails): Task 3 + Task 4. ✓
- H (scheduler cycle -> freeze fails): Task 3 `scheduler_rejects_cycle`. ✓
- I (tick 0 / T+1 boundary): Task 10 `command_input_delay`, Task 11 `loop_advances_tick`. ✓
- J (must-empty at tick end): Task 7 `work_queue_must_empty_assertion`, enforced in loop phase 12. ✓
- K (replay schema/hash mismatch refused): Task 9 `replay_header_equality`, Task 12 `s0_full_gate`. ✓
- L (debug/release fixed-point identical): structural (M0 uses no floating-point; documented in README). ✓

Every spec section referenced by M0 (1.1-1.5, 2.1-2.18, 4.4.1) is covered by a task or documented as structural. No spec section for M0 is unaccounted.

**2. Placeholder scan:** Searched for "TBD", "TODO", "implement later", "add appropriate", "similar to Task". The only `TODO`-style text is in README boundary notes and one inline `// M0: ...` comment marking intentional minimalism with the M1+ expansion noted - these are not plan placeholders (no step says "implement later" without showing the code). All code steps contain complete code. No step references an undefined type: `WorldSnapshot` is defined in Task 9; `SimulationContext` in Task 3; `EventWaitSpec`/`EventMatchSpec`/`CorrelationKey` in Task 4's `event_wait_spec.h` (shared with Task 6); `IssuerStableId`/`IssuerKind`/`PathRequestId`/`AttackId`/`MovementGenerationKey` in Task 1's `types.h`.

**3. Type consistency:** Verified cross-task naming: `SimulationClock::advance()` (Task 2) used by `DeterministicSimulationLoop::phase13_end_tick` (Task 11). `PhaseSystemScheduler::register_system/freeze/run_phase(TickPhase, SimulationContext&)` (Task 3) matches the loop's calls in Task 11. `ActivityTaskRegistry::execute(ActivityTaskId, entity_id_t, const ActivityTaskParams&, ActivityExecutionContext&)` (Task 4) matches the test signature. `WorldMutationBuffer::stage_int/commit/rollback/read_int/pending_count` (Task 5) matches its tests. `TypedEventQueue::enqueue/drain_sorted/matches` (Task 6) matches tests. `PhaseWorkQueue<T>(bool must_empty)` + `WorkQueueRegistry::register_queue/assert_all_empty_at_tick_end` (Task 7) match tests. `DeterministicRNG::seed_all/next/serialize/deserialize` (Task 8) matches tests. `CanonicalStateSerializer::to_bytes/hash`, `ChecksumSystem::compute(const SimulationClock&, const WorldSnapshot&)`, `ReplayHeader::operator==` (Task 9) match tests. `CommandIngressQueue::issue/drain_canonical`, `CommandLog::append/flush/size` (Task 10) match tests. `DeterministicSimulationLoop::run_tick/current_tick/last_checksum` (Task 11) match tests.

**Pre-flight API verification (resolved against the actual codebase):** (a) `rng::RNG` draw = `uint64_t random()`, serialize = `std::string to_string() const` + `RNG(const std::string&)`; Task 8 uses these exact names. (b) `util/hash.h` exposes `class Siphash` (`Siphash(std::array<uint8_t,16> key)` + `uint64_t digest(const uint8_t*, size_t)`); Task 9 uses these exact names with a fixed key. Both are written verbatim, not left as verify-steps. Remaining residual risk: `RNG::to_string()` output containing whitespace would break the `in >> state` token read in Task 8's `deserialize` - the `rng_seed_and_serialize` round-trip test catches this; if it fails, switch to a length-prefixed state string.

**Risk note (honest):** Task 7's `PhaseWorkQueue<T>` inherits `PhaseWorkQueueBase` (defined in `work_queue_registry.h`, created first within the same task). The two headers are co-created in Step 3 in the correct order (registry first, then the inheriting template), so there is no cross-task ordering hazard.
