#!/usr/bin/env python3
# Copyright 2026+ the openage authors. See copying.md for legal info.
"""Convert all convertible AoE2DE campaigns (including DLCs and Art of War) into Mission Packs."""

from __future__ import annotations

import argparse
from concurrent.futures import ProcessPoolExecutor, as_completed
from dataclasses import replace
import hashlib
import json
import multiprocessing
import os
from pathlib import Path
import re
import shutil
import sys
import traceback

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "bin"))
sys.path.insert(0, str(ROOT / "assets/nyan/ww_mvp"))

from openage.scenario.de2_campaign import campaign_entries, extract_scenario, unpack_campaign
from openage.scenario.de2_scenario import read_scenario
from openage.scenario.ir import ScenarioDefinition
from openage.scenario.writer import write_mission_pack
import original_scene
from scripts.tech_tree.catalog import strings_at

# Ordered category definitions: (category_id, category_title)
CATEGORIES: tuple[tuple[str, str], ...] = (
    ("art_of_war", "孙子兵法 · The Art of War"),
    ("age_of_kings", "帝王时代 · Age of Kings"),
    ("conquerors", "征服者 · The Conquerors"),
    ("forgotten", "失落帝国 · The Forgotten"),
    ("historical_battles", "历史战役 · Historical Battles"),
    ("african_kingdoms", "非洲王国 · African Kingdoms"),
    ("rise_of_the_rajas", "蛮王崛起 · Rise of the Rajas"),
    ("last_khans", "最后的可汗 · The Last Khans"),
    ("lords_of_the_west", "西方霸主 · Lords of the West"),
    ("dawn_of_the_dukes", "公爵的崛起 · Dawn of the Dukes"),
    ("dynasties_of_india", "印度王朝 · Dynasties of India"),
    ("coop", "合作战役 · Co-op Campaigns"),
    ("events", "活动与额外剧本 · Events & Bonus"),
)

# Campaign container stem -> (category_id, campaign_title)
CAMPAIGN_METADATA: dict[str, tuple[str, str]] = {
    "cam0": ("art_of_war", "孙子兵法 (挑战任务)"),
    "cam1": ("age_of_kings", "威廉·华莱士 (William Wallace)"),
    "cam2": ("age_of_kings", "圣女贞德 (Joan of Arc)"),
    "cam3": ("age_of_kings", "萨拉丁 (Saladin)"),
    "cam4": ("age_of_kings", "成吉思汗 (Genghis Khan)"),
    "cam5": ("age_of_kings", "巴巴罗萨 (Barbarossa)"),
    "xcam1": ("conquerors", "阿提拉 (Attila the Hun)"),
    "xcam2": ("conquerors", "熙德 (El Cid)"),
    "xcam3": ("conquerors", "蒙特祖玛 (Montezuma)"),
    "fcam1": ("forgotten", "阿拉里克 (Alaric)"),
    "fcam2": ("forgotten", "德古拉 (Dracula)"),
    "fcam3": ("forgotten", "巴里 (Bari)"),
    "fcam4": ("forgotten", "斯福尔扎 (Sforza)"),
    "fcam5": ("forgotten", "帕查库提 (Pachacuti)"),
    "fcam6": ("forgotten", "毗罗阎 (Prithviraj)"),
    "fcam7": ("historical_battles", "历史战役 (Historical Battles)"),
    "acam1": ("african_kingdoms", "塔里克·伊本·齐亚德 (Tariq ibn Ziyad)"),
    "acam2": ("african_kingdoms", "松迪亚塔 (Sundjata)"),
    "acam3": ("african_kingdoms", "弗朗西斯科·德·阿尔梅达 (Francisco de Almeida)"),
    "acam4": ("african_kingdoms", "游娣特 (Yodit)"),
    "rcam1": ("rise_of_the_rajas", "加查·马达 (Gajah Mada)"),
    "rcam2": ("rise_of_the_rajas", "苏利耶跋摩一世 (Suryavarman I)"),
    "rcam3": ("rise_of_the_rajas", "勃印曩 (Bayinnaung)"),
    "rcam4": ("rise_of_the_rajas", "黎利 (Le Loi)"),
    "kcam1": ("last_khans", "帖木儿 (Tamerlane)"),
    "kcam2": ("last_khans", "伊瓦依洛 (Ivaylo)"),
    "kcam3": ("last_khans", "霍屯 (Kotyan Khan)"),
    "wecam1": ("lords_of_the_west", "长腿爱德华 (Edward Longshanks)"),
    "wecam2": ("lords_of_the_west", "西方大公爵 (The Grand Dukes of the West)"),
    "wecam3": ("lords_of_the_west", "欧特维尔家族 (The Hautevilles)"),
    "eecam1": ("dawn_of_the_dukes", "阿基尔达斯与科斯图提斯 (Algirdas and Kestutis)"),
    "eecam2": ("dawn_of_the_dukes", "雅德维加 (Jadwiga)"),
    "eecam3": ("dawn_of_the_dukes", "扬·杰式卡 (Jan Zizka)"),
    "incam1": ("dynasties_of_india", "巴布尔 (Babur)"),
    "incam2": ("dynasties_of_india", "拉金德拉 (Rajendra)"),
    "incam3": ("dynasties_of_india", "提婆波罗 (Devapala)"),
    "coop3": ("coop", "合作 · 萨拉丁 (Saladin CO-OP)"),
    "coop6": ("coop", "合作 · 阿提拉 (Attila CO-OP)"),
    "coop9": ("coop", "合作 · 历史战役 (Historical Battles CO-OP)"),
    "coop10": ("coop", "合作 · 阿拉里克 (Alaric CO-OP)"),
    "coop17": ("coop", "合作 · 塔里克 (Tariq CO-OP)"),
    "coop22": ("coop", "合作 · 苏利耶跋摩一世 (Suryavarman I CO-OP)"),
    "coop25": ("coop", "合作 · 帖木儿 (Tamerlane CO-OP)"),
    "dcam0": ("events", "官方活动剧本 (Event Scenarios)"),
    "e3_bonus": ("events", "E3 演示剧本 (0_E3_Scenario)"),
}

