# openage MVP Gameplay Spec — 单机 Age of Empires 风格可玩 Demo

状态: **活跃 (Active)** — 本文件是当前唯一指导开发的 gameplay spec。
日期: 2026-07-23
分支: `docs/rts-foundation-spec` (将基于此 spec 创建新分支)
取代: 本 spec 明确取代 `docs/superpowers/specs/2026-07-22-rts-gameplay-foundation-design.md` 与 `docs/superpowers/plans/2026-07-22-m0-determinism-harness.md` 作为**当前执行依据**。那两份文档予以**冻结保留**作长期参考,不得影响本 MVP(见 §10)。

---

## 1. 目标

以**最小改动、最快路径**产出一个**可玩的单机** Age of Empires 风格 Demo,玩家能依次通关 William Wallace 教学战役第 1–5 关。

- **可玩**的定义:玩家在桌面端用鼠标 + 键盘操作,**无需打开控制台、无需手动修改任何资源文件**,即可从启动游戏到完成每一关的胜利条件。
- **依次通关**:第 1 关 → 第 2 关 → … → 第 5 关,每关引入一个新机制,前一关的机制在后续关持续可用。
- **最小改动**:优先复用现有 `GameSimulation::run()`、事件循环、地图、渲染、移动、scenario loader、trigger、nyan 数据与已有组件;只在旧 loop 上增加少量边界清晰、日后可整体抽离的新模块。
- **桌面优先**:所有玩家输入统一转换为 `GameplayCommand`,为未来触屏适配留出转换层,但触屏本身不在本 MVP 范围。

### 1.1 五关机制引入表(本 spec 纠正了旧 tutorial 设计)

| 关卡 | 机制 | 玩家学会的事 | 胜利条件 |
|---|---|---|---|
| **Mission 1 — First Strike** | 选择、移动、攻击、伤害、死亡、胜利 | 框选单位、右键移动、A 键攻击敌人、看到掉血与死亡 | 击杀全部脚本敌人(≥1 个 `TutorialEnemyVillager`) |
| **Mission 2 — Gathering** | 食物、木材、黄金采集 + 资源投放 | 派村民采集树(木)/浆果(食)/金矿(金),返回 TC 卸货 | 三种资源各达到阈值(木≥50, 食≥50, 金≥50) |
| **Mission 3 — Build & Train** | 房屋、兵营、城镇中心建造、建造进度、人口上限、村民与民兵生产 | 建造房屋提人口、兵营训练民兵、TC 训练村民 | 训练出 ≥2 民兵 且 拥有 ≥1 房屋 |
| **Mission 4 — Advance** | 一个时代升级 + 少量科技 + 脚本化敌军进攻 | 研究时代升级(解锁民兵升级)、研究一项攻击/护甲科技、抵御敌军定时进攻 | 完成时代升级 且 击退 2 波脚本敌军 |
| **Mission 5 — Stirling** | 组合采集、建造、生产、战斗形成完整胜利闭环 | 经营经济 → 产兵 → 攻陷敌方城镇中心 | 摧毁敌方城镇中心 |

---

## 2. 非目标(冻结清单 — 显式不做)

以下项目在本 MVP 中**一律冻结、不实现、不引入**。若任何任务试图触碰,即偏离 spec:

- `libopenage/gamestate/sim/` 新目录及其 14-phase deterministic `DeterministicSimulationLoop`。
- M0 determinism harness、per-tick checksum、`CanonicalStateSerializer`、replay、lockstep。
- 多人游戏、网络代码、服务器权威、帧同步、重连。
- 通用 AI(敌人只用关卡脚本:定时刷兵、巡逻、攻击指定目标)。
- 完整文明系统(多 civ 选择 UI、文明差异平衡)— 固定单一 `TutorialPlayerSetup`。
- 完整科技树 — 仅 Mission 4 做 1 个时代升级 + ≤2 项科技。
- 完整地图/场景编辑器、AoE2 `.scx` 读取。
- 移动端 / 触屏适配。
- converter 扩展 — 除非直接阻塞当前关卡(目前不阻塞,tutorial 数据自包含,见 §6)。
- 新的复杂 ECS、新的事件总线、新的 Activity Graph 框架、没有当前调用方的抽象基类。

