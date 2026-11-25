"""Start an openage campaign mission or inspect the original Mission 1 terrain."""

import json
import subprocess
import sys
from argparse import REMAINDER, ArgumentParser
from collections.abc import Sequence
from hashlib import file_digest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INSTALLER = ROOT / "assets/nyan/ww_mvp/install.sh"
GAME = ROOT / "scripts/run_ww_mission1.sh"
MODES = (
    ("marching", "第 1 关 · Marching and Fighting", "沿路行军，拆除前哨，再前往村庄击退英军。"),
    ("feeding", "第 2 关 · Feeding the Army", "让村民把食物、木材和黄金各运回 50 单位。"),
    ("training", "第 3 关 · Training the Troops", "建造两座房屋、一座兵营，并训练 4 名民兵。"),
    ("terrain", "原版地图预览 · Mission 1", "从本机 DE2 存档加载第 1 关的原版地形。"),
)


def locate_original():
    """Find the local Steam campaign save and original terrain textures."""
    steam = Path.home() / "Library/Application Support/Steam"
    saves = sorted((steam / "userdata").glob("*/813780/remote/WILLIAM-WALLACE-01.aoe2spgame"))
    common = steam / "steamapps/common/AoE2DE/AgeOfEmpires2Data/resources/_common"
    return (saves[0] if saves else None, common / "terrain/textures/2x", common / "particles")


def _fingerprints(paths: set[Path], previous: dict) -> dict:
    """Reuse content hashes while native file versions (size/mtime) are unchanged."""
    result = {}
    for path in sorted(paths):
        stat = path.stat()
        version = [stat.st_size, stat.st_mtime_ns, stat.st_ctime_ns]
        name = str(path.resolve())
        cached = previous.get(name, {})
        if (
            isinstance(cached, dict)
            and cached.get("version") == version
            and isinstance(cached.get("sha256"), str)
        ):
            digest = cached["sha256"]
        else:
            with path.open("rb") as source:
                digest = file_digest(source, "sha256").hexdigest()
        result[name] = {"version": version, "sha256": digest}
    return result


def _campaign_inputs(install_args: Sequence[str | Path]) -> set[Path]:
    paths = set()
    for directory in (ROOT / "assets/nyan/ww_mvp", ROOT / "openage/scenario", ROOT / "openage/convert"):
        paths.update(
            path
            for path in directory.rglob("*")
            if path.is_file() and path.suffix in (".py", ".pyx", ".nyan", ".toml", ".sh")
        )
    paths.add(Path(__file__))
    paths.add(ROOT / "openage/util/export_cache.py")
    paths.add(ROOT / "scripts/tech_tree/catalog.py")
    paths.update((ROOT / "logs/tech-trees/civilizations").glob("*.json"))
    paths.update(
        path
        for path in (ROOT / "bin/openage/convert").rglob("*")
        if path.is_file() and path.suffix in (".so", ".pyd")
    )
    for name in (
        "graphics.json.gz",
        "civilizations/00-gaia.json.gz",
        "technologies.json.gz",
        "globals.json.gz",
        "civilization_metadata.json.gz",
    ):
        path = ROOT / "logs/tech-trees/raw" / name
        if path.is_file():
            paths.add(path)
    for value in install_args:
        if str(value).startswith("--"):
            continue
        path = Path(value)
        if path.is_file():
            paths.add(path)
        elif path.is_dir():
            # The original install root contains unrelated audio/localizations;
            # only the source trees consumed by scene preparation belong here.
            directories = (
                [
                    path / "resources/_common/drs/graphics",
                    path / "resources/_common/terrain/textures/2x",
                    path / "resources/_common/wpfg/resources",
                    path / "widgetui/textures/ingame",
                ]
                if (path / "resources/_common").is_dir()
                else [path]
            )
            for directory in directories:
                paths.update(child for child in directory.rglob("*") if child.is_file())
    return paths


