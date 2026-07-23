# openage MVP 实施计划 - 按可玩纵切片拆分

状态: **活跃 (Active)** - 本文件是当前唯一执行依据的 plan。
日期: 2026-07-23
配套 spec: `docs/mvp/mvp-gameplay-spec.md`(必读,术语与架构边界以 spec 为准)。

## 总原则

- **顺序不可跳**:Phase 0 -> 1 -> 2 -> 3 -> 4 -> 5 -> 6 -> 7。每 Phase 必须以"玩家无需控制台、无需改资源、可实际通关对应关卡"为完成标准。
- **任务粒度小**:每个任务可独立编译验证。
- **禁止桩**:不创建只挂不用的组件字段、不写表示未完成的 TODO、不留无调用方的抽象。
- **每任务五要素**:修改文件 / 实现内容 / 测试方式 / 人工验收步骤 / 完成定义(DoD)。
- 构建命令:`ninja -C bin -j$(nproc)`。启动:`bin/run --tutorial`。
- 文中行号为 2026-07-23 HEAD 快照,实现时以实际为准。

---

## Phase 0 - 验证当前 tutorial 可编译启动(第一道闸)

**目标**:确认现有 stopgap 能编译,`--tutorial` 能进画面看到村民 + TC。这是后续一切的前提 - 现状 `bin/run` 不存在,stopgap 未编译验证。

### T0.1 清理工作区,建 MVP 分支
- **修改文件**:无代码改动。
- **实现内容**:`git checkout -b mvp/tutorial-demo` (基于当前 HEAD)。保留未提交 converter 改动不动(不属本 MVP,但也不删)。删除未跟踪 `plan-tutorial-lvl.md`(旧 transcript)。
- **测试方式**:`git status` 确认干净(除已知 converter 未提交组)。
- **人工验收**:`git log --oneline -1` 显示在 `62051247` 之上。
- **DoD**:分支创建,工作区可识别。

### T0.2 编译并修复至通过
- **修改文件**:按编译错误定位(预期可能涉及 `game.cpp` load_scenario、`simulation.cpp`、`trigger.cpp` 的类型/包含问题)。
- **实现内容**:`ninja -C bin -j$(nproc)`。逐个修编译错误。**不预判错误** - 以实际编译输出为准。若 `load_scenario` 里 `chunk_layout.get_set("TerrainChunkLayout.tiles")` 或 `placed.get<nyan::ObjectValue>("PlacedEntity.entity")` 类型不匹配,按实际 nyan schema 调整。
- **测试方式**:ninja 退出码 0,`bin/run` 生成。
- **人工验收**:`ls -la bin/run` 存在。
- **DoD**:`ninja -C bin` 无错误,`bin/run` 二进制存在。

### T0.3 启动验证 tutorial 进画面
- **修改文件**:无(若启动崩溃则定位修复)。
- **实现内容**:`bin/run --tutorial --no-prompts`(默认 scenario=mission_1,player_setup=tutorial_setup)。
- **测试方式**:观察窗口出现,日志含 `load_scenario: ... size=16x16 chunks=4`、`initial_entities=2`、`Game simulation started`。
- **人工验收**:画面显示 16×16 草地 + 1 村民 + 1 TC;右键村民能移动(现有 MOVE 绑定);Ctrl+LMB 能生成 test 实体。
- **DoD**:tutorial 启动到可交互画面,无崩溃,移动可用。**记录任何已知异常作为 M1 输入。**

> Phase 0 完成定义(整 Phase):**玩家可启动 tutorial 看到画面并移动村民**,无需任何控制台/资源操作。这是 spec §9.1 的"通用"前置。若编译/启动有阻塞性架构问题,先在此 Phase 解决,不进 M1。

---

## Phase 1 - Mission 1:选择、移动、攻击、伤害、死亡、胜利

**目标**:玩家能选单位、移动、攻击敌人、看到掉血与死亡、全清后胜利。引入 `GameplayCommand` 框架 + `CombatSystem` + `MissionDirector` 骨架。