---

## 3. 事实核查结论(现状基线)

核查于 2026-07-23 完成,作为本 spec 与 plan 的事实地基:

- **构建**:`bin/run` 不存在,最近构建产物 mtime 为 2026-07-22 12:05。当前 HEAD 的 stopgap 改动(`62051247` scenario 装载 + Phase B trigger)**未经编译验证**。Mission 1 能否启动未知 — Phase 0 首要任务即验证。
- **4 个枚举已扩展**(Phase A1 完成):`command_t` 有 GATHER/BUILD/TRAIN/ATTACK;`ability_t` 覆盖 GATHER/DROP_RESOURCES/HARVESTABLE/CONSTRUCTABLE/FOUNDATION/CREATE/PRODUCTION_QUEUE/RESEARCH/ATTACK/RESISTANCE/...;`component_t` 有 GATHER/BUILD/TRAIN/PRODUCTION/RESOURCE_STORAGE;`system_id_t` 有 GATHER/GATHER_PICKUP/RETURN_TO_DROP/DROP_RESOURCES/BUILD_PROGRESS/TRAIN/RESEARCH。
- **activity graph 机制已 wire**:`definitions.h` 的 `ACTIVITY_TASK_SYSTEM_DEFS` 已映射 Gather/DropResources/Constructable/Create/Research → system_id,但 `system/activity.cpp:127-143` 的 `handle_subsystem` switch **只实现 IDLE/MOVE_COMMAND/MOVE_DEFAULT**,其余会抛 "Unhandled subsystem"。本 MVP **不补这些 case**(不扩展 activity graph),改走 tick 驱动(见 §4)。
- **命令派发**:`event/send_command.cpp:67-91` 只构造 IdleCommand/MoveCommand;GATHER/BUILD/TRAIN/ATTACK 落 `default` 只打 warn。命令类只有 `commands/{base,custom,idle,move}.h`,无 gather/build/train/attack。
- **输入**:`input/controller/game/controller.cpp::setup_defaults` 已有:Ctrl+LMB 生成 test 实体、RMB 对选中单位发 MOVE、LMB 框选(set_selected)。`controller->get_selected()` / `get_controlled()` 可用。无 G/B/T/A 键绑定。
- **移动执行**:MOVE 不每帧跑。`Move::move_default`(move.cpp:95)一次性把整条路径写进 `Position` 的 `curve::Continuous<coord::phys3>`(set_last),再用 `game.wait` 事件定时恢复 activity graph。渲染层自行从 curve 插值读位置。
- **组件**:Position/Ownership/CommandQueue/Activity/Live 有实质实现;Gather(仅 `carried_amount`)/Build/Train/Production/ResourceStorage 是**空 `get_type()` 桩**(`component/api/{gather,build,train,production,resource_storage}.h`)。Attack 能力在 `entity_factory.cpp:319-325` 匹配但**不挂任何组件**。
- **Live**:HP 存于 `curve::UnorderedMap<fqon, Discrete<int64_t>>`;`set_attribute` 写后触发 `on_attribute_change` 回调;但**无公开 HP getter**,且 `entity_factory.cpp:250-269` 创建 Live 时**未注册 HP≤0 回调**。
- **GameState**:`get_game_entities()`/`get_players()`/`get_map()` 可用;`entity_id_t`=`player_id_t`=`uint64_t`;**无 `remove_game_entity`**(无销毁 API);无按 owner 索引。
- **Player**:`add_resource`/`get_resource`/`has_enough`/`spend` 可用;资源存 `unordered_map<fqon,int64_t>`;每个 Player 持独立 `nyan::View`(civ patch 已能 apply,`entity_factory.cpp:172-194`)。
- **scenario loader**:`Game::load_scenario`(game.cpp:174+) 已能从 nyan Scenario 读 size/chunks/initial_entities 并 spawn;`APIScenario` getter 齐全。`terrain_factory->add_chunk_with_tiles` / `add_terrain` 签名匹配。
- **tutorial 数据**:`assets/converted/engine/util/{tutorial_data,setup/tutorial_setup,scenario/mission_1}.nyan`。`tutorial_data.nyan` 只有 TutorialVillager(Idle/Move/Turn,speed 3.0)、TutorialTownCenter(Idle)、TutorialEnemyVillager(Idle/Move/Turn)、TutorialGrass。**无 Live/Attack/Gather 能力,无树/资源点实体,无建筑 Constructable**。`tutorial_setup.nyan` 起始 wood=0。
- **trigger**:`TutorialTrigger`(trigger.h/cpp)是线性 step 状态机,Mission 1 step1 谓词 `wood>=50`(但 gather 未实现 → 永不满足);Mission 2-5 是 `return false` 占位。
- **converter**:streaming T1-T7 / BfG / Wwise / 探针在本地 master 已完成(未合并 origin/master);有一组未提交改动(output_checks/preflight/增量导出)。**与本 MVP 无关**,冻结。
- **GUI**:`assets/qml/IngameHud.qml` 存在,但依赖 `converted/interface/hud*.slp.png` 等 de2_base 图;tutorial 自包含模式下这些图不存在 → HUD 渲染不全。需用 QtQuick 原语重写最小 HUD(不依赖 de2_base)。

