# Copyright 2026+ the openage authors. See copying.md for legal info.

"""Desktop launcher for the native engine, test suites and asset converter."""

from __future__ import annotations

import argparse
import codecs
import json
import os
import queue
import re
import shlex
import signal
import subprocess
import sys
import threading
from dataclasses import asdict, dataclass, fields
from datetime import datetime
from pathlib import Path
from typing import Any, Callable

ROOT = Path(__file__).resolve().parents[1]
SETTINGS = ROOT / "logs/launcher/settings.json"
CAMPAIGN_CATALOG = ROOT / "assets/converted/campaigns/catalog.json"
BUILTIN_CATEGORY = "内置默认与 Wallace 教学"
RUN = ROOT / "bin" / ("run.exe" if os.name == "nt" else "run")
GAME_MODES = {
    "项目默认": "engine",
    "Wallace 第 1 关 · Marching and Fighting": "marching",
    "Wallace 第 2 关 · Feeding the Army": "feeding",
    "Wallace 原版地形预览": "terrain",
    "当前已安装的 Wallace 场景": "installed",
}
TEST_SUITES = (
    "综合测试:自动测试 + Wallace 无头回归",
    "全部自动测试",
    "文本快照回归",
    "Wallace 无头回归",
    "Wallace 启动验证(会打开游戏窗口)",
    "指定测试 / 前缀",
    "模拟演示(指定 ID)",
    "列出测试与演示",
)


@dataclass
class Options:
    """Shared launch inputs, persisted only when the user saves settings."""

    executable: str = str(RUN)
    build_dir: str = str(ROOT / "bin")
    build_first: bool = False
    asset_dir: str = ""
    cfg_dir: str = ""
    verbose: bool = False
    game_mode: str = next(label for label, mode in GAME_MODES.items() if mode == "marching")
    width: int = 1280
    height: int = 720
    window_mode: str = "windowed"
    vsync: bool = False
    gl_debug: bool = False
    headless: bool = False
    ignore_user_input: bool = False
    modpacks: str = ""
    campaign_category: str = BUILTIN_CATEGORY
    campaign_id: str = ""
    mission_id: str = ""
    source_save: str = ""
    terrain_dir: str = ""
    suite: str = TEST_SUITES[0]
    have_assets: bool = False
    update_golden: bool = False
    golden_jobs: int = 1
    golden_timings: bool = False
    test_name: str = "testing.snapshot.headless_regression"
    demo_id: int = 7
    source_dir: str = ""
    output_dir: str = str(ROOT / "assets")
    jobs: int = 2
    compression: int = 2
    low_memory: bool = True
    no_media: bool = False
    no_sounds: bool = False
    no_graphics: bool = False
    export_api: bool = False


def load_campaign_catalog(path: Path = CAMPAIGN_CATALOG) -> dict[str, Any]:
    """Load converted campaign catalog if present; return empty structure otherwise."""
    if not path.is_file():
        return {"categories": [], "campaigns": []}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {"categories": [], "campaigns": []}
    if not isinstance(data, dict):
        return {"categories": [], "campaigns": []}
    return data


def load_options(path: Path) -> Options:
    """Reject corrupt preferences instead of starting with misinterpreted flags."""
    if not path.is_file():
        return Options()
    data = json.loads(path.read_text(encoding="utf-8"))
    defaults = asdict(Options())
    if not isinstance(data, dict) or set(data) - defaults.keys():
        raise ValueError(f"无效的启动器设置: {path}")
    for name, value in data.items():
        if type(value) is not type(defaults[name]):
            raise ValueError(f"设置字段类型错误: {name}")
    return Options(**data)


def command_text(command: list[str]) -> str:
    """Format for display only; execution always uses an argv list without a shell."""
    return subprocess.list2cmdline(command) if os.name == "nt" else shlex.join(command)


