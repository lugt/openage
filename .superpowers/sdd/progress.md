# M0 Determinism Harness - Progress Ledger

Plan: docs/superpowers/plans/2026-07-22-m0-determinism-harness.md
Spec: docs/superpowers/specs/2026-07-22-rts-gameplay-foundation-design.md
Branch: docs/rts-foundation-spec (at fork lugt/openage)
Base: a95978c4 (plan pre-flight fixes committed)
Build: `ninja -C bin -j$(nproc)` | Test: `.venv/bin/python -m openage test "<name>"`

## Task status
- [pending] Task 1: scaffold sim/ + StrongId + types
- [pending] Task 2: SimulationClock + clock_advances test
- [pending] Task 3: PhaseSystemScheduler (gates G, H) + SimulationContext
- [pending] Task 4: ActivityTaskRegistry + ActivityTaskResult + event_wait_spec.h
- [pending] Task 5: WorldMutationBuffer
- [pending] Task 6: TypedEventQueue (gates E, WaitAny)
- [pending] Task 7: PhaseWorkQueue + WorkQueueRegistry (gate J)
- [pending] Task 8: DeterministicRNG multi-stream (gate F)
- [pending] Task 9: CanonicalStateSerializer + ChecksumSystem + ReplayHeader (gates A, K, L)
- [pending] Task 10: CommandIngressQueue + CommandLog (gates D, I)
- [pending] Task 11: DeterministicSimulationLoop 14-phase (gates A, B, C, J)
- [pending] Task 12: S0 full-suite integration test (gates A-L)
- [pending] Task 13: M0 README + gate-mapping doc

## 2026-07-22 - 开发执行暂停(用户指示)
用户要求:"暂时先写好 spec、执行的方案,先不开始开发"。
- spec 已锁定提交并推送到 fork:9f6dd4d2
- plan 已写好 + pre-flight 修复,本地提交:a95978c4(尚未推送 fork)
- Task 1 implementer 中途被中止;其半成品(libopenage/gamestate/sim/ 脚手架 +
  gamestate/CMakeLists.txt 的 add_subdirectory(sim/))已清理,工作区回到 a95978c4 干净起点
- 下次开发从 a95978c4 重跑 Task 1 起,按 ledger 继续
- ledger/short scratch 在 .superpowers/sdd/(gitignored)