---

## 4. 架构边界

### 4.1 执行模型决策(关键)

现有 openage 是**事件驱动**,Move 通过 activity graph + `game.wait` 事件执行,非每帧 tick。本 MVP 的**新系统**(战斗/经济/建造/生产/任务导演)**采用 tick 驱动**,直接挂在 `GameSimulation::run()` 的 `event_loop->reach_time()` 之后,与现有 `TutorialTrigger::on_update` 同位置同模式(simulation.cpp:60-72)。

- **新增一个 facade** `GameplaySystems::tick(time, state)`,在 run() 中调用,内部依次 tick 各子系统。边界单一,日后可整体抽离。
- **Move 保持不变** — 继续走 activity graph,不触碰。
- **新系统不扩展 activity graph** — `handle_subsystem` switch 不新增 case;`system_id_t` 里 GATHER/BUILD/TRAIN/RESEARCH 等**保持声明但不 wire**(冻结,见 §10)。
- **进度计时**:建造/训练进度用组件内字段 `start_time` + `finish_time`,每 tick 比较 `current_time >= finish_time` 判定完成。不引入 curve 完成回调、不引入新事件类型 — 最简 poll 模型。

### 4.2 复用清单(不重写)

| 子系统 | 复用点 |
|---|---|
| 主循环 | `GameSimulation::run()`(simulation.cpp:50)、`event_loop->reach_time` |
| 命令链路 | `SendCommandHandler` + `CommandQueue` + `Commander`(event/send_command.cpp) |
| 移动 | activity graph + `Move::move_default`(move.cpp:95)+ `Position` curve + Pathfinder |
| 实体生成 | `EntityFactory::add_game_entity` + `init_components` + `SpawnEntityHandler` 范式 |
| 场景装载 | `Game::load_scenario` + `APIScenario` + `terrain_factory` |
| 渲染 | `WorldObject` / `RenderEntity` / `render_update`(game_entity.cpp:59)+ presenter attach_renderer |
| 输入 | `controller.cpp::setup_defaults` 的 binding 框架 |
| nyan patch | `Transaction::add/commit`(player view,entity_factory.cpp:172-194)— 用于 civ/科技 |
| Player/资源 | `Player::add_resource/spend` |
| 组件基类 | `Component`/`APIComponent`/`InternalComponent` + `entity->get_component` |

### 4.3 新增清单(边界清晰、可抽离)

全部位于 `libopenage/gamestate/` 下,新增文件集中在可识别的子目录/前缀:

