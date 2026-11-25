# openage-plus 统一开发与验收计划（Unified Implementation Plan）

> **状态**：活跃（Active）— 本文档合并了仓库内所有历史 Plan（含早期的 `P0–P7`、`P4.4D`、`Sprint A/B`、`Gate A` 及 `mvp-implementation-plan` 等），按演进阶段统一梳理并对照代码现状逐项打钩：
> - `[x]`：已完成并通过验证
> - `[ ]`：尚未完成（后续待办）
> - `[-]`：已废弃或被新方案取代
>
> 配套统一规格文档：[docs/SPEC.md](SPEC.md)

---

## 0. 历史 Plan 合并谱系

| 原文档 | 阶段/主题 | 当前状态说明 |
| :--- | :--- | :--- |
| [2026-07-20-streaming-convert.md](superpowers/plans/2026-07-20-streaming-convert.md) | 流式转换 Phase 1（`peek.py` 等） | 被 `lazy-convert` 方案吸收完成 |
| [2026-07-21-lazy-convert.md](superpowers/plans/2026-07-21-lazy-convert.md) | 惰性 `ValueMember` 转换与 RSS 预算闸门 | **全部完成** |
| [2026-07-22-m0-determinism-harness.md](superpowers/plans/2026-07-22-m0-determinism-harness.md) | 独立 `gamestate/sim/` 14-phase 确定性循环 | **已废弃**（改用上游 EventLoop + Curve） |
| [mvp-implementation-plan.md](mvp/mvp-implementation-plan.md) | William Wallace 1–5 关可玩纵切片计划 | **Mission 1–2 完成并升级为原版**；Mission 3–5 延续至 Phase G；GUI/打包延续至 Phase H |
| [2026-07-24-de2-base-fullload.md](superpowers/plans/2026-07-24-de2-base-fullload.md) | `de2_base` 全量加载与二进制 mmap 缓存 | Stage 0 工具链已完成；二进制缓存暂缓 |
| [2026-07-24-de2-base-fullload-sprint.md](superpowers/plans/2026-07-24-de2-base-fullload-sprint.md) | `de2_base` 正确性与加载优化冲刺 | **核心工具已完成** |
| [2026-07-24-william-wallace-mvp-handoff.md](superpowers/plans/2026-07-24-william-wallace-mvp-handoff.md) | 早期 MVP Sprint A 交接记录（Converter 阻塞） | **阻塞已解除并归档** |
| [2026-07-24-william-wallace-mvp-progress.md](superpowers/plans/2026-07-24-william-wallace-mvp-progress.md) | Sprint A & Sprint B 进度账本 | **已全面超越完成** |
| [2026-07-25-mission1-attack-command.md](superpowers/plans/2026-07-25-mission1-attack-command.md) | `AttackCommand` 7 阶段垂直切片 | **全部完成** |
| [WW_MISSION1_DEVELOPMENT_PLAN.md](mvp/specs/WW_MISSION1_DEVELOPMENT_PLAN.md) | `P0–P7`（含 `P4.4B` / `P4.4D`）完整开发验收计划 | **P0–P7 全部完成** |
| [2026-11-16-mission1-gate-a-automation.md](superpowers/plans/2026-11-16-mission1-gate-a-automation.md) | Gate A 真机窗口与输入自动化验收计划 | **全部完成** |
| [headless-simulation-roadmap.md](future/headless-simulation-roadmap.md) | 无头模拟 H0–H3 路线图 | **H0 已完成**；H1–H3 归入长期演进 Phase I |

---

## Phase A — 资产转换流式化、DE2 8.9 兼容与校验工具链

