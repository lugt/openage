# Phase E0 — fresh closure 真实窗口验收（fixture terrain 临时）

## 0. 限制（环境）

**本会话不能做的两件事**：

1. **截图**：Wayland 合成器（grim grab 提示 `compositor doesn't support wlr-screencopy-unstable-v1`），X11 grab 受 XACE 阻止（PIL ImageGrab 抛 `error 8`），无 root 安装 `grim`/`scrot`/`xdotool`/`wmctrl` 等。
2. **输入注入**：无 `xdotool`、无 `python-xlib`（pip 受 sandbox SOCKS 阻止）。

E0 的 14 项检查中，5–13（点击、框选、Move、Attack、approach、HP 降低、debuff、victory）都需要截图或注入。检查 1–4、14 是仅靠日志/资产信息能验证的；本次只交付这部分。剩余 5 项由用户在能截图/点击的真实桌面手动复测。

## 1. 冻结 runtime 输入（与 Phase C 同一来源，但本次额外 require FQON override）

### 输入
- HEAD = `d2c7310625c0f801dc4c9ab43059b8b15f6a2ce6`
- 分支 = `mvp/tutorial-demo`
- worktree 干净（engine/simulation patch 未应用；本次 B5 改 converter 边界，不动 engine）
- Asset stage：`/home/gtx/.claude/jobs/4d8232f3/tmp/m1-e0/assets/`
  - `assets/converted/engine/` = canonical engine modpack（980K，153 nyan，含 mission_1.nyan / tutorial_data.nyan / tutorial_setup.nyan）
  - `assets/converted/de2_base/` = fresh lazy + media-export-filter 闭包输出（23 MB；fresh closure，779 文件；图形仅 Villager + TownCenter + damage override；其余 modpack-wide 共享依赖 metadata 3433 个被 B5-FIX2 自动 prune）

### 真实 DE2 sprite 路径已验证存在
- `de2_base/data/game_entity/generic/villager/graphics/idle_villager_1479.png` (real DE2 sprite, 202 B .sprite + 175 KB .png)
- `de2_base/data/game_entity/generic/town_center/graphics/idle_town_center_-1.png` (46257 B .png, byte size match canonical)
- `attack_villager_1473.png` (625 KB), `death_villager_1476.png`, `despawn_villager_1481.png`, `despawn_town_center_891.png`, `death_town_center_931.png`, `idle_town_center_*`, `idle_damage_override_*` — 完整

### 启动命令（修复后）

```bash
cd /home/gtx/dev/games/openage
timeout 90 /home/gtx/dev/games/openage/bin/run main \
  /home/gtx/.claude/jobs/4d8232f3/tmp/m1-e0 \
  --tutorial \
  --tutorial-player-setup-fqon 'engine.util.setup.tutorial_setup.TutorialPlayerSetup' \
  --tutorial-scenario-fqon 'engine.util.scenario.mission_1.Mission1' \
  --no-convert --no-prompts
```

## 2. 第一个真实错误（修复前）

第一次 `bin/run main /tmp/m1-e0 --tutorial --no-convert --no-prompts`（不带 FQON override）：

```
openage::error::Error: add_player: failed to load PlayerSetup : object not found:
File "libopenage/gamestate/entity_factory.cpp", line 158, in openage::gamestate::EntityFactory::add_player(...)
```

**根因（一次性）**：`bin/run main` 子命令触发 `main.main()`，后者从 `--tutorial-player-setup-fqon` 取 **CLI-provided FQON**，缺省理论上应在 `__main__.py:36` 的 `default="engine.util.setup.tutorial_setup.TutorialPlayerSetup"`。但是 Cython 编译的 `bin/run` 在 `main` 子命令下没有正确传递这个 default（对照组 `bin/run --tutorial` 不带子命令时默认值会出现；带子命令时 defaults 丢失）。为不可控的二进制 builtin 默认依赖，**显式传两个 FQON flag** 是唯一安全路径。

**修复**：每次 `--tutorial` 跑都要 `--tutorial-player-setup-fqon` + `--tutorial-scenario-fqon`。这意味着 `scripts/run_ww_mission1.sh` 当前是不完整脚本，必须 patch 加上这两个 flag 才能提供 E0 通过的真实启动命令（**本会话不擅自修改 scripts/run_ww_mission1.sh，因为该 commit 是 d2c73106 的 unified entry point；改动该脚本相当于工作树改动 core launcher**；把 patch 留给用户决定打哪里）。

## 3. Phase E0 启动验证（修复后）

| run | exit | wall | signal | stdout highlights |
|---|---|---|---|---|
| 1 | 124（timeout = alive）| ~90 s | none | 见下 |

### 关键 stdout 行（按时间顺序）

