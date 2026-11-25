# openage-plus 统一技术规格说明书（Unified Specification）

> **状态**：活跃（Active）— 本文档合并了仓库内所有历史 Spec，作为当前 `openage-plus` 唯一统一的技术规格基线，并对照仓库当前已实现代码与测试结果逐项标注完成状态（`[x]` 已实现并验证 / `[ ]` 未实现 / `[-]` 已废弃或被新架构取代）。
> **项目定位**：`openage-plus = openage + playable function helpers`
> **基准分支**：`master`（已同步上游 `SFTtech/openage` `738bfc6c`，含 C++26、`nyan` 0.3.0 模块化与 Float API）

---

## 0. 历史 Spec 合并谱系

本文档整合以下 9 份历史设计文档（原文件保留在各自目录作历史归档）：

| 原文档 | 主题 | 当前处置 |
| :--- | :--- | :--- |
| [2026-07-20-streaming-convert-design.md](superpowers/specs/2026-07-20-streaming-convert-design.md) | 流式 / 惰性资产转换与内存预算（`< 500 MiB`） | **已落地**（Lazy Backend） |
| [2026-07-22-rts-gameplay-foundation-design.md](superpowers/specs/2026-07-22-rts-gameplay-foundation-design.md) | 独立 `sim/` 14-phase 确定性模拟循环设计 | **已废弃/冻结**（改用上游 EventLoop + Curve） |
| [mvp-gameplay-spec.md](mvp/mvp-gameplay-spec.md) | 单机 William Wallace 1–5 关可玩纵切片规格 | **已整合**（Mission 1–2 已完成且升级为原版 120×120；3–5 待做） |
| [2026-07-24-de2-base-fullload-design.md](superpowers/specs/2026-07-24-de2-base-fullload-design.md) | `de2_base` 全量加载正确性与性能边界 | **部分落地**（`validate-nyan`、`exception_guard` 已落地；二进制 mmap 缓存暂缓） |
| [2026-07-24-de2-base-fullload-binary-format.md](superpowers/specs/2026-07-24-de2-base-fullload-binary-format.md) | `de2_base` 二进制关闭缓存格式规范 | **暂缓**（当前直接通过 `nyan` 0.3.0 加载与按需过滤满足性能） |
| [2026-07-24-de2-base-fullload-test-matrix.md](superpowers/specs/2026-07-24-de2-base-fullload-test-matrix.md) | `de2_base` 加载测试矩阵 | **部分落地**（回归与负向契约测试已落地） |
| [WW_MISSION1_SCENARIO_RUNTIME_SPEC.md](mvp/specs/WW_MISSION1_SCENARIO_RUNTIME_SPEC.md) | William Wallace Mission 1 场景与触发器运行时规格 | **已全面落地并扩展至 Mission 2 原版** |
| [2026-08-01-de2-89-macos-compat-design.md](superpowers/specs/2026-08-01-de2-89-macos-compat-design.md) | DE2 `VER 8.9` 与 macOS Feral 移植版兼容规格 | **已落地** |
| [2026-11-15-mission1-gate-a-automation-design.md](superpowers/specs/2026-11-15-mission1-gate-a-automation-design.md) | Gate A 真机窗口与输入自动化验收规格 | **已落地** |

---

## 1. 架构原则与核心边界

- [x] **复用上游事件驱动内核**：所有游戏状态存储于时间索引曲线（`curve::Discrete` / `curve::Continuous`），通过 `EventLoop` 调度事件，**不另起炉灶**（废除独立 `libopenage/gamestate/sim/` 14-phase 固定帧循环设计）。
- [x] **无头与窗口解耦**：核心模拟（`GameSimulation`、`GameState`、`TriggerRuntime`）不依赖 Qt / OpenGL 上下文，支持纯无头高速推进与回归验证。
- [x] **定点数与确定性**：世界坐标（`coord::phys_t`）、仿真时间（`time::time_t`）统一使用定点数；同时间戳事件严格全序（`time`, `handler_id`, `entity_fqon`, `creation_seq`），消除跨平台与容器遍历抖动。
- [x] **上游兼容性（C++26 & `nyan` 0.3.0）**：支持 C++26 模块（`import nyan;`）及上游 PR #1835 对 `AttributeSetting` / `ResourceAmount` / `EntityContainer` / `Projectile` / 射程字段的 `float` API 变更。
- [-] **独立 `sim/` 目录与 Lockstep 多人**：明确冻结（见 [mvp-gameplay-spec.md](mvp/mvp-gameplay-spec.md) §2），单机体验稳定前不引入。