ART_OF_WAR_STRING_IDS: dict[str, int] = {
    f"0_Challenges_{i}.aoe2scenario": 34000 + i for i in range(1, 10)
}


def _slugify(name: str) -> str:
    stem = name.removesuffix(".aoe2scenario")
    slug = re.sub(r"[^A-Za-z0-9._-]+", "_", stem).strip("_")
    return slug or "mission"


def _parse_scenario_worker(args: tuple[str, str, str]) -> tuple[str, str, str | None, str | None]:
    """Parse one .aoe2scenario in a clean worker process (avoids ASP global version leak)."""
    scenario_path_str, json_path_str, install_dir_str = args
    scenario_path = Path(scenario_path_str)
    json_path = Path(json_path_str)
    install_dir = Path(install_dir_str)
    try:
        strings = strings_at(install_dir, "en") | strings_at(install_dir, "zh")
        data = read_scenario(scenario_path, strings)
        data["source_file"] = scenario_path.name
        data["source_sha256"] = hashlib.sha256(scenario_path.read_bytes()).hexdigest()
        json_path.parent.mkdir(parents=True, exist_ok=True)
        tmp = json_path.with_suffix(".tmp")
        tmp.write_text(json.dumps(data, ensure_ascii=False), encoding="utf-8")
        tmp.replace(json_path)
        return (scenario_path_str, json_path_str, None, None)
    except Exception as exc:
        return (scenario_path_str, json_path_str, f"{type(exc).__name__}: {exc}", traceback.format_exc())


def _mission_nyan_text() -> str:
    base = (ROOT / "assets/nyan/ww_mvp/campaign/william_wallace/mission_01.nyan").read_text(encoding="utf-8")
    nyan = base.replace("!version 0.2.0", "!version 0.2.0\n\nimport de2_base.data.civ.celts.celts as celts", 1)
    nyan = nyan.replace("PlayerSetup(engine.util.setup.PlayerSetup):", "PlayerSetup(celts.Celts):")
    nyan = nyan.replace("    PlayerSetup.game_setup = o{}\n", "")
    return nyan


def _link_shared_media(mission_dir: Path, shared_dir: Path) -> None:
    for subdir in ("hud", "original_terrain_graphics", "original_entity_graphics"):
        target = mission_dir / subdir
        if target.is_symlink() or target.exists():
            if target.is_symlink():
                target.unlink()
            else:
                shutil.rmtree(target)
        rel = os.path.relpath(shared_dir / subdir, mission_dir)
        target.symlink_to(rel)


def _mission_title(filename: str, index: int, strings: dict[int, str]) -> str:
    if filename in ART_OF_WAR_STRING_IDS:
        sid = ART_OF_WAR_STRING_IDS[filename]
        zh_title = strings.get(sid, "").strip()
        if zh_title:
            return f"第 {index} 关 · {zh_title}"
    stem = filename.removesuffix(".aoe2scenario")
    return f"第 {index} 关 · {stem}"


