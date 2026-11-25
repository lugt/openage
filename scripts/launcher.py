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


def create_window(settings_path: Path = SETTINGS):
    """Build the desktop UI; all Tk operations stay on the main thread."""
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk
    from tkinter.scrolledtext import ScrolledText

    options = load_options(settings_path)
    try:
        window = tk.Tk()
    except tk.TclError as error:
        raise ValueError(f"Tk 窗口初始化失败:{error}") from error
    window.title("openage · 项目启动器")
    window.geometry("1060x860")
    window.minsize(900, 760)
    window.columnconfigure(0, weight=1)
    window.rowconfigure(3, weight=1)
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

    def entry(parent, row, label, key, browse=None):
        ttk.Label(parent, text=label).grid(row=row, column=0, sticky="w", padx=6, pady=3)
        widget = ttk.Entry(parent, textvariable=variables[key])
        widget.grid(row=row, column=1, sticky="ew", padx=6, pady=3)
        if browse:

            def choose():
                path = (
                    filedialog.askdirectory(parent=window)
                    if browse == "dir"
                    else filedialog.askopenfilename(parent=window)
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

    common = ttk.LabelFrame(window, text="项目与公共设置", padding=8)
    common.grid(row=0, column=0, sticky="ew", padx=12, pady=(10, 4))
    common.columnconfigure(1, weight=1)
    entry(common, 0, "运行程序", "executable", "file")
    entry(common, 1, "构建目录", "build_dir", "dir")
    entry(common, 2, "资源根目录(留空用默认)", "asset_dir", "dir")
    entry(common, 3, "配置目录(留空用默认)", "cfg_dir", "dir")
    toggles = ttk.Frame(common)
    toggles.grid(row=4, column=0, columnspan=3, sticky="w")
    checks(toggles, [("build_first", "运行前构建"), ("verbose", "详细日志")])

    tabs = ttk.Notebook(window)
    tabs.grid(row=1, column=0, sticky="ew", padx=12, pady=4)
    pages = []
    for name in ("游戏 / Wallace 战役", "测试", "Converter"):
        page = ttk.Frame(tabs, padding=8)
        page.columnconfigure(1, weight=1)
        tabs.add(page, text=name)
        pages.append(page)
    game, tests, converter = pages

    def combo(parent, row, label, key, choices):
        ttk.Label(parent, text=label).grid(row=row, column=0, sticky="w", padx=6, pady=3)
        ttk.Combobox(parent, textvariable=variables[key], values=choices, state="readonly").grid(
            row=row, column=1, columnspan=2, sticky="ew", padx=6, pady=3
        )

    combo(game, 0, "启动内容", "game_mode", list(GAME_MODES))
    dimensions = ttk.Frame(game)
    dimensions.grid(row=1, column=0, columnspan=3, sticky="w", padx=6)
    for key, label in (("width", "宽"), ("height", "高")):
        ttk.Label(dimensions, text=label).pack(side="left", padx=4)
        ttk.Entry(dimensions, textvariable=variables[key], width=8).pack(side="left")
    combo(game, 2, "窗口模式", "window_mode", ["windowed", "borderless", "fullscreen"])
    entry(game, 3, "Modpacks(空格分隔,留空自动)", "modpacks")
    entry(game, 4, "原版存档(仅地形预览)", "source_save", "file")
    entry(game, 5, "原版 terrain/textures/2x", "terrain_dir", "dir")
    game_checks = ttk.Frame(game)
    game_checks.grid(row=6, column=0, columnspan=3, sticky="w")
    checks(
        game_checks,
        [
            ("vsync", "VSync"),
            ("gl_debug", "OpenGL 调试"),
            ("headless", "项目无头模式"),
            ("ignore_user_input", "自动验证: 仅响应 Esc"),
        ],
    )
    ttk.Label(
        game,
        text="Wallace 第 1/2 关会准备场景;使用默认程序及资源目录。游戏只加载资产,不自动转换。",
    ).grid(row=7, column=0, columnspan=3, sticky="w", padx=6, pady=3)

    combo(tests, 0, "测试套件", "suite", TEST_SUITES)
    entry(tests, 1, "指定测试 / 前缀", "test_name")
    entry(tests, 2, "模拟演示 ID", "demo_id")
    entry(tests, 3, "Golden 并行用例数", "golden_jobs")
    test_checks = ttk.Frame(tests)
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
        tests,
        text="综合测试 = 自动测试 + Wallace 回归,首个失败即停止。Wallace 需要已转换资产。",
    ).grid(row=5, column=0, columnspan=3, sticky="w", padx=6, pady=8)

    entry(converter, 0, "原版游戏安装目录", "source_dir", "dir")
    entry(converter, 1, "输出资源根目录(写入 converted/)", "output_dir", "dir")
    entry(converter, 2, "并发数", "jobs")
    combo(converter, 3, "PNG 压缩级别", "compression", list(range(5)))
    convert_checks = ttk.Frame(converter)
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
    ttk.Label(
        converter,
        text="转换会更新输出目录。需要交互时可在下方发送输入。",
    ).grid(row=5, column=0, columnspan=3, sticky="w", padx=6, pady=6)

    actions = ttk.Frame(window, padding=(12, 4))
    actions.grid(row=2, column=0, sticky="ew")
    status = tk.StringVar(value="就绪")
    ttk.Label(actions, textvariable=status).pack(side="left", fill="x", expand=True)
    output = ScrolledText(window, height=14, wrap="word", state="disabled", font="TkFixedFont")
    output.grid(row=3, column=0, sticky="nsew", padx=12, pady=4)
    input_bar = ttk.Frame(window, padding=(12, 4))
    input_bar.grid(row=4, column=0, sticky="ew")
    input_bar.columnconfigure(0, weight=1)
    input_text = ttk.Entry(input_bar)
    input_text.grid(row=0, column=0, sticky="ew")

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

    def task_name():
        return ("game", "tests", "convert")[tabs.index(tabs.select())]

    def preview():
        try:
            plan = build_plan(current_options(), task_name())
            append("\n命令预览(未执行):\n" + "\n".join(command_text(cmd) for cmd in plan) + "\n")
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

    def launch(task=None):
        nonlocal running, log_path
        if running:
            return
        try:
            plan = build_plan(current_options(), task or task_name())
            log_dir = ROOT / "logs/launcher"
            log_dir.mkdir(parents=True, exist_ok=True)
            log_path = log_dir / f"{datetime.now():%Y%m%d_%H%M%S_%f}.log"
        except (ValueError, OSError, tk.TclError) as error:
            messagebox.showerror("无法启动", str(error), parent=window)
            return
        stop.clear()
        running = True
        run_button.state(["disabled"])
        build_button.state(["disabled"])
        stop_button.state(["!disabled"])
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
                events.put(("done", code))

        threading.Thread(target=worker, daemon=True).start()

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
                running = False
                active = None
                run_button.state(["!disabled"])
                build_button.state(["!disabled"])
                stop_button.state(["disabled"])
                result = "已停止" if stop.is_set() else "完成" if value == 0 else "失败"
                status.set(f"{result} · 退出码 {value} · {log_path.name}")
                append(f"\n{result},退出码 {value}。完整日志:{log_path}\n")
                if closing:
                    window.destroy()
                    return
        window.after(80, poll)

    def close():
        nonlocal closing
        if not running:
            window.destroy()
        elif messagebox.askyesno("任务仍在运行", "停止当前任务并关闭启动器?", parent=window):
            closing = True
            request_stop()

    ttk.Button(input_bar, text="发送输入", command=send_input).grid(row=0, column=1, padx=6)
    input_text.bind("<Return>", send_input)
    ttk.Button(actions, text="打开日志", command=open_logs).pack(side="right", padx=3)
    ttk.Button(actions, text="保存设置", command=save).pack(side="right", padx=3)
    ttk.Button(actions, text="预览命令", command=preview).pack(side="right", padx=3)
    stop_button = ttk.Button(actions, text="停止", command=request_stop, state="disabled")
    stop_button.pack(side="right", padx=3)
    run_button = ttk.Button(actions, text="运行当前页", command=launch)
    run_button.pack(side="right", padx=3)
    build_button = ttk.Button(actions, text="仅构建", command=lambda: launch("build"))
    build_button.pack(side="right", padx=3)
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
