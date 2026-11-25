# Copyright 2026+ the openage authors. See copying.md for legal info.

"""Isolated, deterministically compared Wallace snapshot cases."""

import argparse
import json
import os
import re
import subprocess
from pathlib import Path
from tempfile import TemporaryDirectory
from time import perf_counter, sleep

from ..log import info
from .snapshot import assert_snapshot, check_golden
from .testing import TestError

JOBS = 1
SHOW_TIMINGS = False


def _campaign_metadata(root: Path) -> dict[Path, bytes]:
    """Only metadata is consumed by the headless scenario loader."""
    return {
        path.relative_to(root): path.read_bytes()
        for path in sorted(root.rglob("*"))
        if path.is_file() and path.suffix in (".nyan", ".toml", ".json", ".csv")
    }


def _freeze_campaign(
    target: Path, mission: int, source_file: str | None = None, variant: str = "original"
) -> None:
    from ..assets import get_asset_path

    source = get_asset_path(None)["converted"].resolve_native_path()
    if source is None:
        raise TestError("Wallace golden tests require a native converted ww_mvp modpack")
    root = Path(source)
    metadata = _campaign_metadata(root)
    scenario = json.loads(metadata[Path("ww_mvp/campaign/william_wallace/scenario.json")])
    definition = scenario["ScenarioDefinition"]
    installed = definition["original_scenario"]["mission"]
    expected_source = source_file or f"1_Wallace_{mission}.aoe2scenario"
    if (
        definition.get("source_file") != expected_source
        or definition["original_scenario"].get("ai_test_variant", "original") != variant
    ):
        raise TestError(f"Installed campaign source does not match {expected_source}")
    if installed != mission:
        raise TestError(f"Installed Wallace mission is {installed}; prepare Mission {mission} first")
    for relative, data in metadata.items():
        output = target / "converted" / relative
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(data)
    if metadata != _campaign_metadata(root):
        raise TestError(
            "Campaign assets changed while preparing golden tests; rerun after conversion finishes"
        )


def case_worker(argv: list[str]) -> None:
    """Execute one case in a fresh engine process; only the parent compares baselines."""
    # Cython provides this module in the build tree.
    from ..gamestate.tests import run_wallace_golden_case  # ty: ignore[unresolved-import]

    parser = argparse.ArgumentParser(description=case_worker.__doc__)
    parser.add_argument("case", type=Path)
    parser.add_argument("assets", type=Path)
    parser.add_argument("result", type=Path)
    args = parser.parse_args(argv)
    started = perf_counter()
    scenario_text = args.case.read_text(encoding="utf-8")
    scenario = json.loads(scenario_text)
    snapshot, stages = run_wallace_golden_case(scenario_text, str(args.assets), with_timings=True)
    if "ai" in scenario:
        operations = [line for line in snapshot.splitlines() if line.startswith("AI ms=")]
        profiles = [line for line in snapshot.splitlines() if line.startswith("AI_PROFILE ")]
        if any("dropped=0" not in line for line in profiles):
            raise TestError("AI replay requires a complete operation trace")
        expected = scenario.get("ai_expect", {})
        if len(operations) < expected.get("min_operations", 0) or len(operations) > expected.get(
            "max_operations", 100000
        ):
            raise TestError(f"Unexpected AI operation count: {len(operations)}")
        kinds = {line.split()[5] for line in operations}
        if not set(expected.get("required_kinds", [])).issubset(kinds):
            raise TestError(f"Missing required AI behavior; observed {sorted(kinds)}")
        rows = [line for line in snapshot.splitlines() if line.startswith("AI_STATE ")]
        if rows and int(rows[-1].split("pop=", 1)[1].split("/", 1)[0]) < expected.get(
            "min_final_population", 0
        ):
            raise TestError("AI did not produce the required population")

        damaged = any(int(hp) < int(maximum) for hp, maximum in re.findall(r"hp=(\d+)/(\d+)", snapshot))
        if expected.get("combat_damage") and not damaged and "missing=1" not in snapshot:
            raise TestError("AI attack orders did not cause observable combat damage")
        limits = {}
        for line in profiles:
            fields = dict(word.split("=", 1) for word in line.split()[1:])
            limits[int(fields["player"])] = (int(fields["apm"]), int(fields["aps"]))
        submitted = {}
        for line in operations:
            fields = dict(word.split("=", 1) for word in line.split() if "=" in word)
            player, now = int(fields["player"]), int(fields["ms"])
            previous = submitted.setdefault(player, [])
            apm, aps = limits[player]
            if (
                sum(at > now - 1000 for at in previous) >= aps
                or sum(at > now - 60000 for at in previous) >= apm
            ):
                raise TestError(f"AI player {player} exceeded its shared action budget")
            previous.append(now)
        cap = scenario["ai"].get("population_limit")
        if cap is not None and any(int(line.split("pop=", 1)[1].split("/", 1)[0]) > cap for line in rows):
            raise TestError("AI population exceeded the authoritative match limit")
        replay = dict(scenario, ai_replay_operations=operations)
        replayed = run_wallace_golden_case(json.dumps(replay), str(args.assets))
        assert_snapshot(args.case.stem + "_operation_replay", snapshot, replayed, scenario["total_ticks"])
    args.result.write_text(
        json.dumps({"snapshot": snapshot, "stages": stages, "wall_seconds": perf_counter() - started}),
        encoding="utf-8",
    )