def convert_all(
    install_dir: Path,
    executable: Path,
    output_root: Path,
    raw_cache_dir: Path,
    catalog_dir: Path,
    jobs: int,
    campaign_filter: set[str] | None = None,
    force_reparse: bool = False,
) -> dict:
    camp_dir = install_dir / "resources/_common/campaign"
    if not camp_dir.is_dir():
        raise FileNotFoundError(f"Campaign directory not found: {camp_dir}")

    de2_base_dir = ROOT / "assets/converted/de2_base"
    if not de2_base_dir.is_dir():
        raise FileNotFoundError(f"de2_base modpack not found at {de2_base_dir}")

    shared_dir = output_root / "_shared"
    for subdir in ("hud", "original_terrain_graphics", "original_entity_graphics"):
        (shared_dir / subdir).mkdir(parents=True, exist_ok=True)

    unpacked_dir = raw_cache_dir / "unpacked"
    scenarios_dir = raw_cache_dir / "scenarios"
    parsed_dir = raw_cache_dir / "parsed"
    unpacked_dir.mkdir(parents=True, exist_ok=True)
    scenarios_dir.mkdir(parents=True, exist_ok=True)
    parsed_dir.mkdir(parents=True, exist_ok=True)

    zh_strings = strings_at(install_dir, "zh")
    en_strings = strings_at(install_dir, "en")
    strings = en_strings | zh_strings

    # Discover campaign containers + standalone E3 scenario
    discovered: list[tuple[str, list[tuple[str, Path]]]] = []
    for path in sorted(camp_dir.glob("*.aoe2campaign*")):
        if path.name.endswith(".tmp"):
            continue
        cid = path.name.removesuffix(".gpv").removesuffix(".aoe2campaign")
        if campaign_filter and cid not in campaign_filter:
            continue
        unpacked = unpacked_dir / path.name.removesuffix(".gpv")
        if force_reparse or not unpacked.is_file():
            unpack_campaign(path, unpacked, executable)
        entries = campaign_entries(unpacked)
        mission_files: list[tuple[str, Path]] = []
        for entry in entries:
            sc_dest = scenarios_dir / cid / entry["name"]
            sc_dest.parent.mkdir(parents=True, exist_ok=True)
            if force_reparse or not sc_dest.is_file() or sc_dest.stat().st_size != entry["size"]:
                extract_scenario(unpacked, entry, sc_dest)
            mission_files.append((entry["name"], sc_dest))
        discovered.append((cid, mission_files))

    e3_path = camp_dir / "0_E3_Scenario.aoe2scenario"
    if e3_path.is_file() and (not campaign_filter or "e3_bonus" in campaign_filter):
        discovered.append(("e3_bonus", [("0_E3_Scenario.aoe2scenario", e3_path)]))

    # Step 1: Parse scenarios in isolated worker processes
    parse_tasks: list[tuple[str, str, str]] = []
    parse_errors: dict[tuple[str, str], str] = {}
    json_paths: dict[tuple[str, str], Path] = {}
    for cid, mission_files in discovered:
        for fname, sc_path in mission_files:
            slug = _slugify(fname)
            jpath = parsed_dir / cid / f"{slug}.json"
            json_paths[(cid, fname)] = jpath
            if force_reparse or not jpath.is_file():
                parse_tasks.append((str(sc_path), str(jpath), str(install_dir)))

    if parse_tasks:
        print(f"Parsing {len(parse_tasks)} scenarios using {jobs} worker(s)...", flush=True)
        ctx = multiprocessing.get_context("spawn")
        with ProcessPoolExecutor(max_workers=jobs, mp_context=ctx, max_tasks_per_child=1) as pool:
            futures = {pool.submit(_parse_scenario_worker, task): task for task in parse_tasks}
            done_count = 0
            for future in as_completed(futures):
                done_count += 1
                sc_str, j_str, err, _tb = future.result()
                sc_p = Path(sc_str)
                cid = sc_p.parent.name if sc_p.parent != camp_dir else "e3_bonus"
                if err:
                    parse_errors[(cid, sc_p.name)] = err
                    print(f"[{done_count}/{len(parse_tasks)}] PARSE FAIL {cid}/{sc_p.name}: {err}", flush=True)
                elif done_count % 25 == 0 or done_count == len(parse_tasks):
                    print(f"[{done_count}/{len(parse_tasks)}] Parsed {cid}/{sc_p.name}", flush=True)

    # Step 2: Convert parsed JSONs into openage Mission Packs sharing _shared/ media
    template = ScenarioDefinition.from_dump(ROOT / "docs/mvp/reports/mission1-scenario-dump")
    mission_nyan = _mission_nyan_text()
    campaigns_out: list[dict] = []
    total_missions = 0
    converted_missions = 0
    failed_missions = 0

    for cid, mission_files in discovered:
        cat_id, camp_title = CAMPAIGN_METADATA.get(cid, ("events", cid))
        missions_out: list[dict] = []
        for idx, (fname, _sc_path) in enumerate(mission_files, 1):
            total_missions += 1
            slug = _slugify(fname)
            jpath = json_paths[(cid, fname)]
            dest = output_root / cid / slug
            title = _mission_title(fname, idx, strings)
            if (cid, fname) in parse_errors:
                failed_missions += 1
                missions_out.append({
                    "id": slug,
                    "title": title,
                    "source_file": fname,
                    "status": "failed",
                    "error": parse_errors[(cid, fname)],
                })
                continue
            if not jpath.is_file():
                failed_missions += 1
                missions_out.append({
                    "id": slug,
                    "title": title,
                    "source_file": fname,
                    "status": "failed",
                    "error": "Parsed JSON missing",
                })
                continue
            try:
                if not force_reparse and (dest / "scenario.json").is_file() and (dest / "mission_01.nyan").is_file():
                    existing = json.loads((dest / "scenario.json").read_text(encoding="utf-8"))
                    metadata = existing.get("ScenarioDefinition", {}).get("original_scenario") or {}
                    if metadata.get("gameplay_metadata_version") == original_scene.GAMEPLAY_METADATA_VERSION:
                        _link_shared_media(dest, shared_dir)
                        converted_missions += 1
                        missions_out.append({
                            "id": slug,
                            "title": title,
                            "source_file": fname,
                            "status": "converted",
                            "path": f"{cid}/{slug}",
                            "width": existing.get("map", {}).get("width", 0),
                            "height": existing.get("map", {}).get("height", 0),
                            "objects": len(existing.get("placements", [])),
                            "triggers": len(existing.get("triggers", [])),
                        })
                        continue
                dest.mkdir(parents=True, exist_ok=True)
                ir = original_scene.original_scene(
                    template,
                    jpath,
                    install_dir,
                    dest,
                    catalog_dir,
                    de2_base_dir=de2_base_dir,
                    shared_media_dir=shared_dir,
                )
                (dest / "mission_01.nyan").write_text(mission_nyan, encoding="utf-8")
                write_mission_pack(ir, dest)
                _link_shared_media(dest, shared_dir)
                converted_missions += 1
                missions_out.append({
                    "id": slug,
                    "title": title,
                    "source_file": fname,
                    "status": "converted",
                    "path": f"{cid}/{slug}",
                    "width": ir.map.width,
                    "height": ir.map.height,
                    "objects": len(ir.placements),
                    "triggers": len(ir.triggers),
                })
                print(f"CONVERTED {cid}/{slug}: {len(ir.placements)} objs, {len(ir.triggers)} triggers", flush=True)
            except Exception as exc:
                failed_missions += 1
                err_msg = f"{type(exc).__name__}: {exc}"
                print(f"CONVERT FAIL {cid}/{slug}: {err_msg}", flush=True)
                missions_out.append({
                    "id": slug,
                    "title": title,
                    "source_file": fname,
                    "status": "failed",
                    "error": err_msg,
                })
        campaigns_out.append({
            "id": cid,
            "category": cat_id,
            "title": camp_title,
            "missions": missions_out,
        })

    manifest = {
        "version": 1,
        "categories": [{"id": cid, "title": title} for cid, title in CATEGORIES],
        "campaigns": campaigns_out,
        "summary": {
            "total": total_missions,
            "converted": converted_missions,
            "failed": failed_missions,
        },
    }
    manifest_path = output_root / "catalog.json"
    tmp = manifest_path.with_suffix(".tmp")
    tmp.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    tmp.replace(manifest_path)
    print(
        f"\nDone! Total={total_missions}, Converted={converted_missions}, Failed={failed_missions}. "
        f"Catalog: {manifest_path}",
        flush=True,
    )
    return manifest