- [x] **A1. Lazy ValueMember 转换后端**：实现 `OPENAGE_CONVERTER_BACKEND=lazy` 惰性读取 `.dat`，将转换峰值 RSS 压入 `< 500 MiB` 预算（`budget_gate.py`、`memprobe.py`）。
- [x] **A2. DE2 多版本 `datfile` 布局兼容**：新增 `de2_profile.py`（`DE2_84_PROFILE` / `DE2_89_PROFILE`）并在 `game_editions.toml` / `version_hashes.toml` 中兼容 Linux / Windows / macOS（`AgeOfEmpires2Data`）。
- [x] **A3. 按需媒体导出过滤**：实现 `media_filter.py` 与 `--media-export-filter`，支持只导出战役闭环所需图形与地形。
- [x] **A4. `validate-nyan` 与 `exception_guard`**：上线独立 `nyan` 静态校验子命令及 CLI 异常保护。
- [x] **A5. 全文明科技树导出**：实现 `scripts/export_tech_trees.py` 与可视化预览 `scripts/tech_tree/preview.html`。
- [ ] **A6. （可选/暂缓）`de2_base` 二进制关闭 mmap 缓存**：仅在全量加载所有扩展文明成为耗时瓶颈时再行启用。

---

## Phase B — 场景 IR、战役解包与 Mission Pack 原生加载（P0–P2）

- [x] **B1 (P0). 基线核查与无头驱动**：验证构建、转换与无头运行路径（[baseline-reality-audit.md](mvp/reports/baseline-reality-audit.md)）。
- [x] **B2 (P1). 原版战役包解包与结构化导出**：实现 `scripts/export_wallace_scenarios.py`，支持剥离 `cam1.aoe2campaign` 的 `GPV` 头部，提取 `.aoe2scenario` 并导出 JSON（[mission1-scenario-feasibility.md](mvp/reports/mission1-scenario-feasibility.md)）。
- [x] **B3 (P2.1–P2.3). Scenario IR、校验器与 Mission Pack 编译器**：完成 `openage/scenario/{ir,validation,writer,loader,terrain}.py` 及 `pytest` 覆盖（129 项测试全通过）。
- [x] **B4 (P2.7). C++ 原生加载器**：实现 `libopenage/scenario/{mission_pack,mission_pack_loader}.cpp`，将地形网格、高程、玩家资源、外交与实体直接注入 `GameState`。

---

## Phase C — 核心战斗、事件触发器与空间寻路（P3–P4 & P4.4D）

*(注：旧计划中的 `P4.4D` 编号即对应本阶段的交互放置、碰撞消歧与回归验证，现已全部合并并验收完成)*

- [x] **C1 (P3.1–P3.4). 事件驱动战斗与生命周期闭环**：
  - [x] 扩展 `GameplayCommand`（`ATTACK`、`MOVE`、`GATHER`、`BUILD`、`TRAIN`、`RESEARCH` 等）与右键上下文分发。
  - [x] 实现离散伤害与护甲相克计算（`apply_discrete_effect.cpp`、`attack.cpp`）。
  - [x] 实现事件驱动死亡（`death.cpp`）与延迟实体移除（`removal.cpp`），杜绝迭代器失效。
- [x] **C2 (P4.1). 事件驱动触发器与确定性调度**：
  - [x] 修复 `Event::operator<` 同时间戳严格全序，消除跨平台调度抖动（[p4.1.1-cross-platform-runtime-stabilization.md](mvp/reports/p4.1.1-cross-platform-runtime-stabilization.md)）。
  - [x] 实现 `TriggerRuntime` / `TriggerInstance` 状态机，淘汰轮询式胜利判定（[p4-trigger-runtime.md](mvp/reports/p4-trigger-runtime.md)、[p4.1-event-driven-objective.md](mvp/reports/p4.1-event-driven-objective.md)）。
- [x] **C3 (P4.4B–P4.4D). RTS 空间碰撞、编队与攻击接近**：
  - [x] `StaticOccupancyGrid` 与 `DynamicOccupancy`（过滤无碰撞装饰物，解决路标/草地贴花阻塞道路）。
  - [x] `swept_aabb`、`MovementReservation` 与 `PlacementValidator`（[p4.4b-footprint-placement.md](mvp/reports/p4.4b-footprint-placement.md)）。
  - [x] `FormationPlanner` 多单位编队移动与 `AttackApproach` 高地 2D 水平距离攻击接近。
  - [x] 修复选中无特定可选组件实体时的崩溃（[p4.4d2-selection-optional-component-crash.md](mvp/reports/p4.4d2-selection-optional-component-crash.md)）。
  - [x] 固化 35 项无头游戏逻辑回归脚本 `scripts/p4_4d_regression.sh --regression`（`PASS=35 FAIL=0`）。

