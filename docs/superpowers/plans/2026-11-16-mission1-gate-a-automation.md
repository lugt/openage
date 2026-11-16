# Mission 1 Gate A Automation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 用一条可重复命令驱动真实 Qt 窗口完成 Mission 1 的选择、移动、攻击、死亡、移除、事件驱动胜利和可见胜利 UI，并可靠地区分成功、断言失败、超时和崩溃。

**Architecture:** 保留现有生产 `Presenter -> InputManager -> Controller -> EventLoop -> TriggerRuntime` 链路，在 Presenter 中增加默认关闭的逐帧验收驱动器。驱动器通过相机投影生成 `QMouseEvent`，以只读状态断言推进；QML 复用 `HudQmlAdapter`，截图只作为状态断言的配套证据。

**Tech Stack:** C++20、Qt6 (`QMouseEvent`, `QQuickItem`, QML)、Eigen、Bash、Python 3/pytest、现有 openage simulation demos。

**Spec:** `docs/superpowers/specs/2026-11-15-mission1-gate-a-automation-design.md`；上位验收定义见 `agent-reports/wallace-mission-01/MVP-SPEC-v2.md`。

## Global Constraints

- 工作留在当前 `mvp/william-wallace` 工作区；不得覆盖、回滚或格式化用户已有的未提交改动。
- Gate A 输入必须经过 `QMouseEvent -> InputManager::process()`；禁止直接调用 `set_selected`、`send_command`、`resolve_context_action` 或 `declare_victory` 作为验收输入/胜利证据。
- 胜利必须来自真实 `game.removal -> TriggerRuntime`；headless 35 个 demo 继续作为语义回归，不能替代窗口验收。
- 自动化只在 `OPENAGE_WW_ACCEPTANCE=combat|full` 时启用；普通启动不注入输入、不改变退出行为。
- `combat` 是开发模式，不能声明 Gate A；只有 `full` 同时验证 Q_PROPERTY 和胜利截图后才能声明 `WW_MISSION_1_WINDOWED_INTEGRATION`。
- 不进入 Gate B、converter、player color、完整 DDS、Mission 2–5 或通用 GUI 自动化框架。
- 构建用 `ninja -C bin libopenage`；回归用 `venv/bin/python -m pytest openage/scenario/tests/ -q` 和 `scripts/p4_4d_regression.sh --regression`。
- 未经用户明确要求，不执行 git commit。下列提交步骤只记录建议的原子提交边界，执行前必须另获许可。

## Review Focus

- **进程返回 0 但日志已有 FATAL/未捕获异常：** `run_ww_mission1.sh` 与 staged verifier 必须判失败；Task 1 用零退出码加崩溃日志的测试固定。
- **单击只有 press/release、没有拖拽距离：** 必须选中实体而不是被当成空框选；Task 2 的真实 Qt 选择断言固定。
- **实体在两次点击之间移动：** 每次点击前重新投影当前 Position，禁止复用初始像素；Task 2 的移动后攻击断言固定。
- **高 DPI/窗口尺寸造成逻辑坐标和 framebuffer 像素不一致：** 点击使用 Camera viewport/QMouseEvent 逻辑坐标，截图单独使用 framebuffer 尺寸；Task 2 在点击前断言投影点位于 viewport 内。
- **C++ 已胜利但 QML 未显示：** `full` 模式同时检查 `HudQmlAdapter::mission_is_victory()` 和胜利截图；Task 4/5 固定，不接受 log-only victory。

---

### Task 1: 让验收脚本对崩溃和回归失败返回真实失败

**Files:**
- Create: `scripts/lib/ww_mission1_log_check.sh`
- Create: `scripts/tests/test_ww_mission1_verification.py`
- Modify: `scripts/run_ww_mission1.sh:107-116`
- Modify: `scripts/verify_ww_mission1.sh:351-412`
- Modify: `scripts/p4_4d_regression.sh:105-145`
- Modify: `agent-reports/wallace-mission-01/PROGRESS.md`

**Interfaces:**
- Produces: `ww_mission1_log_has_fatal <log>`，发现 `FATAL: terminate has been called`、`uncaught exception` 或 `SIGSEGV` 时返回 0，否则返回 1。
- Produces: `OPENAGE_RUN_BIN` 测试覆盖入口；缺省仍为 `$REPO/bin/run`，正常用户命令不变。

- [ ] **Step 1: 写脚本行为回归测试**

用临时日志和临时 fake `bin/run` 覆盖：正常日志成功；脚本自己的 `139=SIGSEGV` 帮助文字不算崩溃；FATAL 日志即调用方退出码为 0 也失败；35 个 demo 中任一未打印 PASS 时 regression 脚本退出非零；全 PASS 时退出 0。