---

## 2. 资产转换器与多版本兼容规格（Converter & Validation）

### 2.1 流式 / 惰性转换与内存控制
- [x] **Lazy ValueMember 后端**：支持 `OPENAGE_CONVERTER_BACKEND=lazy` 惰性解析 `.dat`，避免全量实例化 ~1.85M `ValueMember` 导致内存暴涨。
- [x] **内存采样与预算闸门**：实现 `openage/convert/tool/budget_gate.py` 与 `memprobe.py`，支持 `--memory-budget-test` 监控转换峰值 RSS（目标 `< 500 MiB`）。
- [x] **按需媒体导出过滤**：实现 `--media-export-filter` 与 `openage/convert/processor/conversion/de2/media_filter.py`，支持仅导出指定模组/战役所需 `.sld` 图形与 `.terrain`，大幅缩短首次转换时间。

### 2.2 DE2 多版本（8.4 / 8.9）与跨平台兼容
- [x] **版式 Profile 抽象**：在 `openage/convert/value_object/read/media/datfile/de2_profile.py` 定义 `DE2_84_PROFILE` 与 `DE2_89_PROFILE`，兼容不同版本的 `empires2_x2_p1.dat` 字段偏移（含 `ProjectileUnit`、`LivingUnit`、`Tech` 等结构差异）。
- [x] **跨平台路径发现**：`cfg/converter/games/game_editions.toml` 与 `cfg/converter/games/de2/version_hashes.toml` 同时覆盖 Linux（Steam/Proton）、Windows（Steam）与 macOS（`AgeOfEmpires2Data` 子目录）。
- [x] **全文明科技树导出**：`scripts/export_tech_trees.py` 流式导出全文明单位、建筑、科技前置依赖 JSON 与 HTML 可视化预览（`scripts/tech_tree/preview.html`）。

### 2.3 独立校验与异常守护
- [x] **`validate-nyan` CLI 工具**：`openage/validate_nyan/main.py` 独立执行 `nyan` 语法、导入闭包与跨文件引用校验。
- [x] **CLI 异常守护**：`openage/exception_guard/main.py` 捕获未处理异常并输出结构化诊断，防止静默崩溃。
- [ ] **二进制 `de2_base` 闭包 mmap 缓存**：暂未启用（当前依赖 `nyan` 0.3.0 原生加载与按需过滤，后续若全量加载瓶颈凸显再按 `2026-07-24-de2-base-fullload-binary-format.md` 实施）。

---

## 3. 场景编译、战役解包与原生加载规格（Scenario Pipeline）

### 3.1 Scenario IR 与 Mission Pack 编译器（Python）
- [x] **标准中间表示 `ScenarioIR`**：`openage/scenario/ir.py` 定义地图（尺寸、地形 ID、高程）、玩家初始资源、外交关系、实体摆放、区域定义、触发器图与原版场景元数据。
- [x] **完整校验器**：`openage/scenario/validation.py` 校验 `nyan` FQON 存在性、地图边界、玩家归属及触发器引用完整性。
- [x] **确定性编译器**：`openage/scenario/writer.py` 将 `ScenarioIR` 编译为 C++ 原生读取的 Mission Pack（`scenario.json`、`object_table.csv`、`trigger_graph.json`、`coverage.json`）。

### 3.2 原版战役包解包与还原
- [x] **`.aoe2campaign` 解包**：`scripts/export_wallace_scenarios.py` 支持识别并剥离 Steam 版 `cam1.aoe2campaign` 的 `GPV` 封装头，提取 `1_Wallace_1.aoe2scenario`、`1_Wallace_2.aoe2scenario` 等原版关卡。
- [x] **原版场景转换器**：`assets/nyan/ww_mvp/original_scene.py` 将原版 120×120 地形、高程、外交、禁用科技/单位、5,698 / 10,130 个实体与 31 / 27 个触发器映射为 `de2_base` 实体与 Mission Pack。
- [x] **缺失精灵即时补齐**：自动从原版 `.sld` 提取 `de2_base` 中缺失的树木、悬崖、岸边鱼等精灵纹理并生成 `.sprite` / `.texture`。

