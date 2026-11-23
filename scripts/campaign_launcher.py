"""Start an openage campaign mission or inspect the original Mission 1 terrain."""

from argparse import ArgumentParser
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parent.parent
INSTALLER = ROOT / "assets/nyan/ww_mvp/install.sh"
GAME = ROOT / "scripts/run_ww_mission1.sh"
MODES = (
    ("marching", "第 1 关 · Marching and Fighting", "沿路行军，拆除前哨，再前往村庄击退英军。"),
    ("feeding", "第 2 关 · Feeding the Army", "让村民把食物、木材和黄金各运回 50 单位。"),
    ("terrain", "原版地图预览 · Mission 1", "从本机 DE2 存档加载第 1 关的原版地形。"),
)


def locate_original():
    """Find the local Steam campaign save and original terrain textures."""
    steam = Path.home() / "Library/Application Support/Steam"
    saves = sorted((steam / "userdata").glob("*/813780/remote/WILLIAM-WALLACE-01.aoe2spgame"))
    common = steam / "steamapps/common/AoE2DE/AgeOfEmpires2Data/resources/_common"
    return (saves[0] if saves else None, common / "terrain/textures/2x", common / "particles")


def run_mode(mode, source_save=None, terrain=None):
    """Prepare the selected Mission Pack and run the existing game launcher."""
    local_save, local_terrain, particles = locate_original()
    assets = terrain or local_terrain
    if mode == "terrain":
        source = source_save or local_save
        if not source or not source.is_file() or not assets.is_dir():
            raise ValueError("原版地图预览需要第 1 关 DE2 存档和原版 terrain/textures/2x 目录。")
        install_args = ["--original-map-save", source, assets]
    else:
        original_dir = particles.parents[1]
        mission_index = 1 if mode == "marching" else 2
        mission_json = ROOT / f"logs/wallace-original/mission-{mission_index}.json"
        catalog_ready = (ROOT / "logs/tech-trees/raw/graphics.json.gz").is_file()
        if original_dir.is_dir():
            if not catalog_ready:
                subprocess.run([sys.executable, ROOT / "scripts/export_tech_trees.py", "--original-dir", original_dir], cwd=ROOT, check=True)
                catalog_ready = (ROOT / "logs/tech-trees/raw/graphics.json.gz").is_file()
            if not mission_json.is_file():
                subprocess.run([sys.executable, ROOT / "scripts/export_wallace_scenarios.py", "--original-dir", original_dir], cwd=ROOT, check=True)
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
    subprocess.run(["bash", INSTALLER, *install_args], cwd=ROOT, check=True)
    return subprocess.call(["bash", GAME], cwd=ROOT)


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
                    parent=window, title="选择 William Wallace 第 1 关存档",
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
    args = parser.parse_args()
    if args.mode:
        try:
            return run_mode(args.mode, args.save, args.terrain_dds)
        except (ValueError, subprocess.CalledProcessError) as error:
            parser.exit(1, f"{error}\n")
    try:
        window = create_menu()
    except ImportError:
        parser.exit(1, "Tk is unavailable. Run again with --mode marching, --mode feeding, or --mode terrain.\n")
    window.mainloop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