- [ ] **Step 2: 验证测试先失败**

Run: `venv/bin/python -m pytest scripts/tests/test_ww_mission1_verification.py -q`  
Expected: FAIL；当前 `run_ww_mission1.sh` 只传播 `PIPESTATUS[0]`，regression 脚本也不汇总退出失败。

- [ ] **Step 3: 实现共享日志检查并传播失败**

两个窗口脚本在分类前调用 `ww_mission1_log_has_fatal`；匹配真实 FATAL/uncaught exception/带终端控制序列的 SIGSEGV，但排除脚本退出码帮助行。命中时将结果归为 `ENGINE_FAIL`，即使进程 rc=0 也不能进入 PASS 分支。`p4_4d_regression.sh` 末尾在 `fail > 0` 时退出 1，并使用 `${OPENAGE_RUN_BIN:-$REPO/bin/run}`。

- [ ] **Step 4: 运行聚焦和既有回归**

Run: `venv/bin/python -m pytest scripts/tests/test_ww_mission1_verification.py -q`  
Expected: 全部 PASS。

Run: `scripts/p4_4d_regression.sh --regression`  
Expected: `PASS=35 FAIL=0`，进程退出 0。

- [ ] **Step 5: 按证据刷新进度台账**

将 M0 标为“测试全绿但 WIP/验收假阳性收尾中”，M1 标为“实体渲染已见、完整三实体证据未完成”，M3/M4 标为“binding/demo 51 已实现，真实 Qt 验收未完成”，M5 保持未开始。不得把 demo 51 写成 Qt 输入通过。

- [ ] **Step 6: 建议的原子提交（需用户另行许可）**

`test: make Mission 1 verification fail on crashes`

### Task 2: 建立真实 Qt 输入的 combat 验收驱动器

**Files:**
- Create: `libopenage/presenter/gate_a_driver.h`
- Create: `libopenage/presenter/gate_a_driver.cpp`
- Create: `scripts/verify_ww_gate_a.sh`
- Modify: `libopenage/presenter/presenter.h`
- Modify: `libopenage/presenter/presenter.cpp:66-112,219-257,514-540`
- Modify: `libopenage/presenter/CMakeLists.txt`
- Modify: `scripts/run_ww_mission1.sh`

**Interfaces:**
- Produces: `GateADriver::Mode { COMBAT, FULL }`；本任务只启用 `COMBAT`，`FULL` 在 Task 4 接入 HUD 后启用。
- Produces: `GateADriver::on_frame()`、`GateADriver::failure() -> std::optional<std::string>`。
- Consumes: 现有 `GameSimulation`、`InputManager`、`Camera`、`SelectionModel`、`Window`；Presenter 提供 `capture_screenshot(native_path) -> bool` 回调。
- Environment: `OPENAGE_WW_ACCEPTANCE=combat`，`OPENAGE_WW_ACCEPTANCE_DIR=<absolute evidence directory>`。

- [ ] **Step 1: 定义逐帧状态机和失败行为**

状态固定为 `WAIT_INITIAL -> SELECT -> WAIT_SELECTED -> MOVE -> WAIT_MOVED -> ATTACK -> WAIT_VICTORY -> DONE`。每步有 wall-clock deadline；`failure()` 非空时 Presenter 在主线程抛出 `error::Error`，不得调用 `exit(0)` 掩盖失败。

- [ ] **Step 2: 先运行不存在的 combat 命令，确认缺口**

Run: `scripts/verify_ww_gate_a.sh --combat`  
Expected: 当前为 command not found/不支持，证明没有可复用 Gate A 驱动器。

- [ ] **Step 3: 实现相机投影和真实 Qt 点击**

每次点击时读取实体当前 Position，以 `projection * view` 转为 NDC，再映射到 Camera viewport；断言 `0 <= x < width`、`0 <= y < height`。随后 `InputManager::set_mouse(x,y)`，依次构造并 process `MouseButtonPress`、`MouseButtonRelease`。不得发送直接 simulation command。

- [ ] **Step 4: 实现只读状态推进与证据**

新增 `Presenter::center_camera_on_scenario()`：首个渲染帧前读取当前全部实体 Position 的均值并 `look_at_coord`，让正常 scenario 启动与验收使用同一初始相机。初始等待 60 帧并截图，证据中必须同时见到三实体；点击 PlayerVillager 后要求 SelectionModel 只含其实体 id；右键空地 `(4,4)` 后要求 Position 距目标不超过 0.25 tile；重新投影村民和敌人当前坐标，右键敌人；等待敌人从 GameState 消失且 `MissionState=VICTORY/reason=enemy_removed`。输出 `WW_ACCEPTANCE_COMBAT: PASS` 仅在这些条件全部成立后。

- [ ] **Step 5: 接入 canonical 启动和自动超时**