def build_plan(options: Options, task: str) -> list[list[str]]:
    """Resolve a launch into explicit commands; raise on incompatible settings."""
    executable = str(Path(options.executable).expanduser().resolve())
    common = ["--verbose"] if options.verbose else []
    for flag, path in (("--asset-dir", options.asset_dir), ("--cfg-dir", options.cfg_dir)):
        if path:
            common.extend((flag, str(Path(path).expanduser().resolve())))
    plan = []
    if options.build_first or task == "build":
        plan.append(["cmake", "--build", str(Path(options.build_dir).expanduser().resolve())])
    if task == "build":
        return plan
    if task == "game":
        if options.width <= 0 or options.height <= 0:
            raise ValueError("窗口宽高必须为正整数。")
        if options.window_mode not in ("windowed", "borderless", "fullscreen"):
            raise ValueError("未知的窗口模式。")
        if options.game_mode not in GAME_MODES:
            raise ValueError("请选择游戏或 Wallace 关卡。")
        mode = GAME_MODES[options.game_mode]
        game_args = [
            *common,
            "--window-size",
            str(options.width),
            str(options.height),
            "--window-mode",
            options.window_mode,
        ]
        for enabled, flag in (
            (options.vsync, "--vsync"),
            (options.gl_debug, "--gl-debug"),
            (options.headless, "--headless"),
            (options.ignore_user_input, "--ignore-user-input"),
        ):
            if enabled:
                game_args.append(flag)
        if options.modpacks.strip():
            game_args.extend(("--modpacks", *options.modpacks.split()))
        if options.campaign_category and options.campaign_category != BUILTIN_CATEGORY:
            if options.headless:
                raise ValueError("战役启动需要窗口;无头回归请使用测试页。")
            if executable != str(RUN.resolve()) or options.asset_dir or options.cfg_dir:
                raise ValueError("战役使用项目 bin/run 和默认资源/配置路径,请还原这些设置。")
            if not options.campaign_id or not options.mission_id:
                raise ValueError("请选择具体的战役与关卡。")
            mission_pack = ROOT / "assets/converted/campaigns" / options.campaign_id / options.mission_id
            if not (mission_pack / "scenario.json").is_file():
                raise ValueError(f"所选关卡尚未转换或不支持: {options.campaign_id}/{options.mission_id}")
            plan.append(
                [
                    sys.executable,
                    str(ROOT / "scripts/campaign_launcher.py"),
                    "--campaign",
                    options.campaign_id,
                    "--mission",
                    options.mission_id,
                    "--game-args",
                    *game_args,
                ]
            )
            return plan
        if mode == "engine":
            plan.append([executable, "main", "--no-convert", "--no-prompts", *game_args])
        else:
            if options.headless:
                raise ValueError("Wallace 游戏启动需要窗口;无头回归请使用测试页。")
            if executable != str(RUN.resolve()) or options.asset_dir or options.cfg_dir:
                raise ValueError("Wallace 使用项目 bin/run 和默认资源/配置路径,请还原这些设置。")
            if mode == "installed":
                plan.append(["bash", str(ROOT / "scripts/run_ww_mission1.sh"), *game_args])
            else:
                command = [sys.executable, str(ROOT / "scripts/campaign_launcher.py"), "--mode", mode]
                for flag, path in (("--save", options.source_save), ("--terrain-dds", options.terrain_dir)):
                    if path:
                        command.extend((flag, str(Path(path).expanduser().resolve())))
                plan.append([*command, "--game-args", *game_args])
    elif task == "tests":
        if options.golden_jobs < 1:
            raise ValueError("Golden 并行用例数必须为正整数。")
        if options.suite not in TEST_SUITES:
            raise ValueError("请选择测试套件。")
        suite = TEST_SUITES.index(options.suite)
        if options.update_golden and suite not in (0, 1, 2, 5):
            raise ValueError("更新 golden 只适用于自动测试或指定快照测试。")
        if suite in (0, 1, 2, 5, 7):
            command = [executable, "test", *common, "--golden-jobs", str(options.golden_jobs)]
            if options.golden_timings:
                command.append("--golden-timings")
            if suite in (0, 1):
                command.append("--run-all-tests")
                if options.have_assets:
                    command.append("--have-assets")
            elif suite == 2:
                command.append("testing.snapshot.headless_regression")
            elif suite == 5:
                if not options.test_name.strip() or options.test_name.startswith("-"):
                    raise ValueError("请填写测试名称或前缀。")
                command.append(options.test_name.strip())
            else:
                command.append("--list")
            if options.update_golden:
                command.append("--update-golden")
            plan.append(command)
        if suite in (0, 3, 4):
            if executable != str(RUN.resolve()) or options.asset_dir or options.cfg_dir:
                raise ValueError("Wallace 回归脚本固定使用项目 bin/run 和 assets/cfg,请使用默认路径。")
            script = "verify_ww_mission1.sh" if suite == 4 else "p4_4d_regression.sh"
            command = ["bash", str(ROOT / "scripts" / script)]
            if suite != 4:
                command.append("--regression")
            plan.append(command)
        if suite == 6:
            if options.demo_id < 0:
                raise ValueError("演示 ID 不能为负数。")
            plan.append(
                [
                    executable,
                    "test",
                    *common,
                    "--demo",
                    "gamestate.tests.simulation_demo",
                    str(options.demo_id),
                    *(["--asset-dir", options.asset_dir] if options.asset_dir else []),
                    *(["--cfg-dir", options.cfg_dir] if options.cfg_dir else []),
                ]
            )
    elif task == "convert":
        if not options.source_dir.strip():
            raise ValueError("请选择原版游戏安装目录。")
        if not options.output_dir.strip():
            raise ValueError("请选择转换输出的资源根目录。")
        if options.jobs < 1 or options.compression not in range(5):
            raise ValueError("并发数必须大于 0,PNG 压缩级别必须为 0-4。")
        # --force is required: convert --no-prompts alone performs no conversion.
        command = [
            executable,
            "convert",
            "--force",
            "--no-prompts",
            "--source-dir",
            str(Path(options.source_dir).expanduser().resolve()),
            "--output-dir",
            str(Path(options.output_dir).expanduser().resolve()),
            "--jobs",
            str(options.jobs),
            "--compression-level",
            str(options.compression),
        ]
        if options.verbose:
            command.append("--verbose")
        for field, flag in (
            ("low_memory", "--low-memory"),
            ("no_media", "--no-media"),
            ("no_sounds", "--no-sounds"),
            ("no_graphics", "--no-graphics"),
            ("export_api", "--export-api"),
        ):
            if getattr(options, field):
                command.append(flag)
        plan.append(command)
    else:
        raise ValueError(f"未知任务:{task}")
    return plan


def run_plan(
    plan: list[list[str]],
    emit: Callable[[str], None],
    stop: threading.Event,
    publish: Callable[[subprocess.Popen[bytes] | None], None],
) -> int:
    """Run sequentially, stream output and preserve the first failing exit code."""
    for command in plan:
        if stop.is_set():
            return 130
        emit(f"\n$ {command_text(command)}\n")
        process = subprocess.Popen(
            command,
            cwd=ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            stdin=subprocess.PIPE,
            bufsize=0,
            start_new_session=os.name != "nt",
            creationflags=subprocess.CREATE_NEW_PROCESS_GROUP if os.name == "nt" else 0,
            env={**os.environ, "PYTHONUNBUFFERED": "1"},
        )
        publish(process)
        try:
            assert process.stdout is not None
            decoder = codecs.getincrementaldecoder("utf-8")("replace")
            while chunk := process.stdout.read(4096):
                emit(decoder.decode(chunk))
            emit(decoder.decode(b"", final=True))
            code = process.wait()
        finally:
            if process.poll() is None:
                stop_process(process, force=True)
                process.wait()
            for stream in (process.stdin, process.stdout):
                if stream:
                    stream.close()
            publish(None)
        if stop.is_set():
            return 130
        if code:
            return code
    return 0