### T1.1 数据:给村民/敌人加 Live + Attack 能力
- **修改文件**:`assets/converted/engine/util/tutorial_data.nyan`。
- **实现内容**:给 `TutorialVillager`、`TutorialEnemyVillager` 的 abilities 加 `Live`(含 `Live.attributes` -> `AttributeSetting`(attribute=HP, starting_value=40))和 `Attack`(damage=4, range=1.0, rate=1.0s)。新增 `TutorialHP`(`engine.util.attribute.Attribute` 子类,如 schema 要求)。新增 `Attack` 能力对象 `TutorialAttack`。
- **测试方式**:`bin/run --tutorial` 启动不报 nyan 解析错。
- **人工验收**:日志无 nyan load error。
- **DoD**:villager/enemy 实体带 Live+Attack 能力加载成功。

### T1.2 组件:Live 加 HP getter + 注册死亡回调
- **修改文件**:`libopenage/gamestate/component/api/live.{h,cpp}`、`libopenage/gamestate/entity_factory.cpp`。
- **实现内容**:`Live` 新增 `get_attribute(time, fqon) -> std::optional<int64_t>`(内部 `attribute_values.at(time,fqon)` -> `get(time)`)。`entity_factory.cpp:250-269` 创建 Live 后,注册 `on_attribute_change(hp_fqon, [](int64_t v){ if(v<=0) mark_dead; })` - 但实际销毁走 T1.5 的 `remove_game_entity`(回调只置标志,由 CombatSystem/MissionDirector tick 执行销毁,避免在 curve 回调里重入)。
- **测试方式**:单元测试可加 `live_test.cpp` 验证 set/get_attribute。
- **人工验收**:暂无(内部 API)。
- **DoD**:Live 可读可写 HP,HP≤0 有回调路径。

### T1.3 组件:AttackComponent + entity_factory 挂载
- **修改文件**:新建 `libopenage/gamestate/component/api/attack.{h,cpp}` + 注册 `component_t::ATTACK`(component/types.h 已有枚举值?核查:当前无 ATTACK 枚举 - **需在 component/types.h 加 `ATTACK`**)、`entity_factory.cpp:319-325`(替换 no-op 为挂 AttackComponent)、`component/api/CMakeLists.txt`。
- **实现内容**:`AttackComponent` 持从 ability 读的 `damage`/`range`/`rate` + 运行时 `next_attack_time`。`init_components` 匹配 `engine.ability.type.Attack` 时实例化并读字段(仿 `move.cpp:112` 的 `ability.get<nyan::Float>("Move.speed")` 模式,读 Attack.damage/range/rate)。
- **测试方式**:启动带 Attack 能力的实体不崩。
- **人工验收**:日志无 "unhandled ability parent"。
- **DoD**:AttackComponent 挂载并从 nyan 读到数值。

### T1.4 GameState 加 remove_game_entity
- **修改文件**:`libopenage/gamestate/game_state.{h,cpp}`。
- **实现内容**:`remove_game_entity(entity_id_t)`:从 `game_entities` erase + 通知 renderer(`render_entity` 置空/隐藏)+ 通知 manager。返回是否删除。
- **测试方式**:单元测试加/删实体计数。
- **人工验收**:暂无。
- **DoD**:可销毁实体且不泄漏(渲染层跟随)。

### T1.5 CombatSystem
- **修改文件**:新建 `libopenage/gamestate/system/combat.{h,cpp}` + `system/CMakeLists.txt`。
- **实现内容**:`static void tick(const shared_ptr<GameState>&, const time::time_t&)`:遍历 `get_game_entities()`,对有 AttackComponent 且 CommandQueue 队首为 AttackCommand 的实体:读 target Position,若距离>range 调 `Move::move_default`(move.cpp:95)向目标移动;若在 range 内且 `now>=next_attack_time` -> `target_live->set_attribute(now, hp_fqon, hp-damage)`,推 `next_attack_time=now+rate`。HP≤0 的实体(由 Live 回调置 dead 标志或本 tick 扫描)`remove_game_entity`。
- **测试方式**:Mission 1 端到端(见 T1.8)。
- **人工验收**:暂无。
- **DoD**:CombatSystem 能驱动攻击 + 扣血 + 销毁。