1. **GameplayCommand**(扩展现有 `component::command::Command` 层次,非新框架):新增 `GatherCommand`/`BuildCommand`/`TrainCommand`/`AttackCommand` 类 + 扩 `send_command.cpp` switch + 扩 `controller.cpp` 键绑定。**所有玩家输入归一为 command 对象。**
2. **CombatSystem** — tick 驱动:扫描有 Attack 组件 + 有 ATTACK 命令的实体,驱动攻击周期(接近→周期扣目标 HP→死亡)。
3. **EconomySystem** — tick 驱动:驱动采集状态机(走→采→返→卸),写 Player 资源。
4. **ConstructionSystem** — tick 驱动:推进建造进度,完成时转 foundation→实体。
5. **ProductionSystem** — tick 驱动:推进训练计时,完成时 spawn 新单位。
6. **MissionDirector** — tick 驱动:拥有(step 进度 + 脚本化敌军 + 胜负判定),取代 run() 中 inline 的 `if(tutorial_trigger)` 块。
7. **组件补全**:Live 加 getter、GameState 加 `remove_game_entity`、Gather/Build/Train/Production/Attack 补**真实字段**(非桩)。
8. **GUI backend**:一个 `QObject`(经现有 qmlRegister 桥)向 QML 暴露 资源/人口/选中信息/任务提示/胜负。

### 4.4 禁止清单(不引入)

- 新 ECS / 实体-组件注册框架(沿用现有 `unordered_map<component_t, Component>` 单实例模型)。
- 新事件总线(沿用 `event::EventLoop` + `EventEntity::changes`)。
- 新 Activity Graph 节点类型 / 新 task system case。
- 没有当前调用方的抽象基类 / 接口。
- 大量 placeholder / TODO / 只挂组件不实现行为 — **每个新增组件字段必须有对应 system 读写它**。

---

## 5. 代码处置分类

| 处置 | 对象 | 说明 |
|---|---|---|
| **保留复用** | §4.2 全部 | 不改或仅最小扩展 |
| **保留 + 扩展** | `send_command.cpp` switch、`controller.cpp::setup_defaults`、`entity_factory.cpp::init_components`(挂 Attack 组件 + 注册 Live 回调)、4 个枚举、`IngameHud.qml`(重写内容) | 在既有结构上加分支/字段,不重构 |
| **迁移(桩→真实)** | `component/api/{gather,build,train,production,resource_storage}.{h,cpp}`、`component/api/live.{h,cpp}`(加 getter) | 现有空桩补真实字段与方法,由对应 system 读写 |
| **迁移(逻辑搬家)** | `simulation.cpp:60-72` 的 inline trigger tick → `MissionDirector::tick`;`create_tutorial_trigger` 路由 → `MissionDirector` | 行为不变,换归属 |
| **保留(冻结参考)** | `docs/superpowers/specs/2026-07-22-rts-gameplay-foundation-design.md`、`docs/superpowers/plans/2026-07-22-m0-determinism-harness.md`、`docs/superpowers/plans/2026-07-21-lazy-convert.md` 等 | 不删,不改,不作为执行依据 |
| **冻结(不 wire)** | `system_id_t` 的 GATHER/BUILD/TRAIN/RESEARCH 等条目、`ACTIVITY_TASK_SYSTEM_DEFS` 对应映射、`handle_subsystem` 不加 case | 保留枚举避免 churn,但走 tick 路径而非 activity 路径 |
| **冻结(不碰)** | converter 全部(`openage/convert/**`)、streaming/BfG/Wwise 主干及未提交改动 | 不阻塞 tutorial,不扩展 |
| **删除/清理** | `simulation.cpp:66-70` 的 `static bool logged_completion` hack(迁入 MissionDirector);未跟踪 `plan-tutorial-lvl.md`(旧 transcript,可删) | 迁移后移除 hack |

---

## 6. 最小数据模型(tutorial nyan,自包含)