`verify_ww_gate_a.sh --combat|--full` 设置环境并调用同一个 `run_ww_mission1.sh`，创建时间戳证据目录，使用 Python stdlib 在 180 秒超时后杀掉整个进程组并返回非零。将现有 frame-60 截图代码提炼为 `capture_screenshot()`，删除重复的临时 hook。

- [ ] **Step 6: 构建并运行 combat 验收**

Run: `ninja -C bin libopenage && scripts/verify_ww_gate_a.sh --combat`  
Expected: 若现有窗口稳定则完整 PASS；否则必须保存首个可重复失败（包括当前 mutex 崩溃），且退出非零。后一种情况进入 Task 3，不得改写断言使其通过。

- [ ] **Step 7: 建议的原子提交（需用户另行许可）**

`test: drive Mission 1 combat through real Qt input`

### Task 3: 用可重复证据定位并修复窗口崩溃

**Files:**
- Create: `docs/mvp/reports/mission1-window-crash.md`
- Create/Modify: 由首个失败回归确定的最小测试文件；优先复用 `libopenage/gamestate/demo/` 生命周期测试或 Task 2 的 combat 驱动器。
- Modify: 仅根因证据指向的生产文件；候选调用链为 `libopenage/gamestate/simulation.cpp:249-315`、`libopenage/event/event_loop.cpp:71-107`、`libopenage/engine/engine.cpp:158-221`，不得预先同时修改三处。

**Interfaces:**
- Consumes: Task 2 的 `scripts/verify_ww_gate_a.sh --combat` 和完整首个失败日志。
- Produces: 一个能在修复前失败、修复后通过的行为回归；生产接口除非根因要求，不新增第二套生命周期。

- [ ] **Step 1: 固定首次失败并做三次同构复现**

保留三份完整日志和退出码；记录失败发生在初始化、选择、移动、攻击还是关闭阶段。使用现有 native backtrace；需要调试器时在 macOS 用 `lldb --batch -o run -o 'thread backtrace all' -- <actual command>`，失败则记录环境限制，不改称“偶发 race”。

- [ ] **Step 2: 将最早错误层转为行为回归**

回归必须断言被违反的线程/生命周期/事件契约，而不是只扫描 `mutex lock failed` 文本。先在未修复代码上运行并确认同一根因失败；若三次窗口运行不再复现，保留 `NOT REPRODUCED`，转而验证已有日志与当前二进制是否一致，不做投机修复。

- [ ] **Step 3: 实施根因处最小修复**

依据对象所有权、锁状态和线程退出顺序选择最小改动；禁止通过吞异常、禁止关闭模拟线程、禁止移除输入或把 FULL 改为 headless。若证据指向 `running/lifecycle` 或对象销毁顺序，测试必须覆盖对应并发/销毁时序。

- [ ] **Step 4: 验证修复和连续稳定性**

Run: 聚焦行为回归，随后 `scripts/verify_ww_gate_a.sh --combat` 连续 3 次。  
Expected: 聚焦回归 PASS；3 次均输出 `WW_ACCEPTANCE_COMBAT: PASS`、退出 0、无 FATAL。

- [ ] **Step 5: 运行强制回归并记录根因**

Run: `venv/bin/python -m pytest openage/scenario/tests/ -q` 和 `scripts/p4_4d_regression.sh --regression`。  
Expected: 103 PASS、35 PASS。报告记录根因、修复前失败、修复后证据及未证明事项。

- [ ] **Step 6: 建议的原子提交（需用户另行许可）**

`fix: stabilize Mission 1 windowed simulation lifecycle`（按实际根因调整标题）

### Task 4: 接通最小 Mission HUD 并完成 full Gate A

**Files:**
- Create: `assets/qml/mission_main.qml`
- Modify: `assets/qml/MissionHud.qml:22-45,87-118`
- Modify: `libopenage/presenter/presenter.h`
- Modify: `libopenage/presenter/presenter.cpp:208-257,399-420`
- Modify: `libopenage/presenter/gate_a_driver.h`
- Modify: `libopenage/presenter/gate_a_driver.cpp`
- Modify: `libopenage/gamestate/demo/hud_adapter_demo.cpp`（仅在需要 pin QML 注册契约时）

**Interfaces:**
- Consumes: 既有 `HudQmlAdapter::mission_is_victory()`、`mission_lifecycle()`、`mission_victory_reason()`。
- Produces: QML singleton `OA.HudAdapter`；其 QObject 生命周期必须长于 QQmlEngine。
- Produces: `OPENAGE_WW_ACCEPTANCE=full`，在 combat 条件之后额外要求 HUD 属性为 VICTORY 并保存 `m5-victory-ui.png`，最后输出 `WW_MISSION_1_WINDOWED_INTEGRATION: PASS`。