### T1.6 GameplayCommand:AttackCommand + 派发 + 输入绑定
- **修改文件**:新建 `libopenage/gamestate/component/internal/commands/attack.{h,cpp}` + `commands/CMakeLists.txt`、`event/send_command.cpp:67-91`(加 ATTACK case 构造 AttackCommand)、`input/controller/game/controller.cpp::setup_defaults`(加 A 键绑定:选中单位时 A+LMB 点实体发 AttackCommand;右键点实体也发 AttackCommand)。
- **实现内容**:`AttackCommand(entity_id_t target_id)`。controller 需把鼠标点击的世界坐标转为"点中哪个实体"(遍历 get_game_entities 取最近 Position)。
- **测试方式**:启动后按 A 点敌人,日志见 AttackCommand 入队。
- **人工验收**:同上。
- **DoD**:攻击命令能从输入到 CommandQueue。

### T1.7 MissionDirector 骨架 + 接入 run()
- **修改文件**:新建 `libopenage/gamestate/mission/mission_director.{h,cpp}`(新目录 `mission/`)+ `gamestate/CMakeLists.txt`、`simulation.{h,cpp}`。
- **实现内容**:`MissionDirector` 持 `TutorialTrigger`(step 进度,复用现有)+ 脚本化敌军 + 胜负。`tick(time, state)`:先 tick trigger;M1 无脚本刷兵(敌人初始就有);胜负=敌人全死。移除 `simulation.cpp:60-72` 的 inline `if(tutorial_trigger)` 块 + `static bool logged_completion` hack,替换为 `mission_director->tick(current_time)`。`create_tutorial_trigger` 路由逻辑迁入 director。`GameplaySystems::tick` facade:在 run() reach_time 后调 `mission_director->tick` + `combat_system->tick`。
- **测试方式**:启动 mission_1,director 日志输出当前 step。
- **人工验收**:日志见 director 接管。
- **DoD**:run() 通过 director tick,trigger 不再 inline。

### T1.8 Mission 1 数据 + 胜利
- **修改文件**:`assets/converted/engine/util/scenario/mission_1.nyan`、`trigger.cpp`(M1 谓词)。
- **实现内容**:`mission_1.nyan` 的 initial_entities 改为 1 玩家村民 + 2 敌人村民(owner=1,用 TutorialEnemyVillager)。M1 trigger step1 谓词改为"owner!=0 的存活实体数==0"(遍历 get_game_entities,Ownership owner!=0 且未 dead)。hint 文本改为战斗教学。
- **测试方式**:端到端。
- **人工验收**:1) 启动 mission_1 看到村民 + 2 敌人。2) 框选村民,右键移动。3) A+点敌人,敌人 HP 下降。4) 敌人 HP 归零消失。5) 全清后显示 Mission 1 完成。
- **DoD**:**玩家无需控制台/改资源,完成 Mission 1 战斗闭环并通关。**(spec §9.2 M1)

> Phase 1 完成定义:Mission 1 可玩可通关。引入了 GameplayCommand 框架 + CombatSystem + MissionDirector + 实体销毁。后续 Phase 在此上叠加。

---

## Phase 2 - Mission 2:采集 + 资源投放

**目标**:村民采集树/浆果/金矿,返 TC 卸货,三种资源增长。引入 `EconomySystem` + 资源点实体。

### T2.1 数据:资源点实体 + 村民 Gather 能力
- **修改文件**:`tutorial_data.nyan`、`mission_2.nyan`(新建 scenario)。
- **实现内容**:新增 `TutorialTree`(Harvestable + 资源类型=Wood + 存量 100)、`TutorialBerry`(Food)、`TutorialGoldMine`(Gold)。给 `TutorialVillager` 加 `Gather` + `DropResources` 能力。`TutorialTownCenter` 加 `DropResources`(作为 drop site)。新增资源 FQON:Wood/Food/Gold(若 `tutorial_setup.nyan` 已有 TutorialWood 则复用,补 Food/Gold)。`tutorial_setup.nyan` 起始资源=0(M2 靠采集)。`mission_2.nyan`:16×16 + 1 村民 + 1 TC + 若干树/浆果/金矿。
- **测试方式**:启动 mission_2 不报 nyan 错。
- **人工验收**:日志无 error。
- **DoD**:资源点实体 + villager Gather 能力加载。