def stop_process(process: subprocess.Popen[bytes], force: bool = False) -> None:
    """Stop only the process group created for this launch, including its children."""
    try:
        if os.name == "nt":
            subprocess.run(
                ["taskkill", "/PID", str(process.pid), "/T", "/F"],
                check=False,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
        else:
            os.killpg(process.pid, signal.SIGKILL if force else signal.SIGTERM)
    except ProcessLookupError:
        pass


STOP_HINT = "停止当前任务并关闭启动器?"
EMBLEMS_DIR = ROOT / "logs/tech-trees/emblems"
CAMPAIGN_EMBLEMS: dict[str, str] = {
    "cam0": "chinese",
    "cam1": "celts",
    "cam2": "franks",
    "cam3": "saracens",
    "cam4": "mongols",
    "cam5": "teutons",
    "xcam1": "huns",
    "xcam2": "spanish",
    "xcam3": "aztecs",
    "fcam1": "goths",
    "fcam2": "slavs",
    "fcam3": "byzantines",
    "fcam4": "italians",
    "fcam5": "incas",
    "fcam6": "gurjaras",
    "fcam7": "britons",
    "acam1": "berbers",
    "acam2": "malians",
    "acam3": "portuguese",
    "acam4": "ethiopians",
    "rcam1": "malay",
    "rcam2": "khmer",
    "rcam3": "burmese",
    "rcam4": "vietnamese",
    "kcam1": "tatars",
    "kcam2": "bulgarians",
    "kcam3": "cumans",
    "wecam1": "britons",
    "wecam2": "burgundians",
    "wecam3": "sicilians",
    "eecam1": "lithuanians",
    "eecam2": "poles",
    "eecam3": "bohemians",
    "incam1": "indians",
    "incam2": "dravidians",
    "incam3": "bengalis",
    "coop3": "saracens",
    "coop6": "huns",
    "coop9": "britons",
    "coop10": "goths",
    "coop17": "berbers",
    "coop22": "khmer",
    "coop25": "tatars",
    "dcam0": "mongols",
    "e3_bonus": "britons",
}
CATEGORY_EMBLEMS: dict[str, str] = {
    "art_of_war": "chinese",
    "age_of_kings": "celts",
    "conquerors": "aztecs",
    "forgotten": "byzantines",
    "historical_battles": "britons",
    "african_kingdoms": "ethiopians",
    "rise_of_the_rajas": "khmer",
    "last_khans": "mongols",
    "lords_of_the_west": "burgundians",
    "dawn_of_the_dukes": "poles",
    "dynasties_of_india": "indians",
    "coop": "franks",
    "events": "tatars",
}


def create_window(settings_path: Path = SETTINGS):
    """Build the graphical campaign browser UI with an Advanced Options drawer."""
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk
    from tkinter.scrolledtext import ScrolledText

    try:
        from PIL import Image, ImageTk
    except ImportError:
        Image = None  # type: ignore[assignment]
        ImageTk = None  # type: ignore[assignment]

    options = load_options(settings_path)
    try:
        window = tk.Tk()
    except tk.TclError as error:
        raise ValueError(f"Tk 窗口初始化失败:{error}") from error

    BG = "#161920"
    PANEL = "#1E222B"
    CARD = "#252A36"
    CARD_SELECTED = "#323A4C"
    BORDER = "#343B4D"
    GOLD = "#E0B861"
    GOLD_DARK = "#B88E38"
    FG = "#F2EEE5"
    MUTED = "#9CA3B4"
    GREEN = "#64C27B"
    RED = "#E06C75"

    window.title("openage · 帝国时代 II 战役大厅")
    window.geometry("1220x820")
    window.minsize(980, 680)
    window.configure(bg=BG)
    window.columnconfigure(0, weight=1)
    window.rowconfigure(1, weight=1)

    variables: dict[str, tk.Variable] = {}
    for field in fields(Options):
        value = getattr(options, field.name)
        variables[field.name] = (
            tk.BooleanVar(value=value) if isinstance(value, bool) else tk.StringVar(value=str(value))
        )
    events: queue.Queue[tuple[str, Any]] = queue.Queue(maxsize=2048)
    stop = threading.Event()
    active = None
    running = False
    closing = False
    log_path = Path()
    ansi = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
    image_cache: dict[tuple[str, int], Any] = {}

    def load_emblem(name: str, size: int):
        if Image is None or ImageTk is None or not name:
            return None
        key = (name, size)
        if key in image_cache:
            return image_cache[key]
        path = EMBLEMS_DIR / f"{name}.png"
        if not path.is_file():
            return None
        try:
            img = Image.open(path).convert("RGBA").resize((size, size), Image.Resampling.LANCZOS)
            photo = ImageTk.PhotoImage(img)
            image_cache[key] = photo
            return photo
        except OSError:
            return None

    def scrollable_frame(parent, bg=BG):
        container = tk.Frame(parent, bg=bg)
        container.columnconfigure(0, weight=1)
        container.rowconfigure(0, weight=1)
        canvas = tk.Canvas(container, bg=bg, highlightthickness=0, bd=0)
        vbar = ttk.Scrollbar(container, orient="vertical", command=canvas.yview)
        canvas.configure(yscrollcommand=vbar.set)
        canvas.grid(row=0, column=0, sticky="nsew")
        vbar.grid(row=0, column=1, sticky="ns")
        inner = tk.Frame(canvas, bg=bg)
        win_id = canvas.create_window((0, 0), window=inner, anchor="nw")
        inner.bind("<Configure>", lambda _e: canvas.configure(scrollregion=canvas.bbox("all")))
        canvas.bind("<Configure>", lambda e: canvas.itemconfigure(win_id, width=e.width))

        def _on_wheel(event):
            delta = -1 * (event.delta // 120) if abs(event.delta) >= 120 else (-1 if event.delta > 0 else 1)
            canvas.yview_scroll(delta, "units")

        canvas.bind("<Enter>", lambda _e: canvas.bind_all("<MouseWheel>", _on_wheel))
        canvas.bind("<Leave>", lambda _e: canvas.unbind_all("<MouseWheel>"))
        return container, canvas, inner

    # ---- Top Header Bar ----
    header = tk.Frame(window, bg=PANEL, padx=18, pady=12)
    header.grid(row=0, column=0, sticky="ew")
    header.columnconfigure(1, weight=1)
    tk.Label(
        header,
        text="⚔  openage · 帝国时代 II 战役大厅",
        bg=PANEL,
        fg=GOLD,
        font=("TkDefaultFont", 18, "bold"),
    ).grid(row=0, column=0, sticky="w")

    search_var = tk.StringVar()
    search_box = tk.Frame(header, bg=PANEL)
    search_box.grid(row=0, column=1, sticky="e", padx=12)
    tk.Label(search_box, text="搜索战役/关卡:", bg=PANEL, fg=MUTED).pack(side="left", padx=(0, 6))
    search_entry = ttk.Entry(search_box, textvariable=search_var, width=26)
    search_entry.pack(side="left")

    header_btns = tk.Frame(header, bg=PANEL)
    header_btns.grid(row=0, column=2, sticky="e")

    # ---- Main Body: Left Category Sidebar + Right Campaign/Mission Browser ----
    body = tk.Frame(window, bg=BG, padx=12, pady=10)
    body.grid(row=1, column=0, sticky="nsew")
    body.columnconfigure(1, weight=1)
    body.rowconfigure(0, weight=1)

    sidebar_wrap = tk.Frame(body, bg=PANEL, width=270, highlightbackground=BORDER, highlightthickness=1)
    sidebar_wrap.grid(row=0, column=0, sticky="ns", padx=(0, 12))
    sidebar_wrap.grid_propagate(False)
    sidebar_wrap.columnconfigure(0, weight=1)
    sidebar_wrap.rowconfigure(1, weight=1)
    tk.Label(
        sidebar_wrap,
        text="战役类目",
        bg=PANEL,
        fg=GOLD,
        font=("TkDefaultFont", 12, "bold"),
        anchor="w",
        padx=12,
        pady=10,
    ).grid(row=0, column=0, sticky="ew")
    cat_container, _cat_canvas, cat_list_frame = scrollable_frame(sidebar_wrap, bg=PANEL)
    cat_container.grid(row=1, column=0, sticky="nsew", padx=4, pady=(0, 6))

    right_pane = tk.PanedWindow(
        body,
        orient="vertical",
        bg=BG,
        sashwidth=6,
        sashrelief="flat",
        bd=0,
    )
    right_pane.grid(row=0, column=1, sticky="nsew")

    camp_section = tk.Frame(right_pane, bg=PANEL, highlightbackground=BORDER, highlightthickness=1)
    camp_section.columnconfigure(0, weight=1)
    camp_section.rowconfigure(1, weight=1)
    camp_header_var = tk.StringVar(value="选择战役")
    tk.Label(
        camp_section,
        textvariable=camp_header_var,
        bg=PANEL,
        fg=GOLD,
        font=("TkDefaultFont", 12, "bold"),
        anchor="w",
        padx=14,
        pady=8,
    ).grid(row=0, column=0, sticky="ew")
    camp_container, _camp_canvas, camp_grid = scrollable_frame(camp_section, bg=BG)
    camp_container.grid(row=1, column=0, sticky="nsew", padx=6, pady=(0, 6))

    mission_section = tk.Frame(right_pane, bg=PANEL, highlightbackground=BORDER, highlightthickness=1)
    mission_section.columnconfigure(0, weight=1)
    mission_section.rowconfigure(1, weight=1)
    mission_header_var = tk.StringVar(value="关卡列表")
    tk.Label(
        mission_section,
        textvariable=mission_header_var,
        bg=PANEL,
        fg=GOLD,
        font=("TkDefaultFont", 12, "bold"),
        anchor="w",
        padx=14,
        pady=8,
    ).grid(row=0, column=0, sticky="ew")
    mission_container, _mission_canvas, mission_grid = scrollable_frame(mission_section, bg=BG)
    mission_container.grid(row=1, column=0, sticky="nsew", padx=6, pady=(0, 6))

    right_pane.add(camp_section, minsize=210, stretch="always")
    right_pane.add(mission_section, minsize=220, stretch="always")

    # ---- Bottom Launch Bar ----
    footer = tk.Frame(window, bg=PANEL, padx=18, pady=10, highlightbackground=BORDER, highlightthickness=1)
    footer.grid(row=2, column=0, sticky="ew")
    footer.columnconfigure(0, weight=1)
    selection_summary = tk.StringVar(value="请选择战役与关卡")
    status = tk.StringVar(value="就绪")
    info_col = tk.Frame(footer, bg=PANEL)
    info_col.grid(row=0, column=0, sticky="w")
    tk.Label(info_col, textvariable=selection_summary, bg=PANEL, fg=FG, font=("TkDefaultFont", 12, "bold")).pack(
        anchor="w"
    )
    tk.Label(info_col, textvariable=status, bg=PANEL, fg=MUTED, font=("TkDefaultFont", 10)).pack(anchor="w")

    btn_bar = tk.Frame(footer, bg=PANEL)
    btn_bar.grid(row=0, column=1, sticky="e")

    # ---- Advanced Options & Logs Toplevel Window ----
    adv_win = tk.Toplevel(window)
    adv_win.title("openage · 高级选项与开发工具")
    adv_win.geometry("1020x780")
    adv_win.minsize(860, 640)
    adv_win.withdraw()
    adv_win.protocol("WM_DELETE_WINDOW", adv_win.withdraw)
    adv_win.columnconfigure(0, weight=1)
    adv_win.rowconfigure(1, weight=1)

    def show_advanced(tab_index: int | None = None):
        if tab_index is not None:
            adv_tabs.select(tab_index)
        adv_win.deiconify()
        adv_win.lift()

    def entry(parent, row, label, key, browse=None):
        ttk.Label(parent, text=label).grid(row=row, column=0, sticky="w", padx=6, pady=3)
        widget = ttk.Entry(parent, textvariable=variables[key])
        widget.grid(row=row, column=1, sticky="ew", padx=6, pady=3)
        if browse:

            def choose():
                path = (
                    filedialog.askdirectory(parent=adv_win)
                    if browse == "dir"
                    else filedialog.askopenfilename(parent=adv_win)
                )
                if path:
                    variables[key].set(path)

            ttk.Button(parent, text="浏览…", command=choose).grid(row=row, column=2, padx=6)
        return widget

    def checks(parent, keys):
        for index, (key, label) in enumerate(keys):
            ttk.Checkbutton(parent, text=label, variable=variables[key]).grid(
                row=index // 4, column=index % 4, sticky="w", padx=8, pady=3
            )

    def combo(parent, row, label, key, choices):
        ttk.Label(parent, text=label).grid(row=row, column=0, sticky="w", padx=6, pady=3)
        widget = ttk.Combobox(parent, textvariable=variables[key], values=choices, state="readonly")
        widget.grid(row=row, column=1, columnspan=2, sticky="ew", padx=6, pady=3)
        return widget

    adv_tabs = ttk.Notebook(adv_win)
    adv_tabs.grid(row=0, column=0, sticky="ew", padx=12, pady=(10, 4))
    adv_pages = []
    for name in ("运行与窗口设置", "测试套件", "资源转换 (Converter)"):
        page = ttk.Frame(adv_tabs, padding=10)
        page.columnconfigure(1, weight=1)
        adv_tabs.add(page, text=name)
        adv_pages.append(page)
    page_runtime, page_tests, page_converter = adv_pages

    # Tab 1: Runtime & Window Settings
    entry(page_runtime, 0, "运行程序", "executable", "file")
    entry(page_runtime, 1, "构建目录", "build_dir", "dir")
    entry(page_runtime, 2, "资源根目录(留空用默认)", "asset_dir", "dir")
    entry(page_runtime, 3, "配置目录(留空用默认)", "cfg_dir", "dir")
    dimensions = ttk.Frame(page_runtime)
    dimensions.grid(row=4, column=0, columnspan=3, sticky="w", padx=4, pady=3)
    for key, label in (("width", "窗口宽"), ("height", "窗口高")):
        ttk.Label(dimensions, text=label).pack(side="left", padx=4)
        ttk.Entry(dimensions, textvariable=variables[key], width=8).pack(side="left")
    combo(page_runtime, 5, "窗口模式", "window_mode", ["windowed", "borderless", "fullscreen"])
    entry(page_runtime, 6, "Modpacks(空格分隔,留空自动)", "modpacks")
    entry(page_runtime, 7, "原版存档(仅地形预览)", "source_save", "file")
    entry(page_runtime, 8, "原版 terrain/textures/2x", "terrain_dir", "dir")
    rt_checks = ttk.Frame(page_runtime)
    rt_checks.grid(row=9, column=0, columnspan=3, sticky="w")
    checks(
        rt_checks,
        [
            ("build_first", "运行前构建"),
            ("verbose", "详细日志"),
            ("vsync", "VSync"),
            ("gl_debug", "OpenGL 调试"),
            ("headless", "项目无头模式"),
            ("ignore_user_input", "自动验证: 仅响应 Esc"),
        ],
    )

    # Tab 2: Tests
    combo(page_tests, 0, "测试套件", "suite", TEST_SUITES)
    entry(page_tests, 1, "指定测试 / 前缀", "test_name")
    entry(page_tests, 2, "模拟演示 ID", "demo_id")
    entry(page_tests, 3, "Golden 并行用例数", "golden_jobs")
    test_checks = ttk.Frame(page_tests)
    test_checks.grid(row=4, column=0, columnspan=3, sticky="w")
    checks(
        test_checks,
        [
            ("have_assets", "自动测试包含资产用例"),
            ("update_golden", "更新 golden 基准"),
            ("golden_timings", "Golden 分阶段耗时"),
        ],
    )
    ttk.Label(
        page_tests,
        text="综合测试 = 自动测试 + Wallace 回归,首个失败即停止。Wallace 需要已转换资产。",
    ).grid(row=5, column=0, columnspan=3, sticky="w", padx=6, pady=6)
    test_run_btn = ttk.Button(page_tests, text="运行测试套件", command=lambda: launch("tests"))
    test_run_btn.grid(row=6, column=2, sticky="e", padx=6, pady=4)

    # Tab 3: Converter
    entry(page_converter, 0, "原版游戏安装目录", "source_dir", "dir")
    entry(page_converter, 1, "输出资源根目录(写入 converted/)", "output_dir", "dir")
    entry(page_converter, 2, "并发数", "jobs")
    combo(page_converter, 3, "PNG 压缩级别", "compression", list(range(5)))
    convert_checks = ttk.Frame(page_converter)
    convert_checks.grid(row=4, column=0, columnspan=3, sticky="w")
    checks(
        convert_checks,
        [
            ("low_memory", "低内存"),
            ("no_media", "跳过全部媒体"),
            ("no_sounds", "跳过声音"),
            ("no_graphics", "跳过游戏图形"),
            ("export_api", "导出 nyan API"),
        ],
    )
    conv_btns = ttk.Frame(page_converter)
    conv_btns.grid(row=5, column=0, columnspan=3, sticky="e", pady=4)
    conv_run_btn = ttk.Button(conv_btns, text="转换基础资源", command=lambda: launch("convert"))
    conv_run_btn.pack(side="left", padx=4)
    camp_conv_btn = ttk.Button(
        conv_btns,
        text="批量转换全部战役 (含 DLC / 孙子兵法)",
        command=lambda: launch_custom(
            [[sys.executable, str(ROOT / "scripts/convert_all_campaigns.py")]],
            refresh_after=True,
        ),
    )
    camp_conv_btn.pack(side="left", padx=4)

    # Advanced Logs & Actions
    log_frame = ttk.LabelFrame(adv_win, text="运行日志与命令输出", padding=6)
    log_frame.grid(row=1, column=0, sticky="nsew", padx=12, pady=4)
    log_frame.columnconfigure(0, weight=1)
    log_frame.rowconfigure(0, weight=1)
    output = ScrolledText(log_frame, height=14, wrap="word", state="disabled", font="TkFixedFont")
    output.grid(row=0, column=0, columnspan=2, sticky="nsew")
    input_text = ttk.Entry(log_frame)
    input_text.grid(row=1, column=0, sticky="ew", pady=(6, 0))

    adv_actions = ttk.Frame(adv_win, padding=(12, 6))
    adv_actions.grid(row=2, column=0, sticky="ew")

    # ---- Catalog State & Graphical Rendering ----
    catalog_data: dict[str, Any] = load_campaign_catalog()
    campaigns_by_category: dict[str, list[dict[str, Any]]] = {}
    campaigns_by_id: dict[str, dict[str, Any]] = {}
    category_id_to_title: dict[str, str] = {}

    def reload_catalog_index():
        nonlocal catalog_data
        catalog_data = load_campaign_catalog()
        campaigns_by_category.clear()
        campaigns_by_id.clear()
        category_id_to_title.clear()
        for cat in catalog_data.get("categories", []):
            category_id_to_title[cat["id"]] = cat["title"]
        for camp in catalog_data.get("campaigns", []):
            campaigns_by_id[camp["id"]] = camp
            campaigns_by_category.setdefault(camp["category"], []).append(camp)

    def update_summary():
        cat_title = variables["campaign_category"].get()
        if not cat_title or cat_title == BUILTIN_CATEGORY:
            mode_label = variables["game_mode"].get()
            selection_summary.set(f"当前选择: {BUILTIN_CATEGORY} · {mode_label}")
            return
        cid = variables["campaign_id"].get()
        mid = variables["mission_id"].get()
        camp = campaigns_by_id.get(cid)
        mission = next((m for m in (camp or {}).get("missions", []) if m.get("id") == mid), None)
        if camp and mission:
            if mission.get("status") == "converted":
                selection_summary.set(
                    f"当前选择: {camp['title']} → {mission['title']}  "
                    f"({mission.get('width', '?')}×{mission.get('height', '?')} · "
                    f"{mission.get('objects', 0)} 单位 · {mission.get('triggers', 0)} 触发器)"
                )
            else:
                selection_summary.set(f"当前选择: {camp['title']} → {mission['title']} [格式暂不支持]")
        elif camp:
            selection_summary.set(f"当前选择: {camp['title']}")
        else:
            selection_summary.set("请选择战役与关卡")

    def render_missions():
        for child in mission_grid.winfo_children():
            child.destroy()
        cat_title = variables["campaign_category"].get()
        if cat_title == BUILTIN_CATEGORY:
            mission_header_var.set("内置教学与默认模式")
            for idx, (label, mode_id) in enumerate(GAME_MODES.items()):
                selected = variables["game_mode"].get() == label
                card = tk.Frame(
                    mission_grid,
                    bg=CARD_SELECTED if selected else CARD,
                    highlightbackground=GOLD if selected else BORDER,
                    highlightthickness=2 if selected else 1,
                    padx=12,
                    pady=10,
                    cursor="hand2",
                )
                card.grid(row=idx // 2, column=idx % 2, sticky="ew", padx=6, pady=6)
                mission_grid.columnconfigure(idx % 2, weight=1)
                t_lbl = tk.Label(
                    card,
                    text=label,
                    bg=card["bg"],
                    fg=GOLD if selected else FG,
                    font=("TkDefaultFont", 11, "bold"),
                    anchor="w",
                )
                t_lbl.pack(fill="x")
                s_lbl = tk.Label(
                    card,
                    text=f"模式标识: {mode_id}",
                    bg=card["bg"],
                    fg=MUTED,
                    font=("TkDefaultFont", 9),
                    anchor="w",
                )
                s_lbl.pack(fill="x", pady=(3, 0))

                def pick_builtin(_e=None, m_label=label):
                    variables["game_mode"].set(m_label)
                    render_missions()
                    update_summary()

                for w in (card, t_lbl, s_lbl):
                    w.bind("<Button-1>", pick_builtin)
                    w.bind("<Double-Button-1>", lambda _e, m_label=label: (pick_builtin(m_label=m_label), launch("game")))
            update_summary()
            return

        cid = variables["campaign_id"].get()
        camp = campaigns_by_id.get(cid)
        if not camp:
            mission_header_var.set("关卡列表")
            update_summary()
            return
        query = search_var.get().strip().lower()
        missions = camp.get("missions", [])
        if query:
            filtered = [m for m in missions if query in m["title"].lower() or query in m["id"].lower()]
            if filtered:
                missions = filtered
        mission_header_var.set(f"{camp['title']} · 关卡列表 ({len(missions)} 关)")
        if not any(m["id"] == variables["mission_id"].get() for m in missions):
            first_ok = next((m["id"] for m in missions if m.get("status") == "converted"), missions[0]["id"] if missions else "")
            variables["mission_id"].set(first_ok)

        cols = 2
        for c in range(cols):
            mission_grid.columnconfigure(c, weight=1)
        for idx, mission in enumerate(missions):
            mid = mission["id"]
            ok = mission.get("status") == "converted"
            selected = variables["mission_id"].get() == mid
            card = tk.Frame(
                mission_grid,
                bg=CARD_SELECTED if selected else CARD,
                highlightbackground=GOLD if selected else BORDER,
                highlightthickness=2 if selected else 1,
                padx=12,
                pady=9,
                cursor="hand2" if ok else "arrow",
            )
            card.grid(row=idx // cols, column=idx % cols, sticky="ew", padx=6, pady=5)
            row_frame = tk.Frame(card, bg=card["bg"])
            row_frame.pack(fill="x")
            badge = tk.Label(
                row_frame,
                text=f"{idx + 1:02d}",
                bg=GOLD if selected else "#2E3546",
                fg="#111318" if selected else FG,
                font=("TkDefaultFont", 10, "bold"),
                width=3,
                pady=2,
            )
            badge.pack(side="left", padx=(0, 8))
            title_lbl = tk.Label(
                row_frame,
                text=mission["title"],
                bg=card["bg"],
                fg=FG if ok else MUTED,
                font=("TkDefaultFont", 11, "bold"),
                anchor="w",
            )
            title_lbl.pack(side="left", fill="x", expand=True)
            detail_text = (
                f"地图 {mission.get('width', '?')}×{mission.get('height', '?')} · "
                f"单位 {mission.get('objects', 0)} · 触发器 {mission.get('triggers', 0)}"
                if ok
                else f"未支持格式: {str(mission.get('error', ''))[:46]}"
            )
            detail_lbl = tk.Label(
                card,
                text=detail_text,
                bg=card["bg"],
                fg=GREEN if ok else RED,
                font=("TkDefaultFont", 9),
                anchor="w",
            )
            detail_lbl.pack(fill="x", pady=(4, 0))

            def pick_mission(_e=None, target_id=mid):
                variables["mission_id"].set(target_id)
                render_missions()
                update_summary()

            def run_mission(_e=None, target_id=mid, can_run=ok):
                variables["mission_id"].set(target_id)
                render_missions()
                update_summary()
                if can_run:
                    launch("game")

            for w in (card, row_frame, badge, title_lbl, detail_lbl):
                w.bind("<Button-1>", pick_mission)
                w.bind("<Double-Button-1>", run_mission)
        update_summary()

    def render_campaigns():
        for child in camp_grid.winfo_children():
            child.destroy()
        cat_title = variables["campaign_category"].get()
        query = search_var.get().strip().lower()
        if cat_title == BUILTIN_CATEGORY and not query:
            camp_header_var.set("内置教学与默认入口")
            card = tk.Frame(
                camp_grid,
                bg=CARD_SELECTED,
                highlightbackground=GOLD,
                highlightthickness=2,
                padx=14,
                pady=12,
            )
            card.grid(row=0, column=0, sticky="ew", padx=6, pady=6)
            camp_grid.columnconfigure(0, weight=1)
            emblem = load_emblem("celts", 52)
            if emblem is not None:
                tk.Label(card, image=emblem, bg=CARD_SELECTED).pack(side="left", padx=(0, 12))
            text_box = tk.Frame(card, bg=CARD_SELECTED)
            text_box.pack(side="left", fill="x", expand=True)
            tk.Label(
                text_box,
                text="William Wallace 教学与项目默认启动",
                bg=CARD_SELECTED,
                fg=GOLD,
                font=("TkDefaultFont", 12, "bold"),
                anchor="w",
            ).pack(fill="x")
            tk.Label(
                text_box,
                text="在下方选择项目默认模式或 Wallace 第 1/2 关，双击即可直接启动。",
                bg=CARD_SELECTED,
                fg=MUTED,
                font=("TkDefaultFont", 10),
                anchor="w",
            ).pack(fill="x", pady=(3, 0))
            render_missions()
            return

        if query:
            camps = [
                c
                for c in catalog_data.get("campaigns", [])
                if query in c["title"].lower()
                or query in c["id"].lower()
                or any(query in m["title"].lower() or query in m["id"].lower() for m in c.get("missions", []))
            ]
            camp_header_var.set(f"搜索结果 ({len(camps)} 个战役)")
        else:
            cat_id = next((c["id"] for c in catalog_data.get("categories", []) if c["title"] == cat_title), "")
            camps = campaigns_by_category.get(cat_id, [])
            camp_header_var.set(f"{cat_title} ({len(camps)} 个战役)")

        if not camps:
            tk.Label(
                camp_grid,
                text="未找到匹配的战役。可点击右上角「高级选项」运行批量战役转换。",
                bg=BG,
                fg=MUTED,
                pady=20,
            ).grid(row=0, column=0, sticky="w", padx=12)
            render_missions()
            return

        if not any(c["id"] == variables["campaign_id"].get() for c in camps):
            variables["campaign_id"].set(camps[0]["id"])

        cols = 3
        for c in range(cols):
            camp_grid.columnconfigure(c, weight=1)
        for idx, camp in enumerate(camps):
            cid = camp["id"]
            selected = variables["campaign_id"].get() == cid
            missions = camp.get("missions", [])
            ok_count = sum(1 for m in missions if m.get("status") == "converted")
            card = tk.Frame(
                camp_grid,
                bg=CARD_SELECTED if selected else CARD,
                highlightbackground=GOLD if selected else BORDER,
                highlightthickness=2 if selected else 1,
                padx=10,
                pady=10,
                cursor="hand2",
            )
            card.grid(row=idx // cols, column=idx % cols, sticky="nsew", padx=6, pady=6)
            emblem = load_emblem(CAMPAIGN_EMBLEMS.get(cid, "britons"), 46)
            widgets: list[Any] = [card]
            if emblem is not None:
                img_lbl = tk.Label(card, image=emblem, bg=card["bg"])
                img_lbl.pack(side="left", padx=(0, 10))
                widgets.append(img_lbl)
            meta = tk.Frame(card, bg=card["bg"])
            meta.pack(side="left", fill="both", expand=True)
            widgets.append(meta)
            t_lbl = tk.Label(
                meta,
                text=camp["title"],
                bg=card["bg"],
                fg=GOLD if selected else FG,
                font=("TkDefaultFont", 10, "bold"),
                anchor="w",
                wraplength=175,
                justify="left",
            )
            t_lbl.pack(fill="x")
            c_lbl = tk.Label(
                meta,
                text=f"{ok_count}/{len(missions)} 关已转换",
                bg=card["bg"],
                fg=GREEN if ok_count else RED,
                font=("TkDefaultFont", 9),
                anchor="w",
            )
            c_lbl.pack(fill="x", pady=(4, 0))
            widgets.extend((t_lbl, c_lbl))

            def pick_campaign(_e=None, target_cid=cid, target_cat=camp["category"]):
                if query:
                    variables["campaign_category"].set(category_id_to_title.get(target_cat, BUILTIN_CATEGORY))
                    render_categories()
                variables["campaign_id"].set(target_cid)
                render_campaigns()

            for w in widgets:
                w.bind("<Button-1>", pick_campaign)
        render_missions()

    def render_categories():
        for child in cat_list_frame.winfo_children():
            child.destroy()
        current_cat = variables["campaign_category"].get()
        entries: list[tuple[str, str, str]] = [(BUILTIN_CATEGORY, "celts", f"{len(GAME_MODES)} 项")]
        for cat in catalog_data.get("categories", []):
            camps = campaigns_by_category.get(cat["id"], [])
            if not camps:
                continue
            ok_total = sum(1 for c in camps for m in c.get("missions", []) if m.get("status") == "converted")
            entries.append((cat["title"], CATEGORY_EMBLEMS.get(cat["id"], "britons"), f"{ok_total} 关"))

        for idx, (cat_title, emblem_name, count_text) in enumerate(entries):
            selected = current_cat == cat_title
            row_btn = tk.Frame(
                cat_list_frame,
                bg=CARD_SELECTED if selected else PANEL,
                highlightbackground=GOLD if selected else PANEL,
                highlightthickness=1,
                padx=8,
                pady=7,
                cursor="hand2",
            )
            row_btn.pack(fill="x", padx=4, pady=2)
            emblem = load_emblem(emblem_name, 22)
            widgets: list[Any] = [row_btn]
            if emblem is not None:
                e_lbl = tk.Label(row_btn, image=emblem, bg=row_btn["bg"])
                e_lbl.pack(side="left", padx=(0, 8))
                widgets.append(e_lbl)
            t_lbl = tk.Label(
                row_btn,
                text=cat_title,
                bg=row_btn["bg"],
                fg=GOLD if selected else FG,
                font=("TkDefaultFont", 10, "bold" if selected else "normal"),
                anchor="w",
            )
            t_lbl.pack(side="left", fill="x", expand=True)
            cnt_lbl = tk.Label(
                row_btn,
                text=count_text,
                bg=row_btn["bg"],
                fg=MUTED,
                font=("TkDefaultFont", 9),
            )
            cnt_lbl.pack(side="right")
            widgets.extend((t_lbl, cnt_lbl))

            def pick_cat(_e=None, title=cat_title):
                search_var.set("")
                variables["campaign_category"].set(title)
                render_categories()
                render_campaigns()

            for w in widgets:
                w.bind("<Button-1>", pick_cat)

    def refresh_catalog_ui():
        reload_catalog_index()
        valid_titles = {BUILTIN_CATEGORY, *category_id_to_title.values()}
        if variables["campaign_category"].get() not in valid_titles:
            variables["campaign_category"].set(BUILTIN_CATEGORY)
        render_categories()
        render_campaigns()

    search_var.trace_add("write", lambda *_args: render_campaigns())

    # ---- Execution, Logging & Lifecycle ----
    def append(text):
        output.configure(state="normal")
        output.insert("end", ansi.sub("", text))
        lines = int(output.index("end-1c").split(".")[0])
        if lines > 3000:
            output.delete("1.0", f"{lines - 3000}.0")
        output.see("end")
        output.configure(state="disabled")

    def current_options():
        values: dict[str, Any] = {}
        for field in fields(Options):
            value = variables[field.name].get()
            if isinstance(getattr(Options(), field.name), int) and not isinstance(value, bool):
                value = int(value)
            values[field.name] = value
        return Options(**values)

    def preview():
        try:
            plan = build_plan(current_options(), "game")
            append("\n命令预览(未执行):\n" + "\n".join(command_text(cmd) for cmd in plan) + "\n")
            show_advanced(0)
        except (ValueError, tk.TclError) as error:
            messagebox.showerror("参数无效", str(error), parent=window)

    def save():
        try:
            data = asdict(current_options())
            settings_path.parent.mkdir(parents=True, exist_ok=True)
            temporary = settings_path.with_suffix(".tmp")
            temporary.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
            temporary.replace(settings_path)
            status.set(f"设置已保存:{settings_path}")
        except (ValueError, OSError, tk.TclError) as error:
            messagebox.showerror("保存失败", str(error), parent=window)

    def publish(process):
        events.put(("process", process))

    def launch_custom(plan: list[list[str]], refresh_after: bool = False):
        nonlocal running, log_path
        if running:
            return
        try:
            log_dir = ROOT / "logs/launcher"
            log_dir.mkdir(parents=True, exist_ok=True)
            log_path = log_dir / f"{datetime.now():%Y%m%d_%H%M%S_%f}.log"
        except OSError as error:
            messagebox.showerror("无法启动", str(error), parent=window)
            return
        stop.clear()
        running = True
        play_btn.configure(state="disabled")
        stop_btn.configure(state="normal")
        build_btn.state(["disabled"])
        test_run_btn.state(["disabled"])
        conv_run_btn.state(["disabled"])
        camp_conv_btn.state(["disabled"])
        status.set(f"运行中 · 日志:{log_path.name}")
        current_log = log_path

        def worker():
            code = 1
            try:
                with current_log.open("w", encoding="utf-8") as log:

                    def emit(text):
                        log.write(text)
                        log.flush()
                        events.put(("text", text))

                    code = run_plan(plan, emit, stop, publish)
            except (OSError, ValueError) as error:
                events.put(("text", f"\n启动失败:{error}\n"))
            finally:
                events.put(("done", (code, refresh_after)))

        threading.Thread(target=worker, daemon=True).start()

    def launch(task="game"):
        try:
            plan = build_plan(current_options(), task)
        except (ValueError, tk.TclError) as error:
            messagebox.showerror("无法启动", str(error), parent=window)
            return
        launch_custom(plan, refresh_after=False)

    def request_stop():
        stop.set()
        if active is not None:
            try:
                stop_process(active)
                process = active

                def force_stop():
                    if active is process:
                        stop_process(process, force=True)

                window.after(2000, force_stop)
            except OSError as error:
                append(f"停止失败:{error}\n")
        status.set("正在停止…")

    def send_input(_event=None):
        if active is not None and active.stdin is not None:
            try:
                active.stdin.write((input_text.get() + "\n").encode("utf-8"))
                active.stdin.flush()
                input_text.delete(0, "end")
            except (OSError, ValueError) as error:
                append(f"输入失败:{error}\n")

    def open_logs():
        folder = ROOT / "logs/launcher"
        folder.mkdir(parents=True, exist_ok=True)
        try:
            if os.name == "nt":
                os.startfile(folder)
            else:
                subprocess.Popen(["open" if sys.platform == "darwin" else "xdg-open", str(folder)])
        except OSError as error:
            messagebox.showerror("打开失败", str(error), parent=window)

    def poll():
        nonlocal active, running
        for _ in range(300):
            try:
                kind, value = events.get_nowait()
            except queue.Empty:
                break
            if kind == "text":
                append(value)
            elif kind == "process":
                active = value
                if active is not None and stop.is_set():
                    request_stop()
            elif kind == "done":
                code, refresh_after = value
                running = False
                active = None
                play_btn.configure(state="normal")
                stop_btn.configure(state="disabled")
                build_btn.state(["!disabled"])
                test_run_btn.state(["!disabled"])
                conv_run_btn.state(["!disabled"])
                camp_conv_btn.state(["!disabled"])
                result = "已停止" if stop.is_set() else "完成" if code == 0 else "失败"
                status.set(f"{result} · 退出码 {code} · {log_path.name}")
                append(f"\n{result},退出码 {code}。完整日志:{log_path}\n")
                if refresh_after and code == 0:
                    refresh_catalog_ui()
                if closing:
                    window.destroy()
                    return
        window.after(80, poll)

    def close():
        nonlocal closing
        if not running:
            window.destroy()
        elif messagebox.askyesno("任务仍在运行", STOP_HINT, parent=window):
            closing = True
            request_stop()

    # Wire remaining buttons
    tk.Button(
        header_btns,
        text="🔄 刷新战役",
        command=refresh_catalog_ui,
        bg=CARD,
        fg=FG,
        activebackground=CARD_SELECTED,
        activeforeground=GOLD,
        bd=0,
        padx=10,
        pady=5,
        cursor="hand2",
    ).pack(side="left", padx=(0, 8))
    tk.Button(
        header_btns,
        text="⚙ 高级选项与工具",
        command=lambda: show_advanced(),
        bg=CARD,
        fg=GOLD,
        activebackground=CARD_SELECTED,
        activeforeground=GOLD,
        bd=0,
        padx=12,
        pady=5,
        cursor="hand2",
    ).pack(side="left")

    tk.Button(
        btn_bar,
        text="⚙ 高级选项",
        command=lambda: show_advanced(),
        bg=CARD,
        fg=FG,
        activebackground=CARD_SELECTED,
        activeforeground=GOLD,
        bd=0,
        padx=12,
        pady=8,
        cursor="hand2",
    ).pack(side="left", padx=4)
    stop_btn = tk.Button(
        btn_bar,
        text="⏹ 停止",
        command=request_stop,
        state="disabled",
        bg=CARD,
        fg=RED,
        disabledforeground=MUTED,
        bd=0,
        padx=12,
        pady=8,
    )
    stop_btn.pack(side="left", padx=4)
    play_btn = tk.Button(
        btn_bar,
        text="▶  开始战役",
        command=lambda: launch("game"),
        bg=GOLD,
        fg="#161920",
        activebackground=GOLD_DARK,
        activeforeground="#161920",
        font=("TkDefaultFont", 12, "bold"),
        bd=0,
        padx=22,
        pady=8,
        cursor="hand2",
    )
    play_btn.pack(side="left", padx=(6, 0))

    ttk.Button(log_frame, text="发送输入", command=send_input).grid(row=1, column=1, padx=(6, 0), pady=(6, 0))
    input_text.bind("<Return>", send_input)
    ttk.Button(adv_actions, text="打开日志目录", command=open_logs).pack(side="right", padx=3)
    ttk.Button(adv_actions, text="保存设置", command=save).pack(side="right", padx=3)
    ttk.Button(adv_actions, text="预览命令", command=preview).pack(side="right", padx=3)
    build_btn = ttk.Button(adv_actions, text="仅构建引擎", command=lambda: launch("build"))
    build_btn.pack(side="right", padx=3)

    refresh_catalog_ui()
    window.protocol("WM_DELETE_WINDOW", close)
    window.after(80, poll)
    return window


def main() -> int:
    """Open the launcher without importing or initializing the game engine."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--settings", type=Path, default=SETTINGS, help="launcher preferences JSON path")
    args = parser.parse_args()
    try:
        window = create_window(args.settings)
    except (ImportError, OSError, ValueError) as error:
        parser.exit(1, f"无法打开启动器:{error}\n需要带 Tk 支持的 Python;原有 bin/run 命令仍可直接使用。\n")
    window.mainloop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