### 3.3 C++ 原生加载器（`libopenage/scenario/`）
- [x] **`MissionPackReader` & `MissionPackLoader`**：`libopenage/scenario/mission_pack.cpp` 与 `mission_pack_loader.cpp` 原生挂载地形网格、高程、玩家资源、外交姿态、实体生成与禁用规则。

---

## 4. 游戏核心玩法系统规格（Gameplay Systems）

### 4.1 碰撞、编队与战斗接近（`libopenage/pathfinding/` & `libopenage/scenario/`）
- [x] **静态与动态空间占用**：`StaticOccupancyGrid` 与 `DynamicOccupancy` 维护建筑与可移动单位的物理占地；自动过滤无碰撞装饰物（草地贴花、路标旗帜、隐藏触发单位）。
- [x] **连续碰撞检测与预约消歧**：`swept_aabb.h`、`movement_reservation.cpp` 与 `placement_validator.cpp` 防止移动穿模与多单位终点死锁。
- [x] **编队行军**：`FormationPlanner`（`formation.cpp`）与 `GroupMoveHandler` 计算多单位散开阵型目标点。
- [x] **攻击接近与高地距离**：`AttackApproach`（`attack_approach.cpp`）搜索目标周边可达攻击站位，采用 2D 水平距离（`std::hypot(dne, dse)`）判定高地/斜坡攻击射程。

### 4.2 战斗、护甲伤害与死亡生命周期（`libopenage/gamestate/`）
- [x] **统一命令入口**：`GameplayCommand` / `send_command.cpp` 支持 `MOVE`、`ATTACK`、`GATHER`、`BUILD`、`TRAIN`、`RESEARCH`、`STOP`、`DIPLOMACY` 等指令。
- [x] **离散伤害与护甲相克**：`apply_discrete_effect.cpp` 与 `attack.cpp` 读取 `nyan` 中的 `Attack` / `Armor` 分类，按原版公式结算伤害。
- [x] **事件驱动死亡与延迟移除**：`death.cpp` 与 `removal.cpp` 在 HP ≤ 0 时安全派发死亡状态并延迟清理实体、释放占用网格、触发触发器条件，杜绝迭代器失效。

### 4.3 经济、建造、训练、科技与视野
- [x] **资源采集与投放**：`economy_action.cpp` 与 `gather.cpp` 支持采集树木（木）、浆果丛（食）、金矿（金）、石矿（石）、狩猎与岸边捕鱼（`shore_fishing.cpp`），并自动寻找最近存放点卸货。
- [x] **建筑地基与建造进度**：`construction.cpp` 与 `build.cpp` 支持放置地基、多名村民协同施工、建造进度累计及竣工切换为可用建筑。
- [x] **单位训练与人口上限**：`train.cpp`、城镇中心/兵营生产队列支持训练村民与军事单位，受房屋/TC 提供的人口上限约束，并遵循关卡禁用清单（`disabled_units` / `disabled_buildings`）。
- [x] **科技研发与时代升级**：支持城镇中心研发织布机、升级封建时代及原版触发器强制研发科技（如重装步兵升级）。
- [x] **三态战争迷雾与外交**：支持未探索（全黑）、已探索（半透明灰暗）、当前可见（全亮）三态迷雾、盟友视野共享与动态外交切换。

---

## 5. 触发器运行时与 William Wallace 战役规格（Campaigns）

### 5.1 通用与原版触发器运行时（`libopenage/scenario/trigger/`）
- [x] **事件驱动触发器状态机**：`runtime.cpp` 与 `instance.cpp` 支持条件订阅、循环触发（`looping`）、启停控制（`enabled`）与有序效果执行。
- [x] **原版 `.aoe2scenario` 触发器执行器**：`original_scenario.cpp` 支持原版核心条件（定时、区域接近/数量、存活/被毁、科技与属性阈值）与核心效果（教程提示/字幕、启停触发器、动态刷兵、击杀/移除、外交变更、强制研发、镜头聚焦、胜利/失败宣告、Gaia 村庄自动接管）。