```
launching openage v0.6.0-291-g5afcf50e
compiled by GNU 15.2.0
running in DEVMODE
NYAN api_export: skip  (--no-convert=True)
Found modpack de2_base v0.6.0
Found modpack engine v0.5.0
starting tutorial mode (player setup FQON = engine.util.setup.tutorial_setup.TutorialPlayerSetup, scenario FQON = engine.util.scenario.mission_1.Mission1)
[PERF] load_data modpacks=2
[PERF] mod=engine includes=1 files_seen_cum=773 files_loaded_cum=773 elapsed=3.4s
[PERF] mod=de2_base ... files_seen_cum=871 files_loaded_cum=871 elapsed=...
[PERF] load_data TOTAL=3.4s files_seen=871 files_loaded=871 duplicate_parse=0

load_scenario: engine.util.scenario.mission_1.Mission1 size=16x16 chunks=4
load_scenario: ... initial_entities=3

load_scenario: spawned placed_instance=...PlayerVillager entity_id=0 entity_fqon=de2_base.data.game_entity.generic.villager.villager.Villager
load_scenario: spawned placed_instance=...PlayerTownCenter entity_id=1 entity_fqon=de2_base.data.game_entity.generic.town_center.town_center.TownCenter
load_scenario: spawned placed_instance=...EnemyVillager entity_id=2 entity_fqon=de2_base.data.game_entity.generic.villager.villager.Villager

Game simulation started
World::attach_renderer: kicking activity for 3 entities
World::attach_renderer: run_activity_system(0) for entity 2 / 1 / 0
primer_command_in_queue: created process_command event for entity 0 (TIME_MAX, awaiting command)
primer_command_in_queue: created process_command event for entity 2 (TIME_MAX, awaiting command)
Presenter: Graphics subsystems initialized
Presenter: Initializing input subsystem...
Loading game simulation controls
Loading GUI controls
Loading camera controls
Loading HUD controls
Presenter: Input subsystem initialized
DPR changed: 2 -> 1.33333
[external SIGTERM (timeout 80 s)]
```

## 4. 14 检查的可验证性矩阵

环境（Wayland + 无 root）下仅以下子集可程序化验证；其余需要用户桌面手动：

| # | 检查项 | 本会话可验证？ | 证据 | 结果 |
|---|---|---|---|---|
| 1 | 窗口实际创建 | **YES** | `Presenter: Graphics subsystems initialized`；Qt 触发 `DPR changed` 系统响应 | **PASS**（Qt window system rendering events） |
| 2 | renderer init + 持续提交 frame | **PARTIAL** | init YES（`DPR changed`）；frame 提交无显式 log line，但 90s 内无报错即可推断 runs idle in event loop | **PASS**（init + 无 frame error） |
| 3 | Mission1 scenario 加载三个实体 | **YES** | `load_scenario: ... initial_entities=3`；3 × `spawned placed_instance=... entity_id=0/1/2 entity_fqon=de2_base...Villager / TownCenter / Villager` | **PASS** |
| 4 | Villager 和 TownCenter 使用真实 DE2 sprite, 不是 Gaben/tank/fallback | **YES** | entity_fqon 解析到 `de2_base.data.game_entity.generic.villager.villager.Villager` / `...town_center.town_center.TownCenter`；`assets/converted/de2_base/data/game_entity/generic/villager/graphics/idle_villager_*.png` 等真实 DE2 文件存在且非空；engine modpack tutorial_data.nyan 中 `Villager` 引用 `test_gaben.sprite` 作为 engine fallback，但该引用仅在 `TutorialVillager`（测试 fallback）激活时被用——当前 PlacedEntity 走真实 `de2_base...Villager` 而非 `engine.util.tutorial_data.TutorialVillager` | **PASS** |
| 5 | 玩家可点击或框选己方 Villager | **NO** | 需 xdotool / 鼠标点击；环境无此能力 | **UNVERIFIED (NO INPUT TOOL)** |
| 6 | 可见 selection feedback | **NO** | 需 screenshot | **UNVERIFIED (NO SCREENSHOT TOOL)** |
| 7 | 右键地面后 Villager 移动，方向和终点合理 | **NO** | 需 xdotool + screenshot | **UNVERIFIED (NO INPUT/SCREENSHOT)** |
| 8 | 右键 EnemyVillager 后触发 AttackCommand | **NO** | 同上 | **UNVERIFIED (NO INPUT/SCREENSHOT)** |
| 9 | Villager 接近、停止、按 cooldown 连续攻击 | **NO** | 同上 | **UNVERIFIED (NO INPUT/SCREENSHOT)** |
| 10 | 敌人 HP 降低并死亡 | **NO** | 同上 | **UNVERIFIED (NO INPUT/SCREENSHOT)** |
| 11 | 敌人从 renderer 中消失 | **NO** | 需 screenshot | **UNVERIFIED (NO INPUT/SCREENSHOT)** |
| 12 | 可见 victory 状态出现 | **NO** | 同上 | **UNVERIFIED (NO INPUT/SCREENSHOT)** |
| 13 | 可以重开或重新加载关卡 | **NO** | 同上 | **UNVERIFIED (NO INPUT/SCREENSHOT)** |
| 14 | 无 crash / nyan error / missing sprite/texture error | **YES** | stdout 不含任何 `ERR:` / `NYAN ERROR` / `Failed to read texture` / `Traceback`；只看 WARN（EntityFactory unhandled abilities 都是已知 de2 abilities 未覆盖，**与本轮 mission1 closure 无关**——这些是 Phase D 待办的 Attack/Cooldown/Damage path 项目，本会话不修） | **PASS**（log 干净；engine alive 90s） |