### T2.2 组件:GatherComponent 补字段
- **修改文件**:`component/api/gather.{h,cpp}`。
- **实现内容**:补 `entity_id_t target_spot`、`enum gather_state {IDLE,WALKING_TO_SPOT,CHOPPING,WALKING_BACK,DROPPING}`、`int64_t carried_amount`、`fqon_t carried_type`、从 ability 读的 `gather_rate`。删除空 `get_type()`-only 桩,改为真实持有。
- **测试方式**:编译。
- **人工验收**:暂无。
- **DoD**:GatherComponent 有真实状态字段。

### T2.3 资源点资源量组件
- **修改文件**:`component/api/resource_storage.{h,cpp}`(复用为资源点存量)。
- **实现内容**:改为持 `fqon_t resource_type` + `int64_t amount` + `get/deplete(amount)`。同时兼作村民 carried 的 buffer(若需区分,加 `bool is_spot` 标志 - 但优先单一用途:只做资源点存量,村民 carried 放 GatherComponent)。**决策:ResourceStorage 专做资源点存量;村民 carried 在 GatherComponent。**
- **测试方式**:编译。
- **人工验收**:暂无。
- **DoD**:资源点实体有可减的存量。

### T2.4 EconomySystem
- **修改文件**:新建 `system/economy.{h,cpp}` + CMakeLists。
- **实现内容**:`tick(time,state)`:对有 GatherComponent 且队首 GatherCommand 的村民,按 gather_state 驱动:IDLE->WALKING_TO_SPOT(`Move::move_default` 到 target);到达->CHOPPING(`now>=next_chop` 时 carried+=rate*delta,spot.deplete(rate),更新 next_chop);carried>=cap 或 spot 空->WALKING_BACK(到 TC);到达->DROPPING(`player->add_resource(carried_type,carried)`,carried=0)->IDLE。定位 TC:遍历 owner 实体找带 DropResources 能力最近者。
- **测试方式**:端到端 T2.6。
- **人工验收**:暂无。
- **DoD**:采集状态机可驱动。

### T2.5 GameplayCommand:GatherCommand + 输入绑定
- **修改文件**:`commands/gather.{h,cpp}`、`send_command.cpp`(GATHER case)、`controller.cpp`(右键点资源点实体发 GatherCommand)。
- **实现内容**:`GatherCommand(entity_id_t target_spot)`。右键点击判定:点中带 Harvestable 的实体->Gather;点中敌方实体->Attack;空地->Move。
- **测试方式**:右键树,日志见 GatherCommand。
- **人工验收**:同上。
- **DoD**:采集命令链路通。

### T2.6 GameplaySystems 接入 EconomySystem + M2 胜利
- **修改文件**:`simulation.cpp`(GameplaySystems::tick 加 economy)、`mission_2.nyan`、`trigger.cpp`(M2 factory)。
- **实现内容**:tick 加 `economy_system->tick`。M2 trigger 谓词:Wood≥50 && Food≥50 && Gold≥50(读 player resources)。M2 MissionDirector:无脚本敌军(纯经济关)。
- **测试方式**:端到端。
- **人工验收**:1) 选村民右键树 -> 走去 -> 砍 -> 返 TC -> 卸货,资源栏 wood +N。2) 同样采浆果(食)、金矿(金)。3) 三种各达 50 显示完成。
- **DoD**:**玩家无控制台/改资源完成 Mission 2 采集闭环并通关。**(spec §9.2 M2)

---

## Phase 3 - Mission 3:建造、人口、训练

**目标**:建房屋提人口、建兵营、训练民兵、TC 训练村民。引入 `ConstructionSystem` + `ProductionSystem`。