---

## Phase D — 真实媒体闭环、渲染着色器与 Gate A 窗口自动化（P5–P7）

- [x] **D1 (P5). 真实 DE2 媒体与地形闭合**：支持真实 DE2 草地/道路/浅滩/水体纹理、`outpost_fire` 三阶段火焰精灵与缺失 `.sld` 树木/悬崖自动补齐。
- [x] **D2 (P6). 独立资产根目录与无头全流程验收**：`./bin/run test -a`（35/35）与 `demo 53` 无头通关验收通过。
- [x] **D3 (P7 / Gate A). 真机窗口、HUD 与自动化输入验收**：
  - [x] `parse_sprite.cpp` 过滤空角度防止窗口崩溃。
  - [x] 选中高亮（`selection_model.cpp`、`selection_overlay.cpp`）、三态战争迷雾与血条/建造进度条着色器（`world2d.frag.glsl`、`terrain.frag.glsl`）。
  - [x] 战役 HUD（`assets/qml/MissionHud.qml`、`Minimap.qml`、`hud_qml_adapter.cpp`）。
  - [x] 跨平台真机窗口截图与自动化验证（`scripts/capture_macos_window.py`）。

---

## Phase E — 经济、建造、训练、迷雾与原版 Mission 1 & 2 完整复原

- [x] **E1. 完整经济与建造系统**：实现村民采集（伐木、浆果、采金、采石、狩猎、岸边捕鱼）、资源投放、建筑地基放置与协同建造进度、城镇中心/兵营训练队列与人口上限、科技研发。
- [x] **E2. 原版触发器完整执行器**：实现 `libopenage/scenario/trigger/original_scenario.cpp`，覆盖定时、区域接近/数量、存活/被毁、科技阈值条件，以及教学提示、刷兵增援、Gaia 村庄自动接管（`capture_gaia`）、强制研发科技、外交变更与胜利链。
- [x] **E3. 原版 Mission 1（`1_Wallace_1.aoe2scenario`）完整还原**：120×120 原版地形、5,698 个实体、31 个触发器、禁用 TC 造村民限制，`demo 53` 完整通关（`ORIGINAL_WALLACE_PASS mission=1 time=1081.00000`）。
- [x] **E4. 原版 Mission 2（`1_Wallace_2.aoe2scenario`）完整还原**：120×120 原版地形、10,130 个实体、27 个触发器、开放造村民并保留原版禁用限制，`demo 53` 完整通关（`ORIGINAL_WALLACE_PASS mission=2 time=345.00000`）。
- [x] **E5. 桌面启动器原型**：上线 `scripts/campaign_launcher.py` 与 `scripts/run_campaign.sh`。

---

## Phase F — 上游同步（C++26 / `nyan` 0.3.0）与双语文档整理

- [x] **F1. 合并上游 `SFTtech/openage` (`738bfc6c`)**：
  - [x] 升级至 C++26 模块构建与 `nyan` 0.3.0（`import nyan;`），兼容 Homebrew LLVM Clang 22。
  - [x] 适配上游 PR #1835 的 `float` API 变更（`AttributeSetting`、`ResourceAmount`、`EntityContainer`、`Projectile`、射程字段）。
  - [x] 完成全量单元测试（35/35）、Pytest（129/129）、P4.4D 回归（35/35）及原版 Mission 1 & 2 验证，并推送至 `fork/master`。
- [x] **F2. 中英文双语项目文档**：完成 [README.md](../README.md)（英文默认）、[README.zh-CN.md](../README.zh-CN.md)、[README-PLUS.md](../README-PLUS.md)、[README-PLUS.zh-CN.md](../README-PLUS.zh-CN.md) 与 [README.upstream.md](../README.upstream.md)。
- [x] **F3. 统一 Spec 与 Plan 归口**：生成统一 [docs/SPEC.md](SPEC.md) 与本文档 [docs/PLAN.md](PLAN.md)。

---

## Phase G — William Wallace 战役后续关卡扩展（Mission 3–5）