def _campaign_outputs(destination: Path) -> set[Path]:
    paths = {path for path in destination.rglob("*") if path.is_file()}
    # The installer also owns the explicit grass/road media in the base modpack.
    for name in ("grass", "road"):
        stem = ROOT / "assets/converted/de2_base/data/terrain" / name / "graphics" / f"texture_{name}"
        for extension in (".png", ".texture", ".terrain", ".export-cache.json"):
            path = stem.with_suffix(extension)
            if path.is_file():
                paths.add(path)
    return paths


def prepare_campaign(install_args: Sequence[str | Path]) -> None:
    """Prepare only when scene inputs, converter version or installed output changed."""
    stamp = ROOT / "assets/converted/.campaign-preparation.json"
    destination = ROOT / "assets/converted/ww_mvp"
    try:
        cached = json.loads(stamp.read_text(encoding="utf-8"))
    except (FileNotFoundError, json.JSONDecodeError):
        cached = {}
    if (
        not isinstance(cached, dict)
        or cached.get("version") != 1
        or not isinstance(cached.get("inputs"), dict)
        or not isinstance(cached.get("outputs"), dict)
    ):
        cached = {}
    print("Checking Wallace campaign preparation cache...", flush=True)
    arguments = [str(value) for value in install_args]
    inputs = _fingerprints(_campaign_inputs(install_args), cached.get("inputs", {}))
    outputs = _fingerprints(_campaign_outputs(destination), cached.get("outputs", {}))

    def hashes(files: dict) -> dict:
        return {name: entry.get("sha256") for name, entry in files.items() if isinstance(entry, dict)}

    def publish() -> None:
        temporary = stamp.with_suffix(".tmp")
        temporary.write_text(
            json.dumps(
                {"version": 1, "arguments": arguments, "inputs": inputs, "outputs": outputs}, sort_keys=True
            )
            + "\n",
            encoding="utf-8",
        )
        temporary.replace(stamp)

    if (
        cached.get("arguments") == arguments
        and hashes(inputs) == hashes(cached.get("inputs", {}))
        and outputs
        and hashes(outputs) == hashes(cached.get("outputs", {}))
    ):
        if cached["inputs"] != inputs or cached["outputs"] != outputs:
            publish()
        print("Reusing prepared Wallace campaign (inputs and output hashes match)", flush=True)
        return
    print("Preparing Wallace campaign (inputs or output changed)", flush=True)
    subprocess.run(["bash", INSTALLER, *install_args], cwd=ROOT, check=True)
    outputs = _fingerprints(_campaign_outputs(destination), {})
    if not outputs:
        raise ValueError("Campaign preparation produced no files")
    publish()


def run_mode(mode, source_save=None, terrain=None, game_args=()):
    """Prepare the selected Mission Pack and run the existing game launcher."""
    local_save, local_terrain, particles = locate_original()
    assets = terrain or local_terrain
    if mode == "terrain":
        source = source_save or local_save
        if not source or not source.is_file() or not assets.is_dir():
            raise ValueError("原版地图预览需要第 1 关 DE2 存档和原版 terrain/textures/2x 目录。")
        install_args = ["--original-map-save", source, assets]
    else:
        original_dir = particles.parents[2]
        mission_index = {"marching": 1, "feeding": 2, "training": 3}[mode]
        mission_json = ROOT / f"logs/wallace-original/mission-{mission_index}.json"
        catalog_ready = (ROOT / "logs/tech-trees/raw/graphics.json.gz").is_file()
        if original_dir.is_dir():
            if not catalog_ready:
                subprocess.run(
                    [sys.executable, ROOT / "scripts/export_tech_trees.py", "--original-dir", original_dir],
                    cwd=ROOT,
                    check=True,
                )
                catalog_ready = (ROOT / "logs/tech-trees/raw/graphics.json.gz").is_file()
            if not mission_json.is_file():
                subprocess.run(
                    [
                        sys.executable,
                        ROOT / "scripts/export_wallace_scenarios.py",
                        "--original-dir",
                        original_dir,
                    ],
                    cwd=ROOT,
                    check=True,
                )
        if mission_json.is_file() and original_dir.is_dir() and catalog_ready:
            install_args = ["--original-scenario", mission_json, original_dir]
        else:
            install_args = [f"--{mode}"]
        for flag, filename in (("--grass-dds", "g_grs.dds"), ("--road-dds", "g_rd1.dds")):
            texture = assets / filename
            if texture.is_file():
                install_args.extend((flag, texture))
        fisherman = particles.parent / "drs/graphics/u_vil_male_fisherman_taskA_x1.sld"
        if mode == "feeding" and fisherman.is_file():
            install_args.extend(("--shore-fish-sld", fisherman))
        if mode == "marching" and (particles / "textures/atlases/fire.dds").is_file():
            install_args.extend(("--fire-particles", particles))
    prepare_campaign(install_args)
    return subprocess.call(["bash", GAME, *game_args], cwd=ROOT)