def _stop_workers(workers: dict[int, subprocess.Popen]) -> None:
    for process in workers.values():
        if process.poll() is None:
            process.terminate()
    for process in workers.values():
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def _run_cases(cases: list[Path], jobs: int, show_timings: bool, work: Path) -> None:
    from ..config import BUILD_BIN_DIR

    started = perf_counter()
    if jobs < 1:
        raise ValueError("Golden jobs must be positive")
    definitions = [json.loads(case.read_text(encoding="utf-8")) for case in cases]
    missions = {case["mission"] for case in definitions}
    sources = {
        (
            case.get("campaign_source", f"1_Wallace_{case['mission']}.aoe2scenario"),
            case.get("campaign_variant", "original"),
        )
        for case in definitions
    }
    if len(missions) != 1 or len(sources) != 1:
        raise ValueError("Prepare and run each Wallace mission separately")
    assets = work / "assets"
    if assets.exists():
        raise TestError("Use a fresh results directory; an assets snapshot already exists")
    _freeze_campaign(assets, missions.pop(), *sources.pop())
    asset_seconds = perf_counter() - started
    executable = Path(BUILD_BIN_DIR) / ("run.exe" if os.name == "nt" else "run")
    workers: dict[int, subprocess.Popen] = {}
    next_case = 0
    next_compare = 0
    completed: dict[int, int] = {}
    try:
        while next_compare < len(cases):
            while next_case < len(cases) and len(workers) < jobs:
                current = cases[next_case]
                with (work / f"{current.stem}.log").open("wb") as log:
                    workers[next_case] = subprocess.Popen(
                        [
                            str(executable),
                            "test",
                            "-qq",
                            "--demo",
                            "testing.wallace_golden.case_worker",
                            str(current),
                            str(assets),
                            str(work / f"{current.stem}.result.json"),
                        ],
                        stdout=log,
                        stderr=subprocess.STDOUT,
                    )
                next_case += 1
            for index, process in list(workers.items()):
                if process.poll() is not None:
                    completed[index] = process.wait()
                    del workers[index]
            while next_compare in completed:
                case = cases[next_compare]
                code = completed.pop(next_compare)
                if code:
                    lines = (
                        (work / f"{case.stem}.log").read_text(encoding="utf-8", errors="replace").splitlines()
                    )
                    context = "\n".join(line[:300] for line in lines[-12:])
                    raise TestError(f"{case.stem}: worker exited with code {code}\n{context}")
                result = json.loads((work / f"{case.stem}.result.json").read_text(encoding="utf-8"))
                if show_timings:
                    stages = " ".join(f"{name}={seconds:.3f}s" for name, seconds in result["stages"].items())
                    info("Golden %s: wall=%.3fs %s", case.stem, result["wall_seconds"], stages)
                check_golden(
                    case.with_suffix(".golden"), result["snapshot"], definitions[next_compare]["total_ticks"]
                )
                next_compare += 1
            if next_compare < len(cases):
                sleep(0.01)
    finally:
        _stop_workers(workers)
        if show_timings:
            info(
                "Golden batch: cases=%d jobs=%d elapsed=%.3fs assets=%.3fs",
                len(cases),
                min(jobs, len(cases)),
                perf_counter() - started,
                asset_seconds,
            )


def _run_suite(prefix: str) -> None:
    from ..config import BUILD_SRC_DIR

    fixtures = Path(BUILD_SRC_DIR) / "tests" / "golden" / "wallace"
    cases = sorted(fixtures.glob(f"{prefix}_*.json"))
    if not cases:
        raise TestError(f"No golden cases found for {prefix} in {fixtures}")
    with TemporaryDirectory(prefix="openage-golden-") as temporary:
        _run_cases(cases, JOBS, SHOW_TIMINGS, Path(temporary))


def run_cases(argv: list[str]) -> None:
    """Compare selected fixtures and optionally retain snapshots and stage timings."""
    from ..config import BUILD_SRC_DIR

    parser = argparse.ArgumentParser(description=run_cases.__doc__)
    parser.add_argument("--pattern", default="m1", help="case filename prefix, e.g. m1_06")
    parser.add_argument("--jobs", type=int, default=JOBS)
    parser.add_argument("--timings", action="store_true")
    parser.add_argument("--results-dir", type=Path)
    args = parser.parse_args(argv)
    fixtures = Path(BUILD_SRC_DIR) / "tests/golden/wallace"
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    cases = sorted(fixtures.glob(f"{args.pattern}*.json"))
    if not cases:
        parser.error("No matching golden cases")
    if args.results_dir:
        args.results_dir.mkdir(parents=True, exist_ok=True)
        _run_cases(cases, args.jobs, args.timings, args.results_dir)
    else:
        with TemporaryDirectory(prefix="openage-golden-") as temporary:
            _run_cases(cases, args.jobs, args.timings, Path(temporary))


def mission1_golden() -> None:
    """Run the Mission 1 fixtures in filename order."""
    _run_suite("m1")


def mission2_golden() -> None:
    """Run the Mission 2 fixtures in filename order."""
    _run_suite("m2")


def movement_golden() -> None:
    """Check stop/resume and split-army orders independently of campaign progression."""
    _run_suite("m1_06")
    _run_suite("m1_07")


def ai_mission1_golden() -> None:
    """Run decision, difficulty and disabled-controller cases on the original Mission 1 map."""
    _run_suite("ai_m1")


def ai_mission2_golden() -> None:
    """Run economic AI and operation replay on the original Mission 2 map."""
    _run_suite("ai_m2")


def ai_barbarossa_golden() -> None:
    """Exercise two computer players on the explicitly isolated original Barbarossa map."""
    _run_suite("ai_b1")