- [ ] **Step 1: 先添加 full 的失败 UI 断言并运行**

先在 GateADriver 增加 `FULL -> WAIT_UI`：复用完整 combat 状态机，随后要求读取真实 `HudQmlAdapter` 的 VICTORY 属性；此时尚未接线 adapter，因此测试必须失败。

Run: `scripts/verify_ww_gate_a.sh --full`  
Expected: combat 语义完成后因 `HudQmlAdapter` 未绑定/胜利 UI 不可见而失败；不能回退到只检查日志。

- [ ] **Step 2: 在 QML 加载前注册真实 adapter**

Presenter 在 `init_gui()` 前创建 `HudQmlAdapter`，用 `qmlRegisterSingletonInstance("yay.sfttech.openage", 1, 0, "HudAdapter", adapter.get())` 注册；`setup_selection_viewmodel()` 中调用既有 `attach()`。成员声明保证 adapter 晚于 GUI/QQmlEngine 销毁。

- [ ] **Step 3: 让 scenario 使用 Mission QML，其他模式保持原入口**

scenario runtime 使用 `assets/qml/mission_main.qml`，其根 Item 填满窗口并包含 `MissionHud`。删除 `MissionHud.qml` 内会遮蔽真实 adapter 的 placeholder `QtObject`，改为引用 `OA.HudAdapter`；非 scenario 保留既有 QML root。

- [ ] **Step 4: 扩展 full 驱动的最后一步**

真实移除和 TriggerRuntime 胜利之后，等待 `mission_is_victory()==true`、`mission_lifecycle()=="VICTORY"`、`mission_victory_reason()=="enemy_removed"`，再渲染至少一帧、截图、关闭窗口。超时或属性不符返回非零。

- [ ] **Step 5: 构建并验证 full**

Run: `ninja -C bin libopenage && scripts/verify_ww_gate_a.sh --full`  
Expected: 退出 0，日志含 `WW_MISSION_1_WINDOWED_INTEGRATION: PASS`；证据目录含初始、选择、战斗和 `m5-victory-ui.png`。读取胜利截图，必须实际看到 `VICTORY` 和 objective 状态，否则保持 FAIL。

- [ ] **Step 6: 建议的原子提交（需用户另行许可）**

`presenter: show Mission 1 victory in the production HUD`

### Task 5: 固化 Gate A 证据、回归和人工检查入口

**Files:**
- Create: `docs/mvp/reports/mission1-windowed-acceptance.md`
- Create: `docs/mvp/reports/mission1-windowed-acceptance/`（验收截图与摘要日志）
- Modify: `agent-reports/wallace-mission-01/PROGRESS.md`
- Modify: `scripts/p4_4d_regression.sh`（仅登记新增的稳定行为 demo，不重复运行窗口测试）

**Interfaces:**
- Produces: 用户只需运行 `scripts/run_ww_mission1.sh` 做最终人工检查；开发者回归入口为 `scripts/verify_ww_gate_a.sh --full`。
- Produces: `WW_MISSION_1_WINDOWED_INTEGRATION` 的证据，不声明 `WW_MISSION_1_CONTENT` 或完整 MVP。

- [ ] **Step 1: 运行完整强制矩阵**

Run: `venv/bin/python -m pytest scripts/tests/test_ww_mission1_verification.py openage/scenario/tests/ -q`、`scripts/p4_4d_regression.sh --regression`、`scripts/verify_ww_gate_a.sh --full`。  
Expected: 脚本行为测试和 103 个 scenario 测试全过；35 个 demo 全过；full Gate A 退出 0。

- [ ] **Step 2: 检查证据而非只看退出码**

核对同一次连续运行包含选择、移动、ATTACK、真实 removal、`reason=enemy_removed`、HUD VICTORY 和 clean shutdown；读取全部关键截图，确认初始场景、选择反馈和胜利 UI。缺任何一项则不签署。

- [ ] **Step 3: 写验收报告并更新里程碑**

报告记录 commit、dirty tree、精确命令、退出码、日志/截图路径、平台 GL 信息和未证明事项。M1/M3/M4/M5/M6 只能在证据齐全后更新；M2 若未进入则写 `SKIPPED — visibility did not require render repair`，M7/M8 保持未开始。

- [ ] **Step 4: 给用户一次短人工验收**

提供 `scripts/run_ww_mission1.sh`，只检查：看见三实体、单击选中、右键移动、右键敌人自动攻击、敌人消失、VICTORY 面板、干净关闭。自动测试已覆盖的项目不让用户重复多轮；记录用户结论后才最终签署 Gate A。

- [ ] **Step 5: 建议的原子提交（需用户另行许可）**

`docs: record Mission 1 windowed integration acceptance`
