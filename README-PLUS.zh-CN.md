# openage-plus 架构与完整修改说明（对比上游 `SFTtech/openage`）

> **项目定义**：**`openage-plus = openage + playable function helpers`**
> **语言 (Language)**: [English (README-PLUS.md)](README-PLUS.md) | **简体中文**
> `openage-plus` 是在上游 [SFTtech/openage](https://github.com/SFTtech/openage) 基础上，补齐**完整游戏可玩性（Playable Gameplay Helpers）**的分支版本。
>
> - 项目主入口文档：[README.zh-CN.md](README.zh-CN.md) | [README.md (English)](README.md)
> - 上游原版文档归档：[README.upstream.md](README.upstream.md)

---

## 目录

1. [为什么需要 `openage-plus`？](#1-为什么需要-openage-plus)
2. [整体架构对比图](#2-整体架构对比图)
3. [新增模块完整清单（New Modules）](#3-新增模块完整清单new-modules)
4. [核心引擎逻辑修改深度解析（Modified Core Logic）](#4-核心引擎逻辑修改深度解析modified-core-logic)
5. [战役与关卡还原度详解（William Wallace Mission 1 & Mission 2）](#5-战役与关卡还原度详解william-wallace-mission-1--mission-2)
6. [跨平台（Linux / Windows / macOS）发布与打包路线图](#6-跨平台linux--windows--macos发布与打包路线图)
7. [自动化测试与回归验证矩阵](#7-自动化测试与回归验证矩阵)

---

## 1. 为什么需要 `openage-plus`？

上游 `SFTtech/openage` 自 `v0.5.0` 起开启了底层 C++20 事件驱动架构（Event-Driven ECS + `nyan` 数据语言 + 新版 OpenGL/ Vulkan 渲染管线）的全面重写。然而，上游长期停留在**基础设施与渲染演示阶段**：
- 官方 README 明确注明：*"At the moment, 'gameplay' is basically non-functional."*
- 缺少完整的场景加载器（`.aoe2scenario` / `.aoe2campaign` -> `nyan` Mission Pack -> C++ `GameState`）。
- 缺少触发器运行时（Trigger Runtime），无法运行任何原版战役逻辑。
- 缺少碰撞体积（Static & Dynamic Occupancy）、多单位编队寻路、近战攻击接近（Attack Approach）、护甲伤害计算与死亡移除生命周期。
- 缺少村民资源采集、建筑施工（地基 -> 建造进度 -> 竣工）、城镇中心训练/科技研发及关卡禁用限制。
- 缺少战争迷雾（Fog of War）、小地图、框选/点选高亮、战役目标面板与命令面板 HUD。

**`openage-plus` 的目标**就是在不破坏上游 `nyan` 与事件驱动内核的前提下，提供一整套 **`playable function helpers`**，把引擎从“技术 Demo”推进到“**可直接游玩原版《帝国时代 II：决定版》战役**”的状态。

---

## 2. 整体架构对比图

```
┌───────────────────────────────────────────────────────────────────────────────────┐
│                              openage-plus 完整可玩栈                               │
├───────────────────────────────────────────────────────────────────────────────────┤
│ [启动与工具层]                                                                     │
│  • scripts/campaign_launcher.py (桌面战役启动器)                                    │
│  • scripts/export_wallace_scenarios.py (GPV 战役包解包 & .aoe2scenario 提取)        │
│  • scripts/export_tech_trees.py (全文明科技树与图形目录流式导出)                      │
├───────────────────────────────────────────────────────────────────────────────────┤
│ [资产转换与场景编译层 (Python / Nyan)]                                              │
│  • openage/convert/ (新增 DE2 8.4/8.9 多版本 dat 兼容 + 按需媒体过滤 + SLD 树木转换)  │
│  • openage/scenario/ (新增 Scenario IR -> Validation -> Nyan Mission Pack 编译器)   │
│  • assets/nyan/ww_mvp/ (Mission 1 & 2 原版 120×120 场景转换器 + 16×16 教学包)       │
├───────────────────────────────────────────────────────────────────────────────────┤
│ [C++ 战役与触发器运行时 (libopenage/scenario/)]                                     │
│  • MissionPackReader / ScenarioLoader: 原生加载地形分块、外交、资源、预置实体          │
│  • TriggerRuntime: 事件驱动 Condition / Effect 状态机                              │
│  • OriginalScenario: 原版 AoE2 触发器执行器 (刷兵、接管村庄、科技强制研发、胜利链)     │
│  • ObjectiveController / FeedingTheArmy / OutpostFire: 战役目标与建筑三阶段起火       │
├───────────────────────────────────────────────────────────────────────────────────┤
│ [C++ 核心玩法系统 (libopenage/gamestate/ & pathfinding/)]                          │
│  • StaticOccupancyGrid + DynamicOccupancy + SweptAABB + MovementReservation       │
│  • FormationPlanner + GroupMoveHandler + AttackApproach (2D 高地距离兼容)           │
│  • DiscreteDamage + ArmorClass 伤害计算 + EventLoop 延迟死亡移除                    │
│  • Economy & Town Center: 采集(浆果/伐木/采金/狩猎/捕鱼)、建造地基、训练村民、研发科技 │
│  • FogOfWar (三态迷雾) + 盟友视野共享 + 外交姿态切换                                │
├───────────────────────────────────────────────────────────────────────────────────┤
│ [表现层与交互 HUD (libopenage/presenter/ & input/ & renderer/ & assets/qml/)]      │
│  • PointerEvent + ContextActionResolver (右键语义分发：移动/攻击/采集/建造/修理)     │
│  • SelectionModel + SelectionOverlay (精灵像素级拾取、框选、脚印高亮)                │
│  • HudQmlAdapter + MissionHud.qml (资源/人口顶栏、目标面板、命令面板、中文教学提示)   │
│  • World2D / Terrain / Minimap Shaders (战争迷雾着色、血条、建造进度条、小地图)       │
└───────────────────────────────────────────────────────────────────────────────────┘
```

---

## 3. 新增模块完整清单（New Modules）

### 3.1 Python 场景编译管线（`openage/scenario/` & `openage/validate_nyan/`）

上游没有原版场景导入管线。`openage-plus` 新增了完整的场景中间表示（IR）与 Mission Pack 编译器：

- **[openage/scenario/ir.py](openage/scenario/ir.py)**：定义标准化的 `ScenarioIR` 数据结构（地图尺寸、地形网格、海拔高度、玩家初始资源、外交关系、实体摆放、触发器图）。
- **[openage/scenario/mission1_dump.py](openage/scenario/mission1_dump.py)**：解析原版场景与存档，提取完整的地形与实体清单。
- **[openage/scenario/validation.py](openage/scenario/validation.py)**：校验 `ScenarioIR` 中所有 `nyan` 实体引用（FQON）、坐标边界、玩家 ID 与触发器跨引用完整性。
- **[openage/scenario/writer.py](openage/scenario/writer.py)**：将 `ScenarioIR` 确定性地编译为原生可读的 Mission Pack（`manifest.toml`、`terrain.txt`、`placements.txt`、`triggers.txt`、`mission_01.nyan`）。
- **[openage/scenario/loader.py](openage/scenario/loader.py)** 与 **[openage/scenario/terrain_loader.py](openage/scenario/terrain_loader.py)**：Python 侧场景与地形快照加载器。
- **[openage/validate_nyan/main.py](openage/validate_nyan/main.py)**：独立的 `nyan` 语法与跨文件引用静态校验工具。

### 3.2 原版战役包提取与全文明科技树导出（`scripts/` & `assets/nyan/ww_mvp/`）

- **[scripts/export_wallace_scenarios.py](scripts/export_wallace_scenarios.py)**：
  - 支持直接读取 Steam 版《帝国时代 II：决定版》加密/封装的 `cam1.aoe2campaign`（包含 `GPV` 头部偏移识别与自动剥离）。
  - 提取 `1_Wallace_1.aoe2scenario` 与 `1_Wallace_2.aoe2scenario`，利用 `AoE2ScenarioParser` 导出全部地形（120×120）、玩家起始资源、禁用列表（`disabled_techs` / `disabled_units` / `disabled_buildings`）、5,698 / 10,130 个场景对象及 31 / 27 个原版触发器为结构化 JSON。
- **[assets/nyan/ww_mvp/original_scene.py](assets/nyan/ww_mvp/original_scene.py)**：
  - 将导出的原版 JSON 映射为 `de2_base` 中的真实 `nyan` 单位、建筑、树木、果树、金矿、装饰物与地形块。
  - **缺失精灵自动补齐**：对于 `de2_base` 转换结果中缺少 `idle_*.sprite` 的原版树木/悬崖（如 `349 OakTree`、`411 ForestTree`），自动从原版 `.sld` 图形文件即时转换纹理与 `.sprite` 定义。
  - **关卡禁用项对齐**：精确还原原版 Mission 1 禁止城镇中心训练村民、Mission 2 开放训练村民但禁用指定科技与建筑的规则。
- **[scripts/export_tech_trees.py](scripts/export_tech_trees.py)** 与 **[scripts/tech_tree/](scripts/tech_tree/)**：
  - 流式解析 `empires2_x2_p1.dat`，导出全文明（Civilizations）科技树、建筑/单位前置依赖图与可视化预览页 [scripts/tech_tree/preview.html](scripts/tech_tree/preview.html)。
- **[scripts/campaign_launcher.py](scripts/campaign_launcher.py)** 与 **[scripts/run_campaign.sh](scripts/run_campaign.sh)**：
  - 提供 GUI 弹窗与 CLI 双模式战役启动器，自动检测本机 AoE2:DE 安装目录并优先加载原版 120×120 场景。

### 3.3 C++ 战役加载与触发器系统（`libopenage/scenario/`）

上游没有 `libopenage/scenario/` 目录，`openage-plus` 全新构建了该子系统：

- **[libopenage/scenario/mission_pack_reader.cpp](libopenage/scenario/mission_pack_reader.cpp)**：C++ 原生读取 Mission Pack 清单、地形块、玩家配置、实体实例与触发器配置。
- **[libopenage/scenario/trigger/runtime.cpp](libopenage/scenario/trigger/runtime.cpp)** & **[instance.cpp](libopenage/scenario/trigger/instance.cpp)**：
  - 事件驱动的触发器状态机，支持 `TimerElapsed`、`EntityRemoved`、`TriggerFired`、`AreaReached`、`ResourceStockpile` 等条件，以及 `CompleteObjective`、`FailObjective`、`ActivateTrigger`、`SpawnEntity` 等效果。
- **[libopenage/scenario/trigger/original_scenario.cpp](libopenage/scenario/trigger/original_scenario.cpp)**：
  - 原版 `.aoe2scenario` 触发器在 `openage-plus` 中的原生执行器。
  - 支持条件：定时器（`type 10`）、区域内对象数量/接近检测（`type 1/3/5/22`）、对象存活/被毁（`type 2/4`）、科技状态与属性阈值（`type 6/14`）。
  - 支持效果：显示教程提示与语音文本（`effect 20/3`）、启停触发器（`effect 8/9`）、动态生成单位/增援（`effect 11`）、移除/击杀单位（`effect 14/24`）、外交变更（`effect 1`）、强制研发科技（`effect 2`，如给第 1 关民兵强制升级重装步兵）、镜头聚焦（`effect 19`）与宣告胜利（`effect 4`）。
  - **大地之母（Gaia）村庄自动接管（`capture_gaia`）**：当玩家单位抵达村庄范围时，基于 2D 水平距离（忽略高地 Z 轴误差）与建筑占地半径（`radius_x`），将大地之母的城镇中心、兵营、城墙与哨塔级联移交给玩家 1。
- **[libopenage/scenario/feeding_the_army.cpp](libopenage/scenario/feeding_the_army.cpp)**：Mission 2 经济采集与库存目标控制器。
- **[libopenage/scenario/outpost_fire.cpp](libopenage/scenario/outpost_fire.cpp)**：建筑按受损血量比例（75% / 50% / 25%）动态挂载原版三阶段火焰精灵特效。
- **[libopenage/scenario/shore_fishing.cpp](libopenage/scenario/shore_fishing.cpp)**：村民岸边捕鱼逻辑控制器。

### 3.4 C++ 碰撞、编队与战斗接近系统（`libopenage/pathfinding/`）

上游仅有一个基础网格 A* 寻路器。`openage-plus` 新增了完整的 RTS 空间碰撞与战术移动模块：

- **[libopenage/pathfinding/dynamic_occupancy.cpp](libopenage/pathfinding/dynamic_occupancy.cpp)**：维护所有具有物理碰撞体积（`has_footprint()`）的移动单位与建筑的实时空间占用，自动忽略无碰撞体积的纯装饰物（草丛、路标旗帜、隐藏触发标记）。
- **[libopenage/pathfinding/swept_aabb.cpp](libopenage/pathfinding/swept_aabb.cpp)**：移动线段连续碰撞检测（Swept AABB），防止单位穿模。
- **[libopenage/pathfinding/movement_reservation.cpp](libopenage/pathfinding/movement_reservation.cpp)**：多单位同时移动时的目标格点预约消歧管理器，避免多个单位挤向同一个坐标点导致死锁。
- **[libopenage/pathfinding/formation.cpp](libopenage/pathfinding/formation.cpp)**：多单位编队目标点规划器（`FormationPlanner`），计算稳定的阵型散开点位。
- **[libopenage/pathfinding/attack_approach.cpp](libopenage/pathfinding/attack_approach.cpp)**：近战与远程单位攻击接近服务（`AttackApproach`），在目标建筑/单位周围搜索可达的攻击站位，并使用 2D 水平距离（`std::hypot(dne, dse)`）判定高地/斜坡上的攻击距离。

### 3.5 C++ 交互输入与 QML 战役 HUD（`libopenage/input/` & `libopenage/presenter/` & `assets/qml/`）

- **[libopenage/input/pointer_event.cpp](libopenage/input/pointer_event.cpp)** & **[context_action_resolver.cpp](libopenage/input/context_action_resolver.cpp)**：
  - 将鼠标左键点选/框选、右键上下文指令解耦为语义动作（右键空地=移动、右键敌方=攻击接近、右键资源=采集、右键友方未完工地基=继续建造）。
- **[libopenage/presenter/hud/selection_model.cpp](libopenage/presenter/hud/selection_model.cpp)** & **[selection_overlay.cpp](libopenage/presenter/hud/selection_overlay.cpp)**：
  - 统一管理当前选中单位集合，并在世界空间渲染选中单位脚印高亮框（Selection Overlay Quads）。
- **[libopenage/presenter/hud/hud_qml_adapter.cpp](libopenage/presenter/hud/hud_qml_adapter.cpp)** & **[assets/qml/MissionHud.qml](assets/qml/MissionHud.qml)**：
  - 将 C++ `GameState` 实时数据绑定到 QtQuick QML HUD：顶部四项资源（食物、木材、黄金、石头）与人口上限、右上角任务目标与中英文教学提示、左下角选中单位属性（图标、名称、HP、攻击力、护甲、携带资源）、右下角动作命令面板（训练村民、研发织布机/封建时代、建造房屋/兵营/采矿场、暂停/继续建造、外交切换）。

---

## 4. 核心引擎逻辑修改深度解析（Modified Core Logic）

除了新增模块外，`openage-plus` 对上游核心代码进行了多项关键修复与能力增强：

### 4.1 资产转换器（`openage/convert/`）

1. **DE2 多版本 `empires2_x2_p1.dat` 兼容（[dat_compatibility.py](openage/convert/service/init/dat_compatibility.py) & [version_hashes.toml](cfg/converter/games/de2/version_hashes.toml)）**
   - 上游硬编码的 `genieutils` 解析器在较新的 macOS / Steam DE2 版本（如 8.9 布局）上会因字节偏移变化而崩溃。
   - 新增版本化布局配置（Profile 8.4 / 8.9），支持最新 Steam 版 DE2 数据文件转换。
2. **按需媒体导出过滤与元数据闭包（[media_filter.py](openage/convert/processor/conversion/de2/media_filter.py)）**
   - 全量转换 DE2 数千个 SLD 纹理解析耗时极长。新增按需媒体过滤机制，在保证 `nyan` 引用闭包完整的前提下，可快速转换战役所需资源。

### 4.2 游戏状态与模拟内核（`libopenage/gamestate/` & `libopenage/event/`）

1. **确定性同刻事件调度（[event/eventloop.cpp](libopenage/event/eventloop.cpp)）**
   - 修复同一时间戳（equal-time）下事件执行顺序不确定导致的测试抖动，保证无头测试与窗口运行行为完全一致。
2. **离散伤害结算与护甲相克（[gamestate/system/combat.cpp](libopenage/gamestate/system/combat.cpp) & [discrete_damage.cpp](libopenage/gamestate/discrete_damage.cpp)）**
   - 读取 `nyan` 中的 `Attack` 与 `Armor` 分类属性，按原版公式 $\max(1, \sum \max(0, \text{Attack}_i - \text{Armor}_i))$ 计算每次攻击造成的离散伤害，并跳过无效回退效果。
3. **事件驱动的死亡与实体移除生命周期（[gamestate/system/death.cpp](libopenage/gamestate/system/death.cpp) & [removal.cpp](libopenage/gamestate/system/removal.cpp)）**
   - 当实体血量降至 0 时，不再在迭代中途直接销毁内存，而是派生 `death` 状态并通过 `EventLoop` 延迟触发 `game.removal` 事件，同步清理 `DynamicOccupancy` 碰撞网格并通知 `TriggerRuntime`。
4. **动态生成单位与隐藏触发实体兼容（[gamestate/system/idle.cpp](libopenage/gamestate/system/idle.cpp)）**
   - 原版场景中存在大量无 `Idle` 能力的隐藏辅助对象（如 `Unit1291` 存活保持器）。修改 `Idle::idle()` 使其在实体缺少 `IDLE` 组件时安全返回 `0` 而非抛出异常导致崩溃。
   - 动态刷出的增援或敌军单位（`effect_type == 11`）在生成瞬间立即启动 `run_activity_system(at)`，确保其在无头模式和窗口模式下都能立即执行寻路与战斗。
5. **地图动态占用注册过滤（[gamestate/map.cpp](libopenage/gamestate/map.cpp)）**
   - `register_entity_in_dynamic_occupancy` 仅注册具备物理占地（`entity.has_footprint()`）的实体，避免原版场景中成百上千个草地贴花（`Unit1358`）、路标旗帜阻塞道路。

### 4.3 渲染管线与表现层（`libopenage/renderer/` & `libopenage/presenter/`）

1. **`.sprite` 空角度容错解析（[renderer/resources/parser/parse_sprite.cpp](libopenage/renderer/resources/parser/parse_sprite.cpp)）**
   - `de2_base` 生成的部分精灵（如 `idle_cliff.sprite`）声明了 `angle 255` 但无任何 `frame` 数据，上游 `angles.at()` 会直接抛出 `std::out_of_range` 导致窗口崩溃。现已过滤无帧的非镜像角度。
2. **战争迷雾、血条、建造进度与小地图着色器（[assets/shaders/world2d.frag.glsl](assets/shaders/world2d.frag.glsl) & [terrain.frag.glsl](assets/shaders/terrain.frag.glsl)）**
   - 在地形与 2D 实体着色器中集成三态战争迷雾（未探索全黑、已探索半透明灰暗、当前可见全亮），并在实体上方绘制实时血条与建筑施工进度条。
3. **初始镜头智能聚焦（[presenter/presenter.cpp](libopenage/presenter/presenter.cpp)）**
   - 启动场景时，初始相机优先计算玩家 1 拥有 `MOVE` 或 `BUILD` 组件的单位/建筑质心，避免被地图远端的触发器旗帜（如 `Unit600`）拉偏镜头。

---

## 5. 战役与关卡还原度详解（William Wallace Mission 1 & Mission 2）

| 维度 | Mission 1: Marching and Fighting | Mission 2: Feeding the Army |
| :--- | :--- | :--- |
| **原始文件** | `1_Wallace_1.aoe2scenario` | `1_Wallace_2.aoe2scenario` |
| **地图规模** | 120 × 120 完整原版地形与高低差 | 120 × 120 完整原版地形与河流浅滩 |
| **场景实体数** | 5,698 个原版对象 | 10,130 个原版对象 |
| **原版触发器数** | 31 个触发器完整导入 | 27 个触发器完整导入 |
| **城镇中心技能限制** | 按原版 `disabled_units` 禁止训练村民 | 开放训练村民，按原版禁用特定研究与建筑 |
| **核心流程闭环** | 初始民兵 -> 沿路标旗帜行军 -> 途中触发增援 -> 接近大地之母村庄自动接管城镇中心/兵营/哨塔 -> 触发器强制升级重装步兵 -> 迎战英军反击部队 & 摧毁英军前哨 -> 胜利 | 初始 1 名村民 -> 采集浆果丛 -> 触发器自动刷出第 2~4 名村民并提示建造粮仓/伐木场/采矿场 -> 城镇中心训练新村民 -> 集齐 50 食物、50 木材、50 黄金 -> 延时触发链宣告胜利 |

---

## 6. 跨平台（Linux / Windows / macOS）发布与打包路线图

为了让普通玩家在 **Linux、Windows、macOS** 三大平台上“下载即玩”，无需手动配置 Python 虚拟环境、C++ 编译器或命令行参数，`openage-plus` 规划了**三位一体**的发布架构：

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    openage-plus 跨平台发行包架构                         │
│                                                                         │
│   ┌────────────────────┐   ┌────────────────────┐   ┌────────────────┐ │
│   │ 1. 游戏引擎本体     │ + │ 2. 可视化转换向导   │ + │ 3. 战役启动器   │ │
│   │ (openage-plus core)│   │ (Converter GUI)    │   │ (Launcher GUI) │ │
│   └────────────────────┘   └────────────────────┘   └────────────────┘ │
└─────────────────────────────────────────────────────────────────────────┘
```

### 6.1 核心痛点：为什么 Converter 是跨平台发布的关键？

由于《帝国时代 II：决定版》（AoE2: DE）的原始美术、音效、战役文件受版权保护，**开源发行包不能直接内置转换后的专有贴图与音频资产**。因此，玩家下载 `openage-plus` 后，首次运行必须经历一次**本地资产转换（Asset Conversion）**：
1. 在开发态下，这一步依赖复杂的命令行调用（`openage convert`、`export_tech_trees.py`、`export_wallace_scenarios.py`、`install.sh`）。
2. 全量转换 DE2 数 GB 的 `.sld` 纹理耗时较长，且不同操作系统（Windows Steam 注册表路径、macOS `~/Library/Application Support/Steam`、Linux Proton/Steam 路径）的定位方式各不相同。

### 6.2 三大发布组件设计

#### 组件一：游戏引擎本体（Game Core Binary + 内置 Nyan 逻辑包）
- **包含内容**：
  - 预编译的 `libopenage` 动态库与主程序二进制（C++20 / Qt6 / OpenGL / Cython 扩展）。
  - 内置开源的 `nyan` 运行时库、着色器（`assets/shaders/`）、QML HUD（`assets/qml/`）以及战役逻辑模组定义（`assets/nyan/ww_mvp/`）。
- **内嵌 Python 运行时**：
  - 使用内嵌式 Python（Windows Embedded Python / macOS Framework / Linux AppImage Python）打包 `genieutils`、`AoE2ScenarioParser`、`nyan` 转换器模块，消除用户本机安装 Python 的门槛。

#### 组件二：Converter 可视化工具（首次运行资产转换向导）
- **自动路径探测**：
  - **Windows**：自动扫描 Steam 注册表（`HKCU\Software\Valve\Steam`）与各盘符 `SteamLibrary\steamapps\common\AoE2DE`。
  - **macOS**：自动扫描 `~/Library/Application Support/Steam/steamapps/common/AoE2DE`。
  - **Linux**：自动扫描 `~/.steam/steam/steamapps/common/AoE2DE` 与 Flatpak Steam 路径，同时支持手动点击“浏览...”选择游戏目录。
- **两级极速转换策略（Fast Campaign Mode vs. Full Mode）**：
  - **快速战役模式（推荐，约 30~60 秒）**：利用 `openage-plus` 的按需媒体过滤器（`media_filter.py`）和场景解包器，仅转换核心基础资源 + William Wallace 战役所需的 SLD 精灵、地形与场景，让玩家在 1 分钟内即可进入游戏。
  - **完整转换模式（后台增量进行）**：在后台线程转换全部文明建筑、单位与科技树资产，并提供可视化进度条、当前转换文件名、耗时预估与错误诊断日志导出。
- **资产版本校验与缓存**：
  - 转换产物存放于平台标准用户数据目录（Windows `%APPDATA%\openage-plus\assets`、macOS `~/Library/Application Support/openage-plus/assets`、Linux `~/.local/share/openage-plus/assets`），后续启动直接校验 `manifest.toml` 秒开。

#### 组件三：桌面战役启动器（Campaign Launcher GUI）
- **统一入口**：启动程序后首先打开 Qt6 可视化启动器（扩展自现有的 [scripts/campaign_launcher.py](scripts/campaign_launcher.py)）。
- **核心功能**：
  - **资产状态指示灯**：检测本地是否已完成 DE2 资产转换；若未转换，自动弹出 **Converter 可视化向导**。
  - **战役与关卡选择卡片**：以图文卡片形式展示 *William Wallace Mission 1: Marching and Fighting*、*Mission 2: Feeding the Army* 等关卡，支持切换“原版 120×120 完整场景”与“16×16 快速教学场景”。
  - **分辨率、语言与渲染设置**：支持选择窗口/全屏分辨率、中/英文教程提示与调试叠加层开关。

### 6.3 各平台打包形态（Packaging Targets）

| 平台 | 目标打包格式 | 打包工具链与构建方案 |
| :--- | :--- | :--- |
| **macOS** (Apple Silicon & Intel) | `openage-plus.app` + `.dmg` 镜像 | 使用 `macdeployqt` 自动收集 Qt6 框架与依赖 dylib，内嵌 Python 环境与 `nyan` 库，附带签名脚本 |
| **Windows** (x86_64) | 绿色便携包 `openage-plus-win64.zip` + Inno Setup 安装包 `.exe` | 基于 MSVC / vcpkg 构建，使用 `windeployqt` 打包 Qt6 DLL，集成 Windows Embedded Python |
| **Linux** (x86_64) | `openage-plus-x86_64.AppImage` + Flatpak | 基于 Ubuntu 24.04 LTS 构建，使用 `linuxdeploy` + `linuxdeploy-plugin-qt` 封装单文件可执行 AppImage |

---

## 7. 自动化测试与回归验证矩阵

`openage-plus` 建立了从 Python 单元测试、C++ 模拟测试到窗口真机验证的三层回归防护网：

1. **原版战役完整流程无头回归（Demo 53）**
   ```bash
   ./bin/run test -d gamestate.tests.simulation_demo 53
   ```
   - 自动验证原版 Mission 1（行军、村庄接管、强制科技升级、反击战斗、时间 `1081.0` 通关）与原版 Mission 2（禁训限制核对、采集浆果、刷出新村民、训练村民、集齐三项资源、时间 `345.0` 通关）。
2. **核心玩法与交互 35 项回归套件（P4.4D Regression）**
   ```bash
   bash scripts/p4_4d_regression.sh --regression
   ```
   - 覆盖触发器运行时（Demos 16~20）、碰撞体积与放置校验（Demos 21~26）、上下文右键动作与近战接近（Demos 27~35）、多单位编队与动态避障（Demos 36~45）、迷雾/外交/采集/建造/科技（Demos 46~52）。
3. **Python 场景编译器与科技树测试套件（129 项单元测试）**
   ```bash
   ./venv/bin/pytest openage/scenario/tests scripts/tests
   ```
   - 覆盖 `ScenarioIR`、地形导出、Mission Pack 读写、DE2 8.4/8.9 布局兼容性与科技树导出。