- [ ] **G1. Mission 3 — Training the Troops（原版导入与验证）**：
  - [ ] 使用 `export_wallace_scenarios.py` 与 `original_scene.py` 导出 `1_Wallace_3.aoe2scenario`。
  - [ ] 验证兵营训练民兵、房屋人口上限与原版触发器（训练达标 + 击败英军前哨）。
  - [ ] 纳入 `demo 53` / 无头回归测试。
- [ ] **G2. Mission 4 — Research and Technology（原版导入与验证）**：
  - [ ] 导出 `1_Wallace_4.aoe2scenario`，接入黑暗时代 -> 封建时代升级与铁匠铺/攻防科技。
  - [ ] 验证脚本化多波次敌军定时进攻与民兵自动升级（剑士系）。
  - [ ] 纳入 `demo 53` / 无头回归测试。
- [ ] **G3. Mission 5 — The Battle of Stirling（原版导入与验证）**：
  - [ ] 导出 `1_Wallace_5.aoe2scenario`，打通完整经济 -> 军事 -> 攻城摧毁敌方城镇中心胜利闭环。
  - [ ] 完成 1–5 关连续战役切换与真机试玩截图验证。

---

## Phase H — 跨平台发布与可视化工具链（Linux / Windows / macOS）

- [ ] **H1. Visual Converter Wizard GUI（首次运行资产转换向导）**：
  - [ ] 自动探测 Windows（Steam 注册表 / 盘符）、macOS（`~/Library/Application Support/Steam`）与 Linux（Steam / Flatpak）的 AoE2:DE 安装目录。
  - [ ] 提供“快速战役模式（30–60 秒，利用 `media_filter.py` 仅导出战役所需资源）”与“后台增量完整转换模式”。
  - [ ] 展示实时进度条、耗时预估与日志诊断导出，产物统一写入平台标准用户数据目录并校验 `manifest.toml`。
- [ ] **H2. Campaign Launcher GUI 增强**：
  - [ ] 扩展 `scripts/campaign_launcher.py` 为完整 Qt6 启动器，集成资产状态指示灯、关卡图文卡片（支持原版 120×120 与 16×16 教学版切换）、分辨率与中/英文切换。
- [ ] **H3. 三大平台独立安装包流水线**：
  - [ ] **macOS**：`openage-plus.app` + `.dmg`（`macdeployqt` + 内嵌 Python + `nyan`）。
  - [ ] **Windows**：`openage-plus-win64.zip` + 安装包 `.exe`（MSVC/vcpkg + `windeployqt` + Windows Embedded Python）。
  - [ ] **Linux**：`openage-plus-x86_64.AppImage` / Flatpak（`linuxdeploy` + `linuxdeploy-plugin-qt`）。

---

## Phase I — 长期架构演进与上游回馈

- [ ] **I1. 无头回放与状态快照（`headless-simulation-roadmap.md` H1–H2）**：
  - [ ] 封装 `issue_*_command` C++ 测试 API 与 JSON 指令回放脚本。
  - [ ] 实现 `EventLoop` 观察者与 BLAKE2b 增量状态快照比对。
- [ ] **I2. 通用 AI 与独立 PR 拆解**：
  - [ ] 从 `openage-plus` 拆分独立、纯净的 PR 回馈上游 `SFTtech/openage`（如 DE2 8.9 兼容、可选组件选中容错、`.sprite` 空角度修复、场景导入工具等）。
  - [ ] 评估通用 AI（采集/建造/出兵/进攻）与确定性多人联机（H3）。

---

## 附录：当前标准验证命令速查

```sh
# 1. C++ & Python 全量单元测试 (35/35)
./bin/run test -a

# 2. Scenario & Tech Tree Pytest (129/129)
./venv/bin/pytest openage/scenario/tests scripts/tests

# 3. P4.4D 核心玩法无头回归 (35/35)
bash scripts/p4_4d_regression.sh --regression

# 4. 原版 William Wallace Mission 1 / 2 全流程无头验证
bash assets/nyan/ww_mvp/install.sh --original-scenario logs/wallace-original/mission-1.json "$HOME/Library/Application Support/Steam/steamapps/common/AoE2DE/AgeOfEmpires2Data"
./bin/run test -d gamestate.tests.simulation_demo 53

# 5. 启动桌面战役启动器
bash scripts/run_campaign.sh
```