### T3.1 数据:建筑 + Create/Constructable 能力
- **修改文件**:`tutorial_data.nyan`、`mission_3.nyan`。
- **实现内容**:新增 `TutorialHouse`(Constructable+Foundation, cost wood 25, ProvideContingent +5 pop)、`TutorialBarracks`(Constructable+Foundation, cost wood 50, Create militia)。给 `TutorialTownCenter` 加 `Create villager`(cost food 50)。`TutorialVillager` 加 Build 相关(若 schema 需 villager 端能力)。新增 `TutorialMilitia` 实体(Idle/Move/Attack,HP/damage)。`mission_3.nyan`:2 村民 + TC,起始 wood 200。
- **测试方式**:启动不报错。
- **人工验收**:日志无 error。
- **DoD**:建筑 + 训练目标实体定义就绪。

### T3.2 组件:BuildComponent + TrainComponent + ProductionComponent 补字段
- **修改文件**:`component/api/{build,train,production}.{h,cpp}`。
- **实现内容**:
  - Build:补 `curve::Continuous<float> progress` + `bool is_foundation` + `fqon_t result_fqon` + `int builders`(当前建造村民数)+ 从 Constructable ability 读 `build_time`/`cost`。
  - Train:补 `fqon_t active_fqon`(单 slot)+ `time::time_t finish_time` + `bool training`。
  - Production:从 `Create`/`ProductionQueue` ability 解析可生产 CreatableGameEntity 的 FQON 列表(只读)。
- **测试方式**:编译。
- **人工验收**:暂无。
- **DoD**:三个组件有真实字段。

### T3.3 人口上限
- **修改文件**:`player.{h,cpp}` 或 `game_state`。
- **实现内容**:Player 加 `int64_t pop_used` / `pop_cap`。`pop_cap` = 初始(如 5)+ sum(owner 的 ProvideContingent 实体 amount)。`pop_used` = owner 活体村民/军事单位数。ProductionSystem 训练前检查 `pop_used < pop_cap`。
- **测试方式**:训练时 pop 变化。
- **人工验收**:建房屋后 pop_cap +5。
- **DoD**:人口计数与上限生效。

### T3.4 ConstructionSystem
- **修改文件**:新建 `system/construction.{h,cpp}` + CMakeLists。
- **实现内容**:`tick`:对有 BuildComponent(foundation)的实体:有建造村民(在 range 内且队首 BuildCommand 指向本 foundation)-> `now>=next_progress` 推进 progress;progress>=1.0 -> 销毁 foundation,`EntityFactory::add_game_entity(loop,state,owner,result_fqon)` 生成完整建筑(复用 spawn 范式,设 position)。BuildCommand 入队时校验 + `player->spend(cost)`。
- **测试方式**:端到端 T3.7。
- **人工验收**:暂无。
- **DoD**:建造进度推进 + 完成转实体。

### T3.5 ProductionSystem
- **修改文件**:新建 `system/production.{h,cpp}` + CMakeLists。
- **实现内容**:`tick`:对有 TrainComponent 且队首 TrainCommand 的建筑:`player->spend(cost)`(失败不排)-> 设 active_fqon+finish_time;`now>=finish_time` -> `EntityFactory::add_game_entity` spawn 新单位(传 active_fqon,owner=建筑 owner,出现在建筑旁)。**修 SpawnEntityHandler 硬编码 test 列表**:新增按指定 FQON 生成路径(或 ProductionSystem 直接调 add_game_entity 不经 SpawnEntityHandler)。
- **测试方式**:端到端 T3.7。
- **人工验收**:暂无。
- **DoD**:训练计时 + 产出单位。

### T3.6 GameplayCommand:BuildCommand + TrainCommand + 输入绑定
- **修改文件**:`commands/{build,train}.{h,cpp}`、`send_command.cpp`(BUILD/TRAIN case)、`controller.cpp`(B 键进入"放置建筑"模式 -> LMB 放置发 BuildCommand;T 键对选中建筑发 TrainCommand 弹出可训练列表 - 简化为固定训练 villager/militia)。
- **实现内容**:`BuildCommand(fqon_t building, coord::phys3 pos)`、`TrainCommand(fqon_t creatable)`。B 模式:按 B 显示建筑放置光标(简化为按 B 直接在选中村民旁建房屋,或用鼠标位置)。
- **测试方式**:按 B 建造,按 T 训练,日志见命令。
- **人工验收**:同上。
- **DoD**:建造/训练命令链路通。