def create_menu():
    """Create the desktop menu and keep it available while the game runs."""
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk

    window = tk.Tk()
    window.title("openage · William Wallace")
    window.resizable(False, False)
    panel = ttk.Frame(window, padding=24)
    panel.grid(sticky="nsew")
    ttk.Label(panel, text="William Wallace", font=("TkDefaultFont", 22, "bold")).grid(sticky="w")
    ttk.Label(panel, text="选择关卡或地图预览").grid(sticky="w", pady=(3, 18))
    selected = tk.StringVar(value=MODES[0][0])
    buttons = []
    for mode, title, description in MODES:
        item = ttk.Radiobutton(panel, text=title, value=mode, variable=selected)
        item.grid(sticky="w")
        buttons.append(item)
        ttk.Label(panel, text=description).grid(sticky="w", padx=(24, 0), pady=(2, 12))

    status = tk.StringVar()
    ttk.Label(panel, textvariable=status).grid(sticky="w", pady=(4, 0))

    def launch():
        mode = selected.get()
        args = [sys.executable, __file__, "--mode", mode]
        if mode == "terrain":
            source, terrain, _ = locate_original()
            if not source:
                source = filedialog.askopenfilename(
                    parent=window,
                    title="选择 William Wallace 第 1 关存档",
                    filetypes=[("AoE II DE", "*.aoe2spgame")],
                )
                if not source:
                    return
            if not terrain.is_dir():
                terrain = filedialog.askdirectory(parent=window, title="选择 DE2 terrain/textures/2x")
                if not terrain:
                    return
            args.extend(("--save", source, "--terrain-dds", terrain))

        log = ROOT / "logs/campaign_launcher.log"
        log.parent.mkdir(parents=True, exist_ok=True)
        with log.open("w", encoding="utf-8") as output:
            child = subprocess.Popen(args, cwd=ROOT, stdout=output, stderr=subprocess.STDOUT)
        for button in buttons:
            button.state(["disabled"])
        play.state(["disabled"])
        status.set("游戏正在启动或运行")

        def check():
            result = child.poll()
            if result is None:
                window.after(400, check)
                return
            for button in buttons:
                button.state(["!disabled"])
            play.state(["!disabled"])
            status.set("")
            if result != 0:
                messagebox.showerror("游戏启动失败", f"查看日志\n{log}", parent=window)

        window.after(400, check)

    play = ttk.Button(panel, text="启动游戏", command=launch)
    play.grid(sticky="e", pady=(6, 0))
    return window


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=[mode[0] for mode in MODES])
    parser.add_argument("--save", type=Path)
    parser.add_argument("--terrain-dds", type=Path)
    parser.add_argument(
        "--game-args", nargs=REMAINDER, default=[], help="arguments forwarded to the game launcher"
    )
    args = parser.parse_args()
    if args.mode:
        try:
            return run_mode(args.mode, args.save, args.terrain_dds, args.game_args)
        except (ValueError, subprocess.CalledProcessError) as error:
            parser.exit(1, f"{error}\n")
    try:
        window = create_menu()
    except ImportError:
        parser.exit(
            1,
            "Tk is unavailable. Run again with --mode marching, --mode feeding, --mode training, or --mode terrain.\n",
        )
    window.mainloop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