所有数据继续放在 `assets/converted/engine/util/` 下,**不依赖 de2_base**(converter 冻结)。逐关增量扩展 `tutorial_data.nyan` 与新增 `scenario/mission_N.nyan` + 必要时新 PlayerSetup。

### 6.1 数据原则

- 每关只新增该关所需实体/能力,不多做。
- 复用同一 `TutorialPlayerSetup`(wood=0),各关起始资源在 scenario 的 PlayerSetup 里调,或 MissionDirector 启动时注入。
- 敌人 = `owner_id != 0` 的同种实体(TutorialEnemyVillager 已有),不另造类型。

### 6.2 逐关数据需求(实现时按 plan 落地)

| 关 | 需新增/扩展的 nyan |
|---|---|
| M1 | 给 TutorialVillager + TutorialEnemyVillager 加 `Live`(HP) + `Attack`(damage/range/rate)能力;mission_1 scenario 放 1 玩家村民 + 2 敌人村民 |
| M2 | 新增 `TutorialTree`(Harvestable, wood)、`TutorialBerry`(food)、`TutorialGoldMine`(gold)实体 + 资源量;给村民加 `Gather` + `DropResources` 能力;TC 加 DropResources |
| M3 | 新增 `TutorialHouse`(Constructable+Foundation, ProvideContingent)、`TutorialBarracks`(Constructable+Foundation, Create militia)、给 TC 加 Create villager;村民加 Build 相关 |
| M4 | 新增 `AgeUpgrade` 研究(ResearchableTech)+ 1 项攻击/护甲科技(patch);解锁民兵升级(PassiveTransformTo) |
| M5 | mission_5 scenario:玩家 TC + 资源点 + 远处敌方 TC + 脚本刷兵点 |

### 6.3 资源点建模(关键决策)

地形 tile **无** ResourceSpot 字段(核查确认)。本 MVP **把树/浆果/金矿做成带 `Harvestable` 能力的 GameEntity**,并在该实体挂一个资源量组件(`ResourceStorage` 复用为存量 + 资源类型 FQON)。采集 = EconomySystem 让村民走到该实体、按 rate 减其存量、增村民 carried、返 TC 卸货入 Player。**不**改 terrain schema。

---

## 7. 各系统职责

### 7.1 GameplayCommand(命令归一)

- 扩展 `component::command::Command`(base_command.h)。新增:
  - `GatherCommand(entity_id_t target_spot)` — 采集某资源点实体
  - `BuildCommand(fqon_t building_fqon, coord::phys3 pos)` — 在 pos 建造
  - `TrainCommand(fqon_t creatable_fqon)` — 训练(发给建筑)
  - `AttackCommand(entity_id_t target_id)` — 攻击目标
- 扩 `send_command.cpp:67-91` switch 构造对应类(替换当前 warn-only)。
- 扩 `controller.cpp::setup_defaults` 加键绑定:选中村民时 `G/B`,选中建筑时 `T`,选中单位时 `A`;鼠标右键在点上发 Gather/Attack(按目标类型判定),空地发 Move。
- **CommandQueue 消费协调**:`activity/condition/next_command.cpp` 的 `next_command_move` 条件收窄为"队首是 MoveCommand 才 true",使 GATHER/BUILD/TRAIN/ATTACK 命令留队等待 tick 系统 pop。这是唯一的 activity 侧改动(一行条件),非新框架。

### 7.2 CombatSystem

- `tick(time, state)`:遍历 `get_game_entities()`,对有 AttackComponent 且队首有 AttackCommand 的实体:
  - 若超出攻击距离 → 复用 `Move::move_default` 向目标移动(写 Position curve)。
  - 若在距离内且攻击周期到(`now >= next_attack_time`)→ 读目标 Live,`set_attribute(time, hp_fqon, hp - damage)`,推 `next_attack_time = now + rate`。
- 死亡:Live 的 HP≤0 回调(entity 创建时注册)→ `GameState::remove_game_entity(id)`(新增)+ 通知 renderer + MissionDirector 记账(用于胜负与脚本)。
- 简化:不做投射物(近战直扣 + 远程直扣,共用距离判定);不做完整 Resistance 减伤(M4 才接入 1 项护甲科技减伤)。