### T3.7 接入 + M3 胜利
- **修改文件**:`simulation.cpp`(tick 加 construction+production)、`mission_3.nyan`、`trigger.cpp`(M3 factory)。
- **实现内容**:M3 trigger 谓词:owner==0 的 Militia 实体数≥2 && owner==0 的 House 实体数≥1。M3 director:无脚本敌军。
- **测试方式**:端到端。
- **人工验收**:1) 建房屋 -> 进度条 -> 完成转实体 -> pop_cap +5。2) 建兵营 -> 训练民兵(扣资源+计时+产出)。3) TC 训练村民。4) ≥2 民兵 + ≥1 房屋显示完成。
- **DoD**:**玩家无控制台/改资源完成 Mission 3 建造+训练闭环并通关。**(spec §9.2 M3)

---

## Phase 4 - Mission 4:时代升级 + 科技 + 脚本化敌军

**目标**:研究时代升级 + 1 项科技,抵御 2 波脚本敌军进攻。引入 `ResearchSystem`(轻量)+ MissionDirector 敌军脚本。

### T4.1 数据:科技 + 时代升级
- **修改文件**:`tutorial_data.nyan`、`mission_4.nyan`。
- **实现内容**:新增 `AgeUpgrade`(ResearchableTech,在 TC 研究,解锁 Militia->升级形态 via PassiveTransformTo)。新增 1 项攻击科技(patch:Attack.damage+2)或护甲科技。`mission_4.nyan`:玩家小基地 + 起始资源,敌方刷兵点(远处)。
- **测试方式**:启动不报错。
- **人工验收**:日志无 error。
- **DoD**:科技 + 时代升级数据就绪。

### T4.2 ResearchSystem(轻量,patch apply)
- **修改文件**:新建 `system/research.{h,cpp}` 或并入 ProductionSystem 的研究分支。
- **实现内容**:`tick`:对有 Research 能力且队首 ResearchCommand 的建筑:`player->spend` -> 计时 -> 完成时在 owner player view 上 `new_transaction->add(patch)->commit`(复用 `entity_factory.cpp:172-194` 模式)。时代升级完成 -> 触发 PassiveTransformTo(把已有/后续民兵升级 - 简化为只影响后续产出)。**不实时刷新已有实体数值**(技术债,spec §10.1)。
- **测试方式**:研究完成日志见 patch commit。
- **人工验收**:研究后训练的新单位数值变化(用 Live/Attack getter 验证)。
- **DoD**:研究 -> patch apply 生效(对新单位)。

### T4.3 GameplayCommand:ResearchCommand
- **修改文件**:`commands/research.{h,cpp}`(新增 - command_t 当前无 RESEARCH 枚举,**需加**)、`send_command.cpp`、`controller.cpp`(R 键研究)。
- **实现内容**:`ResearchCommand(fqon_t tech_fqon)`。
- **测试方式**:按 R 研究日志见命令。
- **人工验收**:同上。
- **DoD**:研究命令链路通。

### T4.4 MissionDirector 脚本化敌军
- **修改文件**:`mission_director.{h,cpp}`(加脚本引擎)、`mission_4.nyan`、`trigger.cpp`(M4 factory)。
- **实现内容**:director 持时间表 `{t=30s: spawn 2 enemy militia at point A, AttackCommand(player TC)},{t=60s: spawn 3 enemy militia at point B, AttackCommand(player nearest)}`。tick 检查时间表 spawn + 发命令。敌军 = MissionDirector spawn 的 owner=1 实体。M4 谜词:AgeUpgrade 已研究 && 2 波敌军全被击退(敌军 spawn 后全死)。
- **测试方式**:端到端。
- **人工验收**:1) 研究时代升级(计时+完成)。2) 研究攻击科技(新民兵伤害+2 可验证)。3) 30s 敌军出现进攻。4) 击退。5) 60s 第二波。6) 击退后完成。
- **DoD**:**玩家无控制台/改资源完成 Mission 4 并通关。**(spec §9.2 M4)

