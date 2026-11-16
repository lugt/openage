# Mission 1 Gate A 自动化验收设计

日期：2026-11-15  
分支：`mvp/william-wallace`  
上位文档：`agent-reports/wallace-mission-01/MVP-SPEC-v2.md`、`IMPLEMENTATION-PLAN-v2.md`

## 目的

停止把 macOS 输入助手作为日常开发回归手段，用一个命令重复验证真实窗口中的选择、移动、攻击、死亡、移除、事件驱动胜利和玩家可见 UI。开发者只在自动化失败后定位问题，用户只在 Gate A 收尾时做一次人工体验验收。

本设计只补充 v2 计划的测试执行方式，不改变 Mission 1 的产品范围或完成定义。

## 当前证据

- `venv/bin/python -m pytest openage/scenario/tests/ -q`：103/103 PASS。
- `scripts/p4_4d_regression.sh --regression`：35/35 PASS。
- demo 51 已覆盖生产 `BindingContext` 的 MOVE/ATTACK 分流，但没有经过 `QMouseEvent -> InputManager::process()`，不能作为 M3/M4 的完整证据。
- 真实截图已证明村民和 TownCenter 可以渲染；不能据此宣布完整 M1 或 Gate A 通过。
- 当前窗口仍加载 `assets/test/qml/main.qml`，胜利 UI 未接通。
- `logs/ww_mission1_window_session.log` 出现 `mutex lock failed: Invalid argument`，之后脚本却报告退出码 0；崩溃和验收假阳性都必须处理。

## 目标

1. 默认关闭、显式启用的进程内 Gate A 驱动器，通过真实 `QMouseEvent` 和 `InputManager::process()` 驱动生产输入路径。
2. 根据相机投影计算目标屏幕坐标，不依赖固定截图像素、Accessibility 权限或窗口焦点。
3. 自动断言选择、MOVE、位置变化、ATTACK、伤害、死亡、真实移除、TriggerRuntime 胜利和绑定的 UI 状态。
4. 自动保存日志与关键截图；超时、断言失败、崩溃和非正常退出必须返回失败。
5. 一个 canonical 命令完成 Gate A，并生成可供最终人工验收复核的证据。

## 非目标与反目标

- 不重写已通过的 combat/victory headless 测试，不绕过生产输入路径。
- 不直接调用 `Controller::set_selected()`、`Commander::send_command()`、`resolve_context_action()` 或 `declare_victory()` 作为 Gate A 输入/胜利证据。
- 不开发新的通用 GUI 自动化框架，不扩大 macOS 输入助手的功能。
- 不在本阶段处理 Gate B converter、完整 DDS、全部 sprite、player color 或 Mission 2–5。
- 不用截图单独证明语义，不用日志单独证明玩家可见 UI，不降低断言或忽略异常。

## 方案比较

### A. 继续操作系统级输入助手

优点是最接近外部用户操作；缺点是权限、焦点、坐标和时序脆弱，而且难以可靠读取内部语义。保留为最后一次可选跨进程冒烟，不作为日常回归。

### B. 进程内真实 Qt 输入驱动（采用）

保留真实窗口、渲染、Qt 事件、生产 binding、模拟和 TriggerRuntime，同时在进程内读取断言所需状态。它消除了操作系统自动化的不稳定因素，且符合 v2 Spec 对 `QMouseEvent -> InputManager::process()` 的要求。

### C. 只做用户人工验收

可以判断体验，但不能提供快速、频繁、可重复的回归。仅在自动 Gate A 全绿后执行一次简短检查。

## 实现结构

1. **验收驱动器**：由专用环境变量开启，正常游戏路径完全惰性。使用 Qt 事件/定时器推进步骤，并设置硬性超时；输入使用真实 `QMouseEvent`，鼠标位置同步到 `InputManager`。
2. **坐标与状态适配**：通过现有相机和实体 Position 计算点击位置；只读访问 SelectionModel、GameState、MissionState 和 HUD adapter。测试不得替代产品行为。
3. **证据采集**：在初始场景、选择后、攻击/移除后、胜利 UI 后截图。截图配合状态断言和日志，不单独构成通过条件。
4. **可信退出**：canonical 脚本传播引擎真实退出状态，扫描未捕获异常/FATAL，并区分断言失败、超时、崩溃和正常完成。人工关闭窗口与自动验收退出分开处理。
5. **当前崩溃**：先用新驱动稳定复现，按最早错误层定位 mutex 崩溃；没有证据前不预设修复方案。
6. **胜利 UI**：沿用既有 `HudQmlAdapter` 信号和属性，只做最小 QML context/root 接线，避免另建 MissionState 轮询。

## 执行顺序

1. 修正验收脚本假阳性，更新过时的进度记录并整理现有未提交改动的归属。
2. 建立 Qt 输入驱动器和失败即非零的 Gate A 命令。
3. 复现并修复当前窗口崩溃，完成 M3/M4。
4. 接通最小 Mission HUD，完成 M5。
5. 连续运行完整 Gate A，保留证据；用户做一次最终人工验收。
6. Gate A 签署后再进入 Gate B 最小内容工作。

## 完成定义

- v2 Spec 的 FR-1 至 FR-9 在一次连续真实窗口运行中全部满足。
- 新 Qt 输入测试及既有 35 个 demo、103 个 scenario 测试全部通过。
- 验收命令在成功时返回 0，在崩溃、异常、超时或断言失败时返回非零。
- 形成 `docs/mvp/reports/mission1-windowed-acceptance.md` 及日志、截图证据。
- 只声明 `WW_MISSION_1_WINDOWED_INTEGRATION`，不把 fixture-based Gate A 误称为 converter-backed Gate B 或完整 MVP。

## 留给实现者的决定

驱动器放在 Presenter 的哪个最小扩展点、状态等待的具体 Qt 回调形式，以及崩溃的实际根因和修复，均由代码勘察与失败测试决定。不得为迎合预设实现而绕过生产路径。