### 5.2 William Wallace 战役关卡状态
- [x] **Mission 1 — Marching and Fighting（原版完整还原）**：120×120 原版地形与高程、5,698 个对象、31 个触发器；包含路标行军、途中增援、接近村庄接管 Gaia 建筑、强制升级重装步兵、英军反击与摧毁前哨胜利，且按原版禁用 TC 训练村民。
- [x] **Mission 2 — Feeding the Army（原版完整还原）**：120×120 原版地形与河流浅滩、10,130 个对象、27 个触发器；包含采果/伐木/采金教学、触发器刷出第 2–4 名村民、TC 训练新村民、资源达标与延时胜利链。
- [ ] **Mission 3 — Training the Troops（原版导入与可玩闭环）**：导入 `1_Wallace_3.aoe2scenario`，验证民兵/兵营训练与敌军前哨战斗。
- [ ] **Mission 4 — Research and Technology（原版导入与可玩闭环）**：导入 `1_Wallace_4.aoe2scenario`，验证黑暗->封建时代升级、铁匠铺/攻防科技与两波敌军进攻。
- [ ] **Mission 5 — The Battle of Stirling（原版导入与可玩闭环）**：导入 `1_Wallace_5.aoe2scenario`，验证完整经济 + 军事 + 攻城摧毁敌方城镇中心。

---

## 6. 表现层、交互输入与战役 HUD 规格（Presenter & UI）

- [x] **鼠标拾取与右键上下文语义**：`input_manager.cpp` 与 `semantic_action.cpp` 实现左键点选/框选，右键智能分发移动、攻击、采集、建造指令。
- [x] **选中高亮与容错**：`selection_model.cpp`、`selection_overlay.cpp` 与 `hud_selection_overlay.*.glsl` 绘制选中单位脚印高亮，容忍无特定可选组件的实体（修复 `p4.4d2` 崩溃）。
- [x] **战役 HUD（`assets/qml/MissionHud.qml` & `hud_qml_adapter.cpp`）**：
  - 顶部栏：食物/木材/黄金/石头库存与人口计数。
  - 右上角：战役目标面板、原版触发器字幕/教学提示（支持中英文）。
  - 左下角：选中实体详情（HP、攻击、护甲、携带资源）。
  - 右下角：训练、研发、建造、外交操作面板。
  - 小地图（`Minimap.qml`）与初始镜头自动聚焦玩家核心单位。
- [x] **着色器表现**：`world2d.frag.glsl` 与 `terrain.frag.glsl` 集成三态战争迷雾、实体血条、建造进度条及建筑受损三阶段火焰特效（`outpost_fire.cpp`）；`parse_sprite.cpp` 过滤空角度防止崩溃。

---

## 7. 跨平台发布与自动化验证规格（Release & QA）

### 7.1 自动化测试与验收矩阵
- [x] **核心 C++ & Python 单元测试**：`./bin/run test -a`（35/35 通过，含 `nyan` 类型检查、`hp_float_patches`、事件触发等）。
- [x] **Scenario & Tech Tree Pytest 套件**：`pytest openage/scenario/tests scripts/tests`（129/129 通过）。
- [x] **P4.4D 核心玩法回归矩阵**：`scripts/p4_4d_regression.sh --regression`（35 个无头 Demo 全通过）。
- [x] **原版 Mission 1 & 2 全流程无头回归**：`./bin/run test -d gamestate.tests.simulation_demo 53`（均达到 `ORIGINAL_WALLACE_PASS`）。
- [x] **Gate A 真机窗口自动化**：`scripts/capture_macos_window.py` 等窗口截屏与真机输入验证通过。

### 7.2 跨平台发行包（Linux / Windows / macOS）
- [x] **基础桌面启动器原型**：`scripts/campaign_launcher.py` 与 `scripts/run_campaign.sh` 支持自动探测本地 AoE2:DE 并启动关卡。
- [ ] **Visual Converter Wizard GUI**：首次运行引导式图形向导（自动探测 Steam/Proton/macOS 路径，提供 30–60 秒“快速战役模式”与后台增量“完整转换模式”）。
- [ ] **完整三位一体发行打包**：macOS（`.app` / `.dmg` + `macdeployqt`）、Windows（`win64.zip` / `.exe` + Embedded Python + `windeployqt`）、Linux（`AppImage` / Flatpak + `linuxdeploy`）。