### 7.3 EconomySystem

- `tick(time, state)`:对有 GatherComponent 且队首有 GatherCommand 的村民,驱动状态机:
  - `WALKING_TO_SPOT` → 到达 → `CHOPPING`(按 gather_rate 累加 carried_amount、减资源点存量)
  - carried 满 或 资源点耗尽 → `WALKING_BACK` → 到达 TC → `DROP_RESOURCES` → `player->add_resource(type, carried)` → 回 `IDLE`
- 移动同样复用 `Move::move_default`。
- 资源类型 FQON 来自资源点实体的 Harvestable 能力配置。

### 7.4 ConstructionSystem

- `tick(time, state)`:对有 BuildComponent 的 foundation 实体(玩家下达 BuildCommand 后由系统在 pos 创建 foundation 实体):
  - 有村民在建造(`now >= next_progress_time`)→ 推进 `progress` 字段。
  - `progress >= 1.0` → 销毁 foundation,用同一 FQON 经 `EntityFactory::add_game_entity` 生成完整建筑(复用 spawn 范式)。
- 扣资源:`BuildCommand` 入队时(MissionDirector/输入层)校验 `player->has_enough` + `spend`。

### 7.5 ProductionSystem

- `tick(time, state)`:对有 TrainComponent 且队首有 TrainCommand 的建筑:
  - 开始训练:`player->spend(cost)` → 设 `active_fqon` + `finish_time = now + train_time`。
  - `now >= finish_time` → 经 `EntityFactory::add_game_entity` spawn 新单位(传 active_fqon,不硬编码列表 — 修 `SpawnEntityHandler` 的 test 轮询或新建生成路径),设 owner + 出现在建筑旁。
- 单 slot(tutorial 简化)。

### 7.6 MissionDirector

- `tick(time, state)`:单一导演,取代 run() inline 块。职责三块:
  1. **step 进度**:沿用 `TutorialTrigger` 的 step 谓词轮询(每关一组谓词,实际接入系统产出),完成推下一步。
  2. **脚本化敌军**:按时间表 `spawn` 敌人 + 发 `AttackCommand`(攻击玩家指定单位/建筑)+ 巡逻(Move 到巡逻点)。**无通用 AI**,纯时间表 + 固定目标。
  3. **胜负判定**:胜利 → 显示通关;失败(玩家关键单位/TC 死亡)→ 显示失败 + 可重开。
- per-mission 脚本:`mission_N` 的 spawn 时刻表 + 胜负条件,硬编码在 director 的 mission factory(沿用现有 `create_mission_N_trigger` 结构)。

---

## 8. 关卡需求(验收细节见 §9)

### Mission 1 — First Strike
- 玩家:1 村民。敌人:2 `TutorialEnemyVillager`。地图:16×16 草地 + 玩家村民 + 敌人(分置)。
- 玩法:框选/点选村民 → 右键空地移动 → A + 点敌人攻击 → 看到敌人 HP 下降 → 死亡消失 → 全清胜利。
- **纠正**:旧设计 M1=gather wood 是错的;真正第 1 关教战斗闭环(选择/移动/攻击/伤害/死亡/胜利)。

### Mission 2 — Gathering
- 玩家:1 村民 + 1 TC。地图散布树(木)/浆果(食)/金矿(金)。
- 玩法:选村民 → 右键树(Gather)→ 采 → 返 TC 卸货 → 资源栏 +N。三种资源各达阈值胜利。

### Mission 3 — Build & Train
- 玩家:2 村民 + TC,起始给足 wood。可建房屋、兵营。
- 玩法:建房屋(提人口)→ 建兵营 → 兵营训练民兵 → TC 训练村民。训练出 ≥2 民兵 + ≥1 房屋胜利。