---

## Phase 5 - Mission 5:完整胜利闭环

**目标**:采集->建造->产兵->推进->摧毁敌方 TC 全链路通。

### T5.1 数据:mission_5 地图
- **修改文件**:`tutorial_5.nyan`(新建)、`trigger.cpp`(M5 factory)。
- **实现内容**:`mission_5.nyan`:玩家 TC + 资源点 + 起始村民。远处敌方 TC(owner=1)+ 脚本刷兵点。M5 谜词:owner==1 的 TownCenter 实体不存在(被摧毁)。
- **测试方式**:启动。
- **人工验收**:看到双方 TC。
- **DoD**:地图就绪。

### T5.2 MissionDirector M5 脚本
- **修改文件**:`mission_director.{h,cpp}`、`trigger.cpp`。
- **实现内容**:敌方定时刷兵防御;玩家推进摧毁敌方 TC 即胜。失败条件:玩家 TC 被毁。胜负界面触发。
- **测试方式**:端到端。
- **人工验收**:采集 -> 建兵营 -> 产兵 -> 推进 -> 摧毁敌方 TC -> 胜利。
- **DoD**:**玩家无控制台/改资源完成 Mission 5 完整闭环并通关。**(spec §9.2 M5)

### T5.3 回归验证
- **修改文件**:无。
- **实现内容**:依次跑 mission_1..5,确认机制无回归(移动/攻击/采集/建造/训练/科技/脚本敌军全可用)。
- **测试方式**:5 关全通。
- **人工验收**:5 关全通。
- **DoD**:5 关全可玩可通关。

---

## Phase 6 - 最小 GUI

**目标**:资源栏、人口、选中单位信息、基础操作按钮、任务提示、暂停、重开、胜负界面。**不依赖 de2_base 图**,用 QtQuick 原语。

### T6.1 GUI backend(QObject)
- **修改文件**:新建 `libopenage/gamestate/mission/game_view.{h,cpp}`(QObject,经现有 qmlRegister 桥暴露)、注册到 QML engine(仿 `gui_live_reloader.cpp:14` 的 `qmlRegisterUncreatableType` 或 set_context_property)。
- **实现内容**:`GameView` 暴露 Q_PROPERTY:wood/food/gold、pop_used/pop_cap、selected_entity_name/selected_entity_hp、mission_hint、mission_state(running/won/lost)。由 `MissionDirector`/`GameplaySystems` tick 时更新(或 QML 定时拉)。signal 通知变化。
- **测试方式**:QML 能读到属性。
- **人工验收**:qml console 打印属性。
- **DoD**:C++ 状态可被 QML 访问。

### T6.2 重写 IngameHud.qml(最小)
- **修改文件**:`assets/qml/IngameHud.qml`(重写)、`main.qml`(确认挂载)。
- **实现内容**:用 QtQuick Rectangle/Text 画:顶部资源栏(木/食/金 数字)、人口(x/上限)、左侧选中信息(名/HP 条)、底部操作按钮提示(G/B/T/A/R 说明)、中央任务提示文本(hint)、暂停(P/Esc)、重开按钮、胜负覆盖层。**移除 de2_base SLP 图引用**(`srcPrefix`/`hudImageSource` 改为纯绘制)。
- **测试方式**:启动 tutorial,HUD 显示。
- **人工验收**:HUD 各元素可见且随状态更新。
- **DoD**:完整 HUD 不依赖 de2_base。

### T6.3 暂停/重开/胜负
- **修改文件**:`GameView` + `controller.cpp` + `simulation.cpp`。
- **实现内容**:暂停=停 `running` 或冻结 time_loop;重开=重新 load_scenario;胜负界面=GameView mission_state 触发 QML 覆盖层 + 重开按钮。
- **测试方式**:按键暂停/重开。
- **人工验收**:暂停冻结、重开重置、胜负界面显示。
- **DoD**:暂停/重开/胜负可用。

