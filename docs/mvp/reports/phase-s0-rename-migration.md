# S0 迁移表：tutorial → William Wallace Mission 1 / testscene

记录时间：2026-07-27

按用户 spec「先审计再分类再迁移」。下面是完整旧名 → 新名 + 分类 + 处置表。分类标准：

* **#formal** - 正式 William Wallace Mission 1 MVP 内容
* **#generic** - 通用 scenario 启动基础设施（参数、lib 成员、函数）
* **#testscene** - 仅供 renderer/loader smoke 测试使用的 fixture
* **#doc-frozen** - 历史/兼容文档，不改
* **#alias** - 短期兼容 alias，标记 deprecated，新脚本不可用

---

## 1. CLI 参数（openage/main/main.py + openage/__main__.py）

| 旧 | 新 | 分类 | 文件 | 处置 |
|----|----|----|----|----|----|
| `--tutorial` (bool flag) | `--scenario` (bool) | #generic | openage/main/main.py | rename; 旧 flag 保留为 deprecated alias 映射到新值 |
| `--tutorial-player-setup-fqon` | `--player-setup-fqon` | #generic | openage/main/main.py | rename; 旧 flag 保留为 deprecated alias |
| `--tutorial-scenario-fqon` | `--scenario-fqon` | #generic | openage/main/main.py | rename; 旧 flag 保留为 deprecated alias |
| `default="engine.util.setup.tutorial_setup.TutorialPlayerSetup"` on line 36 | `default=ww_mvp....PlayerSetup` | #formal | openage/main/main.py | update default |
| `default="engine.util.scenario.mission_1.Mission1"` on line 47 | `default=ww_mvp....Mission` | #formal | openage/main/main.py | update default |
| `args.tutorial`, `args.tutorial_player_setup_fqon`, `args.tutorial_scenario_fqon` reader/mutator | `args.scenario`, `args.player_setup_fqon`, `args.scenario_fqon` | #generic | openage/main/main.py | rename locals |

## 2. nyan FQON

| 旧 | 新 | 分类 | 文件 | 处置 |
|----|----|----|----|----|----|
| `engine.util.setup.tutorial_setup.TutorialPlayerSetup` | `ww_mvp.campaign.william_wallace.mission_01.PlayerSetup` | #formal | 全部 nyan 引用 + Python 引用 | rename |
| `engine.util.scenario.mission_1.Mission1` | `ww_mvp.campaign.william_wallace.mission_01.Mission` | #formal | 同上 | rename |
| `engine.util.tutorial_data.TutorialGrass` | `engine.test.testscene.TestSceneGrass` | #testscene | nyan 引用 | rename + 标记 `temporary diagnostic terrain fixture - not final William Wallace terrain` |
| `engine.util.tutorial_data.TutorialTerrainGraphic` | `engine.test.testscene.TestSceneTerrainGraphic` | #testscene | nyan 引用 | rename |
| `engine.util.tutorial_data.TutorialVillager` | `engine.test.testscene.BasicMoveVillager` | #testscene | nyan 引用 | rename |
| `engine.util.tutorial_data.TutorialEnemyVillager` | `engine.test.testscene.BasicCombatVillager` | #testscene | nyan 引用 | rename |
| `engine.util.tutorial_data.TutorialIdle` 等其余 `Tutorial*` ability | `engine.test.testscene.*` 镜像集 | #testscene | nyan 引用 | rename |
| `engine.util.tutorial_data.*` (其它) | `engine.test.testscene.*` | #testscene | nyan | rename 全部 |

## 3. nyan 物理文件

| 旧 | 新 | 分类 | 处置 |
|----|----|----|----|----|
| `assets/test/nyan/tutorial/` (directory) | `assets/test/nyan/testscene/` | #testscene | rename 目录 |
| `assets/test/nyan/tutorial/fixtures/tutorial_data.nyan` | `assets/test/nyan/testscene/fixtures/testscene_data.nyan` | #testscene | rename |
| `assets/test/nyan/tutorial/fixtures/setup/tutorial_setup.nyan` | `assets/test/nyan/testscene/fixtures/setup/testscene_setup.nyan` | #testscene | rename（testscene 是 generic 启动 fixture，并非 mission-specific setup） |
| `assets/test/nyan/tutorial/fixtures/scenario/mission_1.nyan` | `assets/test/nyan/william_wallace/mission_01/mission_01.nyan` | #formal | rename 到新目录，分离正式 mission 与 test fixture |
| `assets/test/nyan/tutorial/schemas/{terrain,game_entity,scenario}.nyan` | `assets/test/nyan/testscene/schemas/{terrain,game_entity,scenario}.nyan` | #testscene | rename |
| `assets/test/nyan/tutorial/install.sh` | `assets/test/nyan/testscene/install.sh` | #testscene | rename + 更新 FQON 引用 |
| `assets/test/nyan/tutorial/validate.sh` | `assets/test/nyan/testscene/validate.sh` | #testscene | rename + 更新 mission_1 检测改为更通用 |
| `assets/test/nyan/tutorial/README.md` | `assets/test/nyan/testscene/README.md` | #testscene | 内容更新为 testscene 语义 |