def main() -> int:
    installation = Path.home() / "Library/Application Support/Steam/steamapps/common/AoE2DE"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original-dir", type=Path, default=installation / "AgeOfEmpires2Data")
    parser.add_argument(
        "--executable",
        type=Path,
        default=installation / "Age Of Empires II.app/Contents/MacOS/Age Of Empires II",
    )
    parser.add_argument("--output-dir", type=Path, default=ROOT / "assets/converted/campaigns")
    parser.add_argument("--cache-dir", type=Path, default=ROOT / "logs/campaigns")
    parser.add_argument("--catalog-dir", type=Path, default=ROOT / "logs/tech-trees")
    parser.add_argument("--jobs", type=int, default=max(1, min(6, os.cpu_count() or 2)))
    parser.add_argument("--campaigns", nargs="*", help="Optional subset of campaign IDs (e.g. cam0 cam1)")
    parser.add_argument("--force", action="store_true", help="Re-extract and re-parse all scenarios")
    args = parser.parse_args()
    convert_all(
        install_dir=args.original_dir,
        executable=args.executable,
        output_root=args.output_dir,
        raw_cache_dir=args.cache_dir,
        catalog_dir=args.catalog_dir,
        jobs=args.jobs,
        campaign_filter=set(args.campaigns) if args.campaigns else None,
        force_reparse=args.force,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