> Phase 6 完成定义(spec §9.3):完整 GUI 不依赖 de2_base,玩家全程用 HUD + 键鼠操作,无需控制台。

---

## Phase 7 - 打包

**目标**:产出一个可分发的可玩包。

### T7.1 单一启动入口
- **修改文件**:`openage/main/main.py`、`libopenage/main.cpp`。
- **实现内容**:`bin/run --tutorial` 默认进 mission 选择(或默认 mission_1);mission 间自动衔接(通关后下一关)。资产打包进 `assets/converted/engine/util/`(已自带)。
- **测试方式**:`bin/run --tutorial` 一条命令进游戏。
- **人工验收**:从 mission_1 一路打到 mission_5。
- **DoD**:单命令全流程可玩。

### T7.2 README + 验收走查
- **修改文件**:`docs/mvp/README.md`(新建)。
- **实现内容**:写明启动方式、操作键位、5 关流程、已知技术债(spec §10)。执行 spec §9 全量验收。
- **测试方式**:按 README 走查。
- **人工验收**:5 关全通 + GUI 完整。
- **DoD**:**5 关全可玩可通关,单机 AoE 风格 Demo 达成。**

---

## 第一阶段最小任务清单(Phase 0 - 立即可执行)

> 这是现在就该开始的第一批任务。完成后(Phase 0 DoD)再进 Phase 1。每任务都足够小,可独立提交。

| # | 任务 | 关键命令/文件 | 完成判据 |
|---|---|---|---|
| 0.1 | 建 `mvp/tutorial-demo` 分支,删 `plan-tutorial-lvl.md` transcript | `git checkout -b mvp/tutorial-demo`;`rm plan-tutorial-lvl.md` | 分支在 62051247 之上,工作区可识别 |
| 0.2 | `ninja -C bin -j$(nproc)` 编译,修所有错误(以实际输出为准) | 预计涉 `game.cpp` load_scenario 的 nyan 类型访问、`simulation.cpp`/`trigger.cpp` | ninja 退出 0,`bin/run` 生成 |
| 0.3 | `bin/run --tutorial --no-prompts` 启动验证 | 观察日志 `load_scenario size=16x16`/`initial_entities=2`/`Game simulation started` | 画面显示 16×16 草地 + 1 村民 + 1 TC |
| 0.4 | 验证现有 MOVE:右键村民能移动;Ctrl+LMB 能生成 test 实体 | 鼠标操作 | 移动可用,无崩溃 |
| 0.5 | 记录启动期任何异常/警告作为 Phase 1 输入 | 日志归档 | 异常清单产出 |

**Phase 0 整体 DoD**:**玩家可启动 tutorial 看到画面并移动村民,无需控制台/改资源。** 完成后立即进入 Phase 1(T1.1 起)。

---

## 风险提示(实现时留意)

1. **load_scenario 的 nyan 字段访问**(game.cpp:239-298):核查中 `TerrainChunkLayout.tiles` 的 schema 与代码访问方式(`get_set` vs `get_orderedset`、tile 元素是 Object 还是 fqon)需在 T0.2 实际对齐 - 这是 stopgap 最可能崩的点。
2. **CommandQueue 消费冲突**:activity graph 的 `next_command_move` 条件必须收窄(否则非 MOVE 命令被当 MOVE 处理)- 在 T1.6 加 AttackCommand 时一并改 `activity/condition/next_command.cpp`。
3. **SpawnEntityHandler 硬编码 test 列表**(spawn_entity.cpp:187-192):ProductionSystem 需走 FQON 参数化生成,不复用其轮询。
4. **Live 回调重入**:HP≤0 回调在 `set_attribute` 路径触发,销毁(`remove_game_entity`)应在 CombatSystem tick 主路径执行,回调只置 dead 标志。
5. **GUI 不依赖 de2_base**:`IngameHud.qml` 现有 SLP 引用必须移除(tutorial 自包含无这些图)。