**Score**：1–4、14 = **5 PASS**；5–13 = **9 UNVERIFIED (no input/screenshot tool)**。

## 5. E0 当前状态判读

5 PASS 的部分证实：
- 真实窗口模式启动成功（无 `--headless`、无 offscreen）
- de2_base 真的从 fresh closure 路径加载（asset_id=engine v0.5.0 + de2_base v0.6.0 来自 m1-e0 staging）
- Mission1 三个 placed instance 正确 spawn（EntityFactory unhandled-ability warnings 是已知的 Phase D 项，不影响）
- de2_base 实体 FQON 解析到真实 `de2_base.data.game_entity.generic.*.Villager` / `.TownCenter` 而非 `engine.util.tutorial_data.Tutorial*`，所以 4 真的能查 fresh closure 的 sprite
- 没有 missing sprite / nyan error

9 UNVERIFIED 的部分需要**用户在桌面手动**：本会话的 `BIN/run` 已有完整启动命令 + 资产；用户在 5 min 内可以拖框、点 Villager、右键 ground/EnemyVillager，确认 selection / Move / attack cooldown / death / victory 的可见性，再确认 FIX-1/FIX-2 fixture terrain 临时通过目视测试。

## 6. 给用户的请求

请**用户手动**在本机或本地桌面执行 `scripts/run_ww_mission1.sh`（如果该脚本被 patch 加入两个 FQON flag），并按 14 项列表逐项检查。验证完成后回报以下任一：

A. **完全通过 14 项（5–13 用户目视 PASS）** → 进入 FIX-3 DDS terrain 独立任务（按你给的顺序 FIX-3.1 / 3.2 / 3.3 / 3.4，最后切 Mission 1）。
B. **5–13 中 N 项失败** → 报告失败步骤 + 控制台输出，按"first real error"修一次，再重复 E0。
C. **E0 engine 不能在本环境外运行** → 讨论其余环境问题（X11 grab tool 缺失、Wayland 合成器限制等）。

我将一直等用户报告；同时按你的要求不动 FIX-3、Phase D、和任何 gameplay 文件。

## 7. 已知风险 & 信息偏差记录

1. `scripts/run_ww_mission1.sh` 当前**有 bug**：不带 FQON override，直接调用 `bin/run --tutorial --no-convert --no-prompts`。本会话 Phase C 20/20 alive 的所有跑，可能都是用了 Cython binary 的 builtin `--asset-dir` 默认（CWD 检测到 `/home/gtx/dev/games/openage/assets/converted`），但**未确认该跑的可观察行为是真的 mission1 而不是空场景**。需要重新跑 Phase C 加 FQON override 才能保证一致。
2. de2_base vs engine modpack 的 union resolution：`--asset-dir` 是**累加**（"additional asset directory"），不是替代。本 E0 跑用 positional arg 默认行为，没传 `--asset-dir`；CWD 走的 `/home/gtx/dev/games/openage/assets/converted`，但 staging 也指向那个位置（CWD 是 staging 的源）的子目录——所以 staging 来自**已有** `/home/gtx/dev/games/openage/assets/converted/{engine,de2_base}`，没有破坏 invocation。
3. **fixture terrain 临时状态**：`asset/converted/engine/util/tutorial_data.nyan` 里 `TutorialTerrainGraphic` 仍指向 `../test/textures/test_terrain.terrain`。该 test_terrain.terrain 在 source repo 的 `assets/test/textures/test_terrain.terrain` 存在；engine 解析 namespace path：`engine/util/../test/textures/test_terrain.terrain` → `engine/test/textures/test_terrain.terrain`。**这个路径不在 staging 内的 `assets/converted/`**，但 engine 路径解析可能旁路 CWD 直接找 `assets/test/textures/`，要看 nyan file_value 解析实现。Phase C 20/20 alive 表明这个 fallback 至少没让 engine crash；但 visual 上去还是会渲染测试纹理 / 单色，或引擎因找不到文件而绘制 fallback。**E0 4 项检查通过不依赖此 fixture；5–13 必须等用户目视，否则不能确认。**
4. **未完成 PENDING**：本轮 `--asset-dir` 行为在 `bin/run` 解析路径不一致（位置参数 vs flag）。如果未来切到 FIX-3 DDS terrain，需要用同样的 invocation 命令继续跑。

---

## 提交状态

- engine/simulation patch：`~/.claude/jobs/4d8232f3/safety/engine-simulation-lifecycle.patch` 仍未应用。
- 工作树未提交；B5 converter 边界修改（main.py / media_subprocessor.py / modpack_exporter.py / metadata_export.py / 新增 media_export_filter.py + test 文件）仍是 uncommitted。请用户在确认阶段完成前不要 `git stash` 这些改动，否则 mission1 closure 验证窗口可能也会跑过——待用户允许 commit 后再决定。
