;; This buffer is for text that is not saved, and for Lisp evaluation.
;; To create a file, visit it with ‘C-x C-f’ and enter text in its buffer.

了解,我看到 Python 包调研 agent 被停了(killed)。没关系——前三个 agent 的结果已经非常完整,而且第一个调研(顶层架构)已经覆盖了 Python 层的核心。我刚才自己手动补的 convert 目录结构也足够了。

  我现在有足够的信息来综合输出完整的架构说明。让我把所有调研整合成一篇。

  ---
  openage 架构总览
  
  openage 是一个开源的帝国时代引擎重制项目,用 C++20 + Python + Cython 三层构建,数据用 nyan DSL 描述,渲染基于 Qt + OpenGL。

  一、三层技术栈与职责

  ┌─────────────────────────────────────────────────────────┐
  │  Python 层  (openage/)                                  │
  │  职责:入口/argparse、资产转换(convert)、Cython 胶水编排 │
  ├─────────────────────────────────────────────────────────┤
  │  Cython 桥层  (*.pyx / .pxd)                            │
  │  职责:唯一的 Python↔C++ 通道;性能热点媒体解码         │
  ├─────────────────────────────────────────────────────────┤
  │  C++ 层  (libopenage/)                                  │
  │  职责:引擎核心、仿真、渲染、事件系统、坐标、寻路       │
  ├─────────────────────────────────────────────────────────┤
  │  外部库  nyan (数据) + Qt (窗口/GL/UI/输入) + OpenGL   │
  └─────────────────────────────────────────────────────────┘

  关键认知:C++ 层是引擎本体,Python 层主要做"把原版游戏资产转成 openage 格式"这件离线的事;运行时几乎全在 C++ 里。Python 和 C++ 之间只通过 Cython 桥接,不是 pybind11。

  ---
  二、启动链路(从命令行到一帧)

  bin/run (带嵌入式 Python 解释器的 C++ 可执行)
    └─ openage/__main__.py::main()          # argparse,注册 main/convert/test 等子命令
         └─ openage/main/main.py::main()    # 启动准备
              ├─ cpp_interface_setup(args)   # 注册所有 PyFunc 跨语言回调
              ├─ Union().root 挂载 VFS       # 把 assets/ 和 cfg/ mount 进去
              ├─ 按需触发 convert 转换资产
              └─ main_cpp.run_game(args, root)   # .pyx 跨界
                   └─ libopenage::run_game(args_cpp)   # C++ 总入口
                        ├─ CVarManager.load_all()    # 读 cfg/*.oac
                        └─ Engine::loop()            # 三线程主循环
                             ├─ 线程1: TimeLoop→Clock       (推进仿真时间)
                             ├─ 线程2: GameSimulation→EventLoop  (仿真)
                             └─ 主线程: Presenter→Renderer  (Qt/OpenGL)

  - bin/run 和 python3 -m openage 等价,前者仅为 sanitizer 等动态分析存在
  - Qt 要求 QGuiApplication 必须在主线程,所以仿真被挪到 worker 线程,渲染留在主线程
  - HEADLESS 模式无 Presenter,仿真直接在主线程跑

  ---
  三、Python 与 C++ 的边界(Cython 桥)
  
  这是理解整个项目最关键的一把钥匙。

  两侧的对接层

  ┌───────────┬─────────────────────────┬──────────────────────────────────────────────────────────────┐
  │    侧     │          目录           │                             职责                             │
  ├───────────┼─────────────────────────┼──────────────────────────────────────────────────────────────┤
  │ C++ 侧    │ libopenage/pyinterface/ │ PyObj(持有 PyObject*)、异常双向翻译、PyFunc 注册与初始化校验 │
  ├───────────┼─────────────────────────┼──────────────────────────────────────────────────────────────┤
  │ Python 侧 │ openage/cppinterface/   │ 对应的 .pyx 胶水,setup.py 编排所有子接口初始化               │
  └───────────┴─────────────────────────┴──────────────────────────────────────────────────────────────┘

  声明机制(很特别)

  C++ 头文件里用 // pxd: 注释块声明 Cython 可见的类和函数签名(见 libopenage/main.h),buildsystem/pxdgen.py 据此自动生成 .pxd 文件。这是理解任何跨界调用的入口——改 C++ 接口时,记得同步改 // pxd: 注释。

  Cython 的三类用途

  1. 跨界调用桥梁:main_cpp.pyx(Python args→C++ main_arguments)、versions.pyx、cvar.pyx、log_cpp.pyx、fslike/cpp.pyx、filelike/cpp.pyx
  2. 性能热点:转换器里逐像素/逐帧的媒体解码——slp.pyx、smp.pyx、smx.pyx、sld.pyx、cabextract/lzxd.pyx、PNG/Opus 编码
  3. 嵌入式入口:run.py 编译成带解释器的 bin/run

  ---
  四、nyan:数据描述语言

  nyan 是什么

  nyan 是 openage 的外部数据描述库(独立项目,bin/nyan-external/),提供面向对象、支持继承/多态/补丁(patch)的数据 DSL。openage 用它定义游戏对象(单位、建筑、科技、能力)的属性。

  关键认知:没有 libopenage/nyan/ 目录——C++ 直接 #include <nyan/nyan.h> 用外部库。而 openage/nyan/(Python)只是转换器用的"哑"存储格式,不参与运行时查询。

  nyan DSL 长什么样

  # assets/converted/engine/ability.nyan
  !version 0.2.0
  import engine.ability.property
  import engine.root as root

  Ability(root.Object):
      properties : dict(abstract(...AbilityProperty), AbilityProperty) = {}

  对象用 FQON(fully-qualified object name,点分路径,如 engine.ability.Ability)引用。

  nyan 数据流(转换 vs 运行时,两条独立路径)

  离线转换:
    原版游戏文件(.dat/.slp/.wav…)
      └─ openage/convert/ (Python)
           ├─ nyan_subprocessor.py 构建 NyanObject
           ├─ openage/nyan/nyan_structs.py (哑存储容器)
           ├─ import_tree.py 解析引用、生成 import 语句
           └─ 导出 .nyan 文本 → assets/converted/<modpack>/*.nyan + modpack.toml

  运行时:
    Game::load_data() 遍历 modpack includes
      └─ nyan::Database::load(file, fileload_func)   # C++ 直接调外部 nyan 库
           └─ nyan::Database → GameState.get_db_view() → nyan::View
                └─ EntityFactory 读取 nyan::Object
                     └─ 生成 GameEntity / Terrain / Player 等运行时对象

  资产与 nyan 的关联:assets/converted/engine/util/graphics.nyan 用 file 类型字段引用贴图路径(如 Animation 的 sprite : file),单位通过 ability 引用 Animation,Animation 引用贴图文件。

  ---

  五、C++ 引擎核心(libopenage)

  三大核心模块分工

  Engine (总编排器,起多线程)
   ├─ 持有: TimeLoop / CVarManager / GameSimulation / Presenter
   ├─ 不持有 Renderer —— Renderer 由 Presenter 间接拥有
   │
   ├── Presenter (高层,懂游戏语义)
   │    ├─ 持有: Window / Renderer / Camera / 5个RenderStage / GUI / InputManager
   │    └─ 运行 Qt 主循环 + 驱动渲染
   │
   └── Renderer (低层,只懂图形对象)
        ├─ 抽象: Renderer/Window/RenderPass/Texture/ShaderProgram/Camera
        └─ 后端: opengl/ (完整) + vulkan/ (仅 stub)

  严格单向依赖:Engine→Presenter→Renderer,反向不成立。

  三线程主循环(Engine::loop())

  ┌─────────────┬────────────────────────────────────────────────────────────┬────────────────────────────────────────────────┐
  │    线程     │                           跑什么                           │                      关键                      │
  ├─────────────┼────────────────────────────────────────────────────────────┼────────────────────────────────────────────────┤
  │ 线程1       │ TimeLoop::run() 推进 Clock 仿真时间                        │ 独立                                           │
  ├─────────────┼────────────────────────────────────────────────────────────┼────────────────────────────────────────────────┤
  │ 线程2(仿真) │ GameSimulation::run() → EventLoop::reach_time(time, state) │ 不直接调 renderer,通过 RenderFactory push 更新 │
  ├─────────────┼────────────────────────────────────────────────────────────┼────────────────────────────────────────────────┤
  │ 主线程      │ Presenter::run() Qt 事件循环 + 渲染                        │ Qt 硬性要求主线程                              │
  └─────────────┴────────────────────────────────────────────────────────────┴────────────────────────────────────────────────┘

  gamestate + event + curve:数据驱动的"曲线数据库"

  这是 openage 最核心的设计——事件驱动的曲线数据库:

  - curve:所有可变状态都存在时间序列化的 curve 里(位置、角度、所有权、命令队列、activity 节点……)。curve 不是动画曲线,而是"变量随时间变化的完整历史",任意时刻可查询
    - Continuous<T> 禁止跳变(位置用)、Segmented<T> 允许跳变(角度用)、Discrete<T> 离散状态(所有权/活动节点)、DiscreteMod<T> 循环(动画)、Queue<T> 时间线队列(命令队列)
    - 内部是 KeyframeContainer<T>(按时间排序的 vector,首 keyframe 在 -INF 作哨兵)
  - event:时间排序的事件队列(用 PairingHeap),每个事件关联一个 EventEntity 目标和 EventHandler
  - 触发:curve 修改时通过 EventEntity::changes() 反向通知所有依赖此 curve 的事件,使其重新调度——这就是"数据驱动"

  数据流:事件 invoke → handler 修改 curve → curve::changes() 反向通知依赖 → 重新调度。

  标准数据流(spawn→移动为例)

  SpawnEntityHandler::invoke → factory.add_game_entity → 创建实体
    → entity_pos.set_position(time, pos) 写位置曲线
    → activity.init(time) 设置初始 activity 节点
    → manager.run_activity_system(time) → Activity::advance 走状态机
         → TASK_SYSTEM 节点(MOVE) → Move::move_command → move_default
              → pathfinder.get_path 拿 waypoints
              → 向 Continuous<phys3> 写一串位置 keyframe
              → 向 Segmented<phys_angle_t> 写角度 keyframe
    → 曲线被改 → changes() 触发依赖此实体的事件重新调度

  坐标系统(coord/)

  强类型,CRTP 模板编译期阻止"绝对+绝对"等无意义运算,全部由 coord.h.template 生成:

  ┌───────────────────────┬────────────────────────┬─────────────────────────────────────────────────┐
  │        坐标系         │          标量          │                      用途                       │
  ├───────────────────────┼────────────────────────┼─────────────────────────────────────────────────┤
  │ phys3                 │ FixedPoint<int64_t,16> │ 游戏世界物理坐标(亚瓦片精度)                    │
  ├───────────────────────┼────────────────────────┼─────────────────────────────────────────────────┤
  │ tile/tile3            │ int64_t                │ 瓦片整数网格(逻辑)                              │
  ├───────────────────────┼────────────────────────┼─────────────────────────────────────────────────┤
  │ chunk/chunk3          │ int32_t                │ 区块(16 tile 一格,分块加载)                     │
  ├───────────────────────┼────────────────────────┼─────────────────────────────────────────────────┤
  │ scene2/3              │ FixedPoint<int64_t,16> │ 渲染中间坐标,to_world_space()→Eigen 喂 GPU      │
  ├───────────────────────┼────────────────────────┼─────────────────────────────────────────────────┤
  │ camhud/viewport/input │ int32_t                │ 屏幕/相机像素(input 经 Camera raycast 转 phys3) │
  └───────────────────────┴────────────────────────┴─────────────────────────────────────────────────┘
  C++ 头文件里用 // pxd: 注释块声明 Cython 可见的类和函数签名(见 libopenage/main.h),buildsystem/pxdgen.py 据此自动生成 .pxd 文件。这是理解任何跨界调用的入口——改 C++ 接口时,记得同步改 // pxd: 注释。

  Cython 的三类用途

  1. 跨界调用桥梁:main_cpp.pyx(Python args→C++ main_arguments)、versions.pyx、cvar.pyx、log_cpp.pyx、fslike/cpp.pyx、filelike/cpp.pyx
  2. 性能热点:转换器里逐像素/逐帧的媒体解码——slp.pyx、smp.pyx、smx.pyx、sld.pyx、cabextract/lzxd.pyx、PNG/Opus 编码
  3. 嵌入式入口:run.py 编译成带解释器的 bin/run

  ---
  四、nyan:数据描述语言

  nyan 是什么

  nyan 是 openage 的外部数据描述库(独立项目,bin/nyan-external/),提供面向对象、支持继承/多态/补丁(patch)的数据 DSL。openage 用它定义游戏对象(单位、建筑、科技、能力)的属性。

  关键认知:没有 libopenage/nyan/ 目录——C++ 直接 #include <nyan/nyan.h> 用外部库。而 openage/nyan/(Python)只是转换器用的"哑"存储格式,不参与运行时查询。

  nyan DSL 长什么样

  # assets/converted/engine/ability.nyan
  !version 0.2.0
  import engine.ability.property
  import engine.root as root

  Ability(root.Object):
      properties : dict(abstract(...AbilityProperty), AbilityProperty) = {}

  对象用 FQON(fully-qualified object name,点分路径,如 engine.ability.Ability)引用。

  nyan 数据流(转换 vs 运行时,两条独立路径)

  离线转换:
    原版游戏文件(.dat/.slp/.wav…)
      └─ openage/convert/ (Python)
           ├─ nyan_subprocessor.py 构建 NyanObject
           ├─ openage/nyan/nyan_structs.py (哑存储容器)
           ├─ import_tree.py 解析引用、生成 import 语句
           └─ 导出 .nyan 文本 → assets/converted/<modpack>/*.nyan + modpack.toml

  运行时:
    Game::load_data() 遍历 modpack includes
      └─ nyan::Database::load(file, fileload_func)   # C++ 直接调外部 nyan 库
           └─ nyan::Database → GameState.get_db_view() → nyan::View
                └─ EntityFactory 读取 nyan::Object
                     └─ 生成 GameEntity / Terrain / Player 等运行时对象

  资产与 nyan 的关联:assets/converted/engine/util/graphics.nyan 用 file 类型字段引用贴图路径(如 Animation 的 sprite : file),单位通过 ability 引用 Animation,Animation 引用贴图文件。

  ---

  convert 工作流(最大头的 Python 模块)

  openage/convert/ 把原版帝国时代资源转换成 openage 自由格式:

  原版资源(.dat/.slp/.drs/.wav…)
    └─ main.py::convert_assets()           入口,准备 srcdir/targetdir
         ├─ service/init/version_detect    识别原版游戏版本(对照 cfg/converter/games/*.toml 哈希)
         ├─ service/init/mount_asset_dirs   把原版资源 mount 进 VFS
         ├─ value_object/read/media/        二进制解码(slp.pyx/smp.pyx/smx.pyx/sld.pyx/blendomatic/colortable/datfile/drs)
         ├─ processor/                     处理(组合、查询)
         ├─ entity_object/                 转换中间表示
         ├─ service/export/png|opus        媒体重编码(纹理→PNG,音频→Opus)
         ├─ service/read/nyan_api_loader   读 nyan API 定义
         ├─ nyan_subprocessor              构建 NyanObject(openage/nyan/nyan_structs.py)
         └─ service/export                 导出 → .nyan 文本 + 媒体 + modpack.toml
              → assets/converted/<modpack>/

  输入:原版游戏文件;输出:assets/converted/ 下的 nyan 数据 + 转换后的媒体资产 + modpack 清单。

  其他 Python 模块

  ┌───────────────────────────────────────────────────────────────────┬─────────────────────────────────────────────┐
  │                               模块                                │                    职责                     │
  ├───────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────┤
  │ openage/__main__.py                                               │ argparse 中央入口                           │
  ├───────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────┤
  │ openage/main/main.py                                              │ 主子命令(挂载 VFS、转换、调 C++ run_game)   │
  ├───────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────┤
  │ openage/codegen/                                                  │ C++↔Python 绑定的代码生成(生成 .pxd 等)     │
  ├───────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────┤
  │ openage/cppinterface/                                             │ Cython 胶水编排(setup.py 注册所有 PyFunc)   │
  ├───────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────┤
  │ openage/nyan/                                                     │ 转换器用的 nyan 哑存储(非运行时)            │
  ├───────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────┤
  │ openage/cvar/、game/、gamestate/、event/、renderer/、pathfinding/ │ 多为对应 C++ 模块的 Cython 绑定/镜像        │
  ├───────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────┤
  │ openage/util/                                                     │ Python 工具,含 fslike/(Union FS)、filelike/ │
  └───────────────────────────────────────────────────────────────────┴─────────────────────────────────────────────┘

  资产三个 "assets" 位置

  ┌────────────────────────────┬─────────────────────────────────────────────────────────────────────────────┐
  │            位置            │                                    职责                                     │
  ├────────────────────────────┼─────────────────────────────────────────────────────────────────────────────┤
  │ libopenage/assets/ (C++)   │ modpack 管理(ModManager,读 modpack.toml,校验 manifest.toml SHA3,挂载 VFS)   │
  ├────────────────────────────┼─────────────────────────────────────────────────────────────────────────────┤
  │ openage/assets.py (Python) │ 资产定位,搭 Union FS(DEVMODE 用仓库 assets/,否则叠 global + 用户目录 + CLI) │
  ├────────────────────────────┼─────────────────────────────────────────────────────────────────────────────┤
  │ 顶层 assets/               │ 实际文件:shaders/、converted/engine/(内置 nyan)、qml/、textures/、test/     │
  └────────────────────────────┴─────────────────────────────────────────────────────────────────────────────┘

  ---
  八、基础设施层

  ┌────────────────┬─────────────────────────────────────────────────────────────────────────┬───────────────────────────────────────────────────────────┐
  强类型,CRTP 模板编译期阻止"绝对+绝对"等无意义运算,全部由 coord.h.template 生成:            
  
  ┌───────────────────────┬────────────────────────┬─────────────────────────────────────────────────┐
  │        坐标系         │          标量          │                      用途                       │
  ├───────────────────────┼────────────────────────┼─────────────────────────────────────────────────┤
  │ phys3                 │ FixedPoint<int64_t,16> │ 游戏世界物理坐标(亚瓦片精度)                    │
  ├───────────────────────┼────────────────────────┼─────────────────────────────────────────────────┤
  │ tile/tile3            │ int64_t                │ 瓦片整数网格(逻辑)                              │
  ├───────────────────────┼────────────────────────┼─────────────────────────────────────────────────┤                                                 
  │ chunk/chunk3          │ int32_t                │ 区块(16 tile 一格,分块加载)                     │
  ├───────────────────────┼────────────────────────┼─────────────────────────────────────────────────┤                                                 
  │ scene2/3              │ FixedPoint<int64_t,16> │ 渲染中间坐标,to_world_space()→Eigen 喂 GPU      │
  ├───────────────────────┼────────────────────────┼─────────────────────────────────────────────────┤
  │ camhud/viewport/input │ int32_t                │ 屏幕/相机像素(input 经 Camera raycast 转 phys3) │
  └───────────────────────┴────────────────────────┴─────────────────────────────────────────────────┘
  
  等距投影效果由资产方向性 + 正交相机 + scene→world_space 转换组合实现,没有显式 isometric 矩阵。
  
  寻路(pathfinding/)

  基于 flow-field 的分层寻路:高层 A* 跨 sector portal,低层三场计算(CostField→IntegrationField→FlowField)。

  ---
  六、渲染栈
  
  一次"绘制一帧"的调用链

  Presenter::render()                          [presenter.cpp:388]
   ├─ camera_manager->update()                 更新 view/projection,写 UniformBuffer
   ├─ terrain_renderer->update()               从 RenderEntity 拉新数据,重建 mesh
   ├─ world_renderer->update()                 同上
   ├─ hud_renderer->update()
   ├─ gui->render()                            Qt QML 渲染到纹理
   └─ for pass in render_passes:               [skybox→terrain→world→hud→gui→screen]
        └─ renderer->render(pass)              纯虚
           └─ GlRenderer::render(pass)         [opengl/renderer.cpp:170]
                ├─ glClear(...)
                └─ for each Renderable: 上传 uniform + glDraw*
  
  是 6 个 pass 的 FBO 链:前 5 个各渲染到自己的 FBO 纹理,最后 ScreenRenderStage 合到 display(窗口)。

  仿真↔渲染的解耦(核心设计亮点)

  RenderFactory + RenderEntity 是带 shared_mutex 的生产者-消费者桥:
  
  - 仿真线程(生产者):game_entity.cpp/terrain_chunk.cpp 调 render_entity->update(id,pos,angle,animation_path,time) 推数据
  - 渲染线程(消费者):各 RenderStage::update() 每帧拉新数据重建 mesh

  两个线程完全并行,无需锁住整帧。

  渲染阶段(Stages)

  skybox(纯色背景)、terrain(地形)、world(单位/建筑)、hud(选择框等 overlay)、screen(最终合成,带截图)、camera(相机管理,非 stage)。

  输入(input/,基于 Qt)

  OS → Qt(QKeyEvent/QMouseEvent/QWheelEvent)
    → GlWindow(QWindow) → WindowEventHandler
    → Presenter 注册的回调:Qt 事件转 InputEvent → InputManager::process()
    → 活跃 InputContext 栈匹配 → binding_action
    → 三个 Controller 之一:
         camera: CameraManager->move/zoom
         game:   → GameSimulation(commander)
         hud:    拖框选择等

  UI 走 Qt QML(assets/qml/)。默认键位绑定硬编码在 controller/*/binding.cpp,目前无外部配置文件。

  ---
  七、Python 层与 convert(资产转换)

  convert 工作流(最大头的 Python 模块)

  openage/convert/ 把原版帝国时代资源转换成 openage 自由格式:

  原版资源(.dat/.slp/.drs/.wav…)
    └─ main.py::convert_assets()           入口,准备 srcdir/targetdir
         ├─ service/init/version_detect    识别原版游戏版本(对照 cfg/converter/games/*.toml 哈希)
         ├─ service/init/mount_asset_dirs   把原版资源 mount 进 VFS
         ├─ value_object/read/media/        二进制解码(slp.pyx/smp.pyx/smx.pyx/sld.pyx/blendomatic/colortable/datfile/drs)
         ├─ processor/                     处理(组合、查询)
         ├─ entity_object/                 转换中间表示
         ├─ service/export/png|opus        媒体重编码(纹理→PNG,音频→Opus)
         ├─ service/read/nyan_api_loader   读 nyan API 定义
         ├─ nyan_subprocessor              构建 NyanObject(openage/nyan/nyan_structs.py)
         └─ service/export                 导出 → .nyan 文本 + 媒体 + modpack.toml
              → assets/converted/<modpack>/

  输入:原版游戏文件;输出:assets/converted/ 下的 nyan 数据 + 转换后的媒体资产 + modpack 清单。

  其他 Python 模块

  ┌───────────────────────────────────────────────────────────────────┬─────────────────────────────────────────────┐
  │                               模块                                │                    职责                     │
  ├───────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────┤
  │ openage/__main__.py                                               │ argparse 中央入口                           │
  ├───────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────┤
  │ openage/main/main.py                                              │ 主子命令(挂载 VFS、转换、调 C++ run_game)   │
  ├───────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────┤
  │ openage/codegen/                                                  │ C++↔Python 绑定的代码生成(生成 .pxd 等)     │
  ├───────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────┤
  │ openage/cppinterface/                                             │ Cython 胶水编排(setup.py 注册所有 PyFunc)   │
  ├───────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────┤
  │ openage/nyan/                                                     │ 转换器用的 nyan 哑存储(非运行时)            │
  ├───────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────┤
  │ openage/cvar/、game/、gamestate/、event/、renderer/、pathfinding/ │ 多为对应 C++ 模块的 Cython 绑定/镜像        │
  ├───────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────┤
  │ openage/util/                                                     │ Python 工具,含 fslike/(Union FS)、filelike/ │
  └───────────────────────────────────────────────────────────────────┴─────────────────────────────────────────────┘

  资产三个 "assets" 位置

  ┌────────────────────────────┬─────────────────────────────────────────────────────────────────────────────┐
  │            位置            │                                    职责                                     │
  ├────────────────────────────┼─────────────────────────────────────────────────────────────────────────────┤
  │ libopenage/assets/ (C++)   │ modpack 管理(ModManager,读 modpack.toml,校验 manifest.toml SHA3,挂载 VFS)   │
  ├────────────────────────────┼─────────────────────────────────────────────────────────────────────────────┤
  │ openage/assets.py (Python) │ 资产定位,搭 Union FS(DEVMODE 用仓库 assets/,否则叠 global + 用户目录 + CLI) │
  ├────────────────────────────┼─────────────────────────────────────────────────────────────────────────────┤
  │ 顶层 assets/               │ 实际文件:shaders/、converted/engine/(内置 nyan)、qml/、textures/、test/     │
  └────────────────────────────┴─────────────────────────────────────────────────────────────────────────────┘

  ---
  八、基础设施层

  ┌────────────────┬─────────────────────────────────────────────────────────────────────────┬───────────────────────────────────────────────────────────┐
  │      模块      │                                  核心                                   │                           备注                            │
  ├────────────────┼─────────────────────────────────────────────────────────────────────────┼───────────────────────────────────────────────────────────┤
  │ time/          │ Clock(倍速/暂停/停止)+ TimeLoop + time_t 定点                           │ 全链路时间基座,被 gamestate/renderer/event/curve 广泛依赖 │
  ├────────────────┼─────────────────────────────────────────────────────────────────────────┼───────────────────────────────────────────────────────────┤
  │ util/          │ FixedPoint、Path(FSLike/FileLike)、sformat、Enum、向量矩阵、LZX 解压    │ 被依赖最广                                                │
  ├────────────────┼─────────────────────────────────────────────────────────────────────────┼───────────────────────────────────────────────────────────┤
  │ log/           │ LogSource/LogSink + MSG 宏 + 6 级带颜色                                 │ 被依赖最广之一                                            │
  ├────────────────┼─────────────────────────────────────────────────────────────────────────┼───────────────────────────────────────────────────────────┤
  │ error/         │ Error 异常 + ENSURE 宏 + 栈回溯 + terminate 处理器                      │                                                           │
  ├────────────────┼─────────────────────────────────────────────────────────────────────────┼───────────────────────────────────────────────────────────┤
  │ datastructure/ │ PairingHeap(支持 decrease_key,寻路/event 用)、ConcurrentQueue、ConstMap │                                                           │
  ├────────────────┼─────────────────────────────────────────────────────────────────────────┼───────────────────────────────────────────────────────────┤
  │ job/           │ 多线程 job 池                                                           │ 仅截图用,窄                                               │
  ├────────────────┼─────────────────────────────────────────────────────────────────────────┼───────────────────────────────────────────────────────────┤
  │ rng/           │ xorshift128+ 可序列化                                                   │ 窄                                                        │
  └────────────────┴─────────────────────────────────────────────────────────────────────────┴───────────────────────────────────────────────────────────┘

  ---
  九、关键发现:过渡/未接通状态

  引擎正在重写中,几个子系统架构齐全但运行时绑定未接通:

  ┌────────────────────────┬───────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
  │         子系统         │                                                       状态                                                        │
  ├────────────────────────┼───────────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
  │ cvar(console variable) │ CVarManager::create() 在整个 C++ 代码库无任何调用方,load_all() 读 keybinds.oac 后 set 调用因 store 为空而静默丢弃 │
  ├────────────────────────┼───────────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
  │ console(游戏内终端)    │ set/get/list 命令全被注释,register/set_visible/on_* 全是 TODO 存根                                                │
  ├────────────────────────┼───────────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
  │ audio                  │ Qt 音频系统完整但无消费者(presenter.h:143 的 init_audio() 是注释)                                                 │
  ├────────────────────────┼───────────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
  │ options(运行期反射)    │ 历史遗留,与主流程解耦;注释明说"TODO: load option values from cvar system"未连                                     │
  ├────────────────────────┼───────────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
  │ 键位                   │ 实际由 input 模块的 setup_defaults 在 C++ 硬编码,未走 cvar                                                        │
  └────────────────────────┴───────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘

  Vulkan 后端仅是 stub 骨架,Window::create() 当前硬编码返回 opengl::GlWindow。

  ---
  总结一句话

  openage 是一个**三线程(Qt/OpenGL 渲染 + 事件驱动仿真 + 时钟)**的等距 2D 引擎:C++ 层用"curve 时间序列 + event 队列"做数据驱动仿真,通过 RenderEntity 这套带锁的生产者-消费者桥把状态推给渲染线程;Python 层主要离线把原版游戏资产转成 nyan 数据
  + 自由格式媒体;nyan 是贯穿两者的数据描述语言(转换器用哑存储生成,C++ 运行时用外部库查询)。三者靠 Cython 桥(.pyx/.pxd + // pxd: 注释)连接。

  ---
  需要我针对某一块深入吗?比如:
  - convert 资产转换的完整细节(各 reader 格式、processor、export)
  - event/curve 仿真机制的代码级走读
  - nyan DSL 的完整类型系统与 patch 机制
  - 把这份架构存成项目文档(写入 doc/ 或 memory)供以后查阅