注释：mission_1.nyan 不属于 `testscene/`；它是 William Wallace Mission 1 MVP 的真正内容，迁移到 `assets/test/nyan/william_wallace/mission_01/`。

## 4. C++ 代码（libopenage）

| 旧 | 新 | 分类 | 文件 | 处置 |
|----|----|----|----|----|----|
| `libopenage/gamestate/tutorial/` 目录 | `libopenage/gamestate/objective/` | #generic | - | rename 目录 |
| `libopenage/gamestate/tutorial/trigger.{h,cpp}` | `libopenage/gamestate/objective/objective_controller.{h,cpp}` | #generic | - | rename 文件 |
| `class TutorialTrigger` | `class WilliamWallaceMission01ObjectiveController`（按 spec 优先于通用） | #generic | trigger.h/cpp | rename 类 |
| `tutorial_player_setup_fqon` 参数 | `scenario_player_setup_fqon` | #generic | simulation.h/cpp, engine.h/cpp | rename 字段/参数 |
| `tutorial_scenario_fqon` 参数 | `scenario_scenario_fqon` 或 `scenario_fqon`（按上下文） | #generic | simulation.h/cpp, engine.h/cpp | rename 字段/参数 |
| `bool tutorial` 成员 | `bool scenario_mode` 或 `bool scenario` | #generic | engine.h/cpp | rename 字段；用 alias 同时支持旧名以兼容 |
| `std::shared_ptr<TutorialTrigger> tutorial_trigger` | `std::shared_ptr<WilliamWallaceMission01ObjectiveController> objective_controller` | #generic | simulation.h/cpp | rename 字段 |
| `create_tutorial_trigger()` 方法 | `create_objective_controller()` | #generic | simulation.cpp | rename |
| `make_defeat_enemy_trigger()` factory | `make_defeat_enemy_objective()` | #generic | trigger.h/cpp | rename |
| `create_default_tutorial_trigger()` factory | `create_default_mission_objective()` | #generic | trigger.h/cpp | rename |
| `create_mission_2..5_trigger()` (Mission 2-5 factories) | 不改（per scope: 只重命名 Mission 1） | n/a | trigger.cpp | 留待后续 Mission 2-5 activate 时改名 |
| `# TutorialTrigger` 注释 | `# WilliamWallaceMission01ObjectiveController — ...` | #generic | trigger.h | 更新注释 |
| Tutorial module text "tutorial" in comments | objective / mission wording | #generic | 多处 | 更新 |
| `add_subdirectory(tutorial/)` | `add_subdirectory(objective/)` | #generic | libopenage/gamestate/CMakeLists.txt | 更新 |
| `libopenage/main.cpp:args.tutorial` | `args.scenario_mode` 或 alias | #generic | libopenage/main.cpp | rename + alias |

## 5. libopenage 行为保留承诺

- `GameSimulation::run()` main loop 顺序不变；
- `GameSimulation::start()` 内部时序不变；
- Engine 三线程（time_loop, simulation, presenter）顺序不变；
- Activity graph、Event、Curve API 不动；
- Attack/Death/Entity-removal 架构不重构；
- 仅做 rename + 字段名 + alias 包装，保证新代码外部行为与旧等效。

## 6. Python arguments normalization

| 旧 | 新 | 分类 | 文件 |
|----|----|----|----|----|
| `tutorial_player_setup_fqon = str(getattr(args, "tutorial_player_setup_fqon", "") or "")` | `scenario_player_setup_fqon = str(getattr(args, "scenario_player_setup_fqon", "") or "")` | #generic | openage/main/main.py:265-269 |
| `tutorial = bool(getattr(args, "tutorial", False))` | `scenario_mode = bool(getattr(args, "scenario", False))` | #generic | openage/main/main.py:264 |
| `if getattr(args, "tutorial", False) and not args.modpacks:` 检查 | `if getattr(args, "scenario_mode", False) and not args.modpacks:` | #generic | openage/main/main.py:223 |

## 7. 启动日志（保留可机读字段，去掉 tutorial 字段名）

| 旧 log 字段 | 新 log 字段 | 分类 |
|---|---|---|
| `tutorial mode enabled; player setup FQON = ..., scenario FQON = ...` | `scenario mode enabled; player setup FQON = ..., scenario FQON = ...` | #generic |
| `Tutorial mode: using 'engine' modpack (self-contained tutorial data)` | `Scenario mode: using 'engine' modpack` | #generic |
| `create_tutorial_trigger: no mission suffix matched; ...` | `create_objective_controller: no mission suffix matched; ...` | #generic |

新增（必须出现）：

```
Scenario kind: William Wallace Mission 1 MVP reconstruction
Scenario FQON: <new William Wallace Mission FQON>
Player setup FQON: <new PlayerSetup FQON>
Initial entities: 3
```

## 8. 脚本