### Mission 4 — Advance
- 玩家:小型基地。可研究时代升级 + 1 项攻击/护甲科技。
- 敌军:定时 2 波进攻(脚本刷兵攻击玩家建筑)。
- 玩法:升级时代(解锁民兵升级)→ 研究科技 → 抵御 2 波 → 胜利。

### Mission 5 — Stirling
- 玩家:TC + 资源点。敌方:远处 1 TC + 脚本刷兵。
- 玩法:采集 → 建兵营 → 产兵 → 推进 → 摧毁敌方 TC 胜利。

---

## 9. 验收标准

### 9.1 通用(每关都必须满足)

- `ninja -C bin` 编译通过,`bin/run --tutorial --tutorial-scenario-fqon engine.util.scenario.mission_N.MissionN` 启动到画面。
- **无需控制台命令**(除启动指令)、**无需手动改任何资源文件**即可游玩。
- 每关引入的机制在该关及后续关持续可用(无回归)。
- 无新增 `// TODO` 表示未完成功能;无只挂不用的组件字段。

### 9.2 逐关 DoD(Definition of Done)

| 关 | DoD |
|---|---|
| M1 | 玩家能框选/点选村民,右键移动,A 键攻击,敌人 HP 条/数值下降,HP≤0 敌人消失,全清后画面显示 Mission 1 完成。 |
| M2 | 右键树/浆果/金矿触发采集,村民走→采→返 TC→卸货,资源栏三种资源递增,各达 50 显示完成。 |
| M3 | 能建房屋(进度条到 100% 转实体,人口上限 +N),建兵营,兵营训练民兵(扣资源+计时+产出),TC 训练村民;≥2 民兵 + ≥1 房屋后完成。 |
| M4 | 能研究时代升级(计时+完成),研究 1 项科技(patch apply 生效,单位数值变化可验证),2 波敌军定时进攻并被击退,完成。 |
| M5 | 采集→建造→产兵→推进→摧毁敌方 TC 全链路通,显示胜利。 |

### 9.3 GUI 验收(Phase 6)
- 资源栏(木/食/金)、人口(x/上限)、选中单位信息(名/HP)、基础操作按钮(G/B/T/A 提示)、任务提示文本、暂停、重开、胜负界面齐全且**不依赖 de2_base 图**(QtQuick 原语)。

---

## 10. 技术债边界

### 10.1 显式技术债(本 MVP 接受,记录不修)

- **patch 不实时刷新已有实体**:研究科技 commit patch 后,仅对**新创建**实体生效;已有实体不立即重读 nyan view。M4 科技效果通过"研究完成后只影响后续产出单位 + 对已有单位用 CombatSystem 直接调数值"绕过。完整 curve 推 delta 留作长期债。
- **单 slot 训练队列**:无多单位排队。
- **简化战斗**:无投射物动画、无完整 Resistance 矩阵。
- **脚本化敌军硬编码**:时间表写死在 MissionDirector,无可配置 trigger 引擎。
- **自包含 tutorial 数据**:不接真实 AoE2 数据(M5 之后可评估切到 de2_base)。
- **tick 驱动系统与 activity graph 并存**:两套执行模型(Move 走 activity,其余走 tick)。边界清晰但非理想统一。日后若回到 spec 路线,系统可整体抽离。

### 10.2 冻结参考(长期,不影响当前)

`docs/superpowers/specs/2026-07-22-rts-gameplay-foundation-design.md`(确定性 SimLoop)与 `docs/superpowers/plans/2026-07-22-m0-determinism-harness.md`(M0 harness)是**长期联网/确定性目标**的设计参考,保留不删。**但**:当前 MVP 明确不实现它们,任何任务不得以"为将来 spec 铺路"为由引入 sim/、checksum、replay、lockstep。若未来需要联网,届时基于已通关的 MVP 做确定化迁移,而非现在预先建框架。

### 10.3 不接受的债(禁止)

- 只挂组件不实现行为的桩。
- 没有调用方的抽象。
- 为"将来可能需要"而引入的框架。
- 任何冻结清单(§2)项目的实现。