| 旧 | 新 | 分类 | 处置 |
|----|----|----|----|----|
| `scripts/run_ww_mission1.sh` | 同文件名，但 PATH 默认值改为 Will Wallace FQON；命令行用 `--scenario`；不允许出现 `tutorial` 字段在 active launch 上（注释可保留以解释迁移） | #formal | update |
| `scripts/verify_ww_mission1.sh` | 同文件名，verify grep 改为新 FQON + `Scenario kind: William Wallace Mission 1 MVP reconstruction` | #formal | update |
| `run-test-20260727.sh` | 不再需要使用 `--tutorial`；改成 `--scenario ...` | #formal | update |

## 9. 文件名 rename

| 旧 | 新 | 分类 |
|----|----|----|
| `plan-tutorial-lvl.md` | `deleted` | #historical transcript，superseded by current `docs/mvp/` documentation |
| `doc/tutorial-lvl-design.md` | 不动；更新文内 `TutorialTrigger` 引用为新名 | #generic-documentation |
| `assets/test/nyan/tutorial/` | `assets/test/nyan/testscene/` | #testscene |

## 10. 兼容 alias

| 旧 | 行为 | 删除窗口 |
|----|---|---|
| `--tutorial` | 解析为 `--scenario`；打印 deprecation warning 到 stderr；映射 truth 不偏离 | 下一版（无 commit 计划，标记 TODO） |
| `--tutorial-player-setup-fqon` | 解析为 `--player-setup-fqon`；同名 deprecation warning | 同上 |
| `--tutorial-scenario-fqon` | 解析为 `--scenario-fqon`；同上 | 同上 |
| `--tutorial + 新的 --scenario-fqon` 冲突 | fail-fast：显式报错 "tutorial flag conflicts with new scenario flags" | 永久 |

## 11. 静态检查（CI test）

新写一个测试，扫描下面字符串在以下路径中出现则测试失败（仅 informational migration fence）：

```
TutorialPlayerSetup
TutorialTrigger
TutorialGrass
tutorial_scenario_fqon
tutorial_player_setup_fqon
engine.util.tutorial_data
```

被扫路径（白名单排除）：
- `assets/test/nyan/tutorial/` （迁移前目录，迁移后是 testscene/，应不在）
- `assets/test/nyan/testscene/` （迁移后允许）
- 历史文档：`Phase*findings*.md`, `Phase*report*.md`, `PhaseB-converter-final-report.md`, `PhaseC-runtime-report.md`, `PhaseE0-report.md`, `PhaseS0-*`, `doc/` （历史性教程文档）, `docs/` （带历史日期的设计）

不被扫路径：
- `scripts/`
- `libopenage/gamestate/objective/` (after rename)
- `assets/test/nyan/testscene/` (after rename)
- `openage/main/main.py` etc — 这些 main launch 流程文件
- 任何包含新命名(`WilliamWallaceMission01ObjectiveController`, `Scenario`, `TestScene`) 而显式 deprecated alias 区不在内

测试写在：`openage/validate_nyan/tests/test_no_tutorial_in_formal_paths.py` 或同等位置。

## 12. 文档保留

下列文档被视为历史记录（**不** 修改）：

- `PhaseA-findings.md`, `PhaseB-converter-final-report.md`, `PhaseC-runtime-report.md`, `PhaseE0-report.md`
- `docs/superpowers/specs/2026-07-22-rts-gameplay-foundation-design.md` （已 frozen per spec）
- `docs/superpowers/specs/2026-07-24-de2-base-fullload-design.md` 等 7/24 系列
- `docs/superpowers/plans/2026-07-*.md` 系列
- `doc/tutorial-lvl-design.md` （设计 spec 也不动实体名，仅注释更新）
- `docs/mvp/mvp-gameplay-spec.md` 与 `mvp-implementation-plan.md`（可能含 `TutorialTrigger` 旧引用 —— 检查后再决定，仅注释性引用可不动）

S0 后的所有 Phase 报告 / 任务清单 / 验证脚本会改为新命名。

## 13. 提交计划

按用户 spec 拆为小提交：

1. `scenario: add generic scenario launch arguments + deprecation aliases`
2. `scenario: rename MVP data to William Wallace Mission 1`
3. `test: move tutorial fixtures to testscene namespace`
4. `script: launch WW Mission 1 with explicit scenario FQON`（含 `run_ww_mission1.sh`, `verify_ww_mission1.sh`）
5. `docs: deleted plan-tutorial-lvl.md (historical transcript, superseded by docs/mvp/)`
6. `ci: add no-Tutorial-in-formal-paths check`

每个都 ≤300-500 行 diff，能 git revert。

## 14. 待办：用户决策点

- `assets/test/nyan/william_wallace/mission_01/` 目录我新建还是用户偏好其它命名？
- TutorialTrigger rename 是否 `WilliamWallaceMission01ObjectiveController`（per spec），或更通用的 `MissionObjectiveController`？
- C++ class migrate 完成后 build 时间约 5–10 min（ninja），是否需要先编译再做事？

S0 报告终。请确认 → 我就开始改。
