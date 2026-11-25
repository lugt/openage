# Copyright 2026+ the openage authors. See copying.md for legal info.

"""Regression contracts for desktop launch routing and failure propagation."""

import importlib.util
import io
import os
import sys
import threading
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
from unittest.mock import patch

from .testing import assert_raises, assert_value


def launch_contracts() -> None:
    """Check argv boundaries, campaign forwarding and sequential failure behavior."""
    from ..config import BUILD_SRC_DIR

    source = Path(BUILD_SRC_DIR) / "scripts" / "launcher.py"
    spec = importlib.util.spec_from_file_location("openage_launcher_test", source)
    if spec is None or spec.loader is None:
        raise RuntimeError("Cannot load desktop launcher")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    try:
        spec.loader.exec_module(module)
        options = module.Options(source_dir="/original game/assets", output_dir="/converted assets")
        options.golden_jobs = 2
        options.golden_timings = True
        test_command = module.build_plan(options, "tests")[0]
        assert_value(test_command[test_command.index("--golden-jobs") + 1], "2")
        assert_value("--golden-timings" in test_command, True)
        converter = module.build_plan(options, "convert")[-1]
        assert_value("--force" in converter and "--no-prompts" in converter, True)
        assert_value(converter[converter.index("--source-dir") + 1], str(Path(options.source_dir).resolve()))
        assert_value(converter[converter.index("--output-dir") + 1], str(Path(options.output_dir).resolve()))
        assert_value(module.GAME_MODES[options.game_mode], "marching")
        options.ignore_user_input = True
        campaign = module.build_plan(options, "game")[-1]
        assert_value("--ignore-user-input" in campaign, True)
        assert_value(campaign[campaign.index("--game-args") + 1], "--window-size")
        from ..__main__ import main as cli_main

        with patch("openage.main.main.main", return_value=0) as launch_game:
            assert_value(
                cli_main(["main", "--scenario", "--window-size", "1280", "720", "--ignore-user-input"]), 0
            )
            assert_value(launch_game.call_args.args[0].window_size, [1280, 720])
            assert_value(launch_game.call_args.args[0].scenario, True)
            assert_value(launch_game.call_args.args[0].ignore_user_input, True)
        options.headless = True
        with assert_raises(ValueError):
            module.build_plan(options, "game")
        options.suite = module.TEST_SUITES[0]
        combined = module.build_plan(options, "tests")
        assert_value("--run-all-tests" in combined[0], True)
        assert_value(combined[-1][-1], "--regression")
        options.suite = module.TEST_SUITES[6]
        options.verbose = True
        demo = module.build_plan(options, "tests")[-1]
        assert_value("--verbose" in demo[demo.index("--demo") + 1 :], False)
        campaign_spec = importlib.util.spec_from_file_location(
            "openage_campaign_test", source.parent / "campaign_launcher.py"
        )
        if campaign_spec is None or campaign_spec.loader is None:
            raise RuntimeError("Cannot load campaign launcher")
        campaign_module = importlib.util.module_from_spec(campaign_spec)
        campaign_spec.loader.exec_module(campaign_module)
        original_root = Path("/original game/AgeOfEmpires2Data")
        common = original_root / "resources" / "_common"
        with (
            patch.object(
                campaign_module,
                "locate_original",
                return_value=(
                    None,
                    common / "terrain/textures/2x",
                    common / "particles",
                ),
            ),
            patch.object(Path, "is_file", return_value=True),
            patch.object(Path, "is_dir", return_value=True),
            patch.object(campaign_module, "prepare_campaign") as install,
            patch.object(campaign_module.subprocess, "call", return_value=0),
        ):
            campaign_module.run_mode("marching")
            install_args = install.call_args.args[0]
            install_root = install_args[install_args.index("--original-scenario") + 2]
            assert_value(install_root, original_root)
        with TemporaryDirectory(prefix="openage-campaign-cache-") as temporary:
            project = Path(temporary)
            templates = project / "assets/nyan/ww_mvp"
            templates.mkdir(parents=True)
            template = templates / "install.sh"
            template.write_text("initial installer", encoding="utf-8")
            utility = project / "openage/util/export_cache.py"
            utility.parent.mkdir(parents=True)
            utility.write_text("cache version", encoding="utf-8")
            icon_exporter = project / "scripts/tech_tree/catalog.py"
            icon_exporter.parent.mkdir(parents=True)
            icon_exporter.write_text("icon exporter version", encoding="utf-8")
            scenario_output = project / "assets/converted/ww_mvp/scenario.json"

            def install_campaign(*_args, **_kwargs):
                scenario_output.parent.mkdir(parents=True, exist_ok=True)
                scenario_output.write_text("prepared campaign", encoding="utf-8")

            with (
                patch.object(campaign_module, "ROOT", project),
                patch.object(campaign_module, "INSTALLER", template),
                patch.object(campaign_module.subprocess, "run", side_effect=install_campaign) as prepare,
            ):
                campaign_module.prepare_campaign(["--marching"])
                campaign_module.prepare_campaign(["--marching"])
                assert_value(prepare.call_count, 1)
                os.utime(template, ns=(0, 0))
                with patch.object(
                    campaign_module, "file_digest", wraps=campaign_module.file_digest
                ) as digest:
                    campaign_module.prepare_campaign(["--marching"])
                    calls = digest.call_count
                    assert_value(calls > 0, True)
                    campaign_module.prepare_campaign(["--marching"])
                    assert_value(digest.call_count, calls)
                assert_value(prepare.call_count, 1)
                template.write_text("updated installer version", encoding="utf-8")
                campaign_module.prepare_campaign(["--marching"])
                assert_value(prepare.call_count, 2)
                scenario_output.write_text("damaged", encoding="utf-8")
                campaign_module.prepare_campaign(["--marching"])
                assert_value(prepare.call_count, 3)
                campaign_module.prepare_campaign(["--feeding"])
                assert_value(prepare.call_count, 4)
        process = SimpleNamespace(
            stdout=io.BytesIO(b"first command failed\n"),
            stdin=io.BytesIO(),
            wait=lambda: 7,
            poll=lambda: 7,
        )
        output = []
        with patch.object(module.subprocess, "Popen", return_value=process) as spawn:
            result = module.run_plan(
                [["first"], ["never-run"]], output.append, threading.Event(), lambda _: None
            )
            assert_value(result, 7)
            assert_value(spawn.call_count, 1)
            assert_value("first command failed\n" in output, True)
    finally:
        sys.modules.pop(spec.name, None)


def parallel_golden_contracts() -> None:
    """Parallel cases preserve comparison order and terminate siblings on the first mismatch."""
    import json

    from . import snapshot, wallace_golden
    from .testing import TestError

    with TemporaryDirectory(prefix="openage-golden-contract-") as temporary:
        root = Path(temporary)
        cases = [root / "a.json", root / "b.json"]
        for case in cases:
            case.write_text(json.dumps({"mission": 1, "total_ticks": 5}), encoding="utf-8")
            case.with_suffix(".golden").write_text(case.stem + "\n", encoding="utf-8")
        processes = []
        mismatch = False
        require_refill = False

        class Worker:
            def __init__(self, command, stdout, stderr):
                self.done = False
                self.terminated = False
                self.position = len(processes)
                case = Path(command[-3])
                result = {
                    "snapshot": "wrong\n" if mismatch else case.stem + "\n",
                    "stages": {},
                    "wall_seconds": 0.0,
                }
                Path(command[-1]).write_text(json.dumps(result), encoding="utf-8")
                processes.append(self)

            def poll(self):
                if require_refill and self.position == 0 and len(processes) < 3:
                    return None
                if mismatch and self.position == 1 and not self.done:
                    return None
                self.done = True
                return 0

            def wait(self, timeout=None):
                if require_refill and self.position == 0 and len(processes) < 3:
                    raise TestError("An idle worker slot was not refilled behind the slow first case")
                self.done = True
                return 0

            def terminate(self):
                self.terminated = True
                self.done = True

            def kill(self):
                raise AssertionError("A terminated worker must be reaped without another kill")

        with (
            patch.object(wallace_golden, "_freeze_campaign"),
            patch.object(wallace_golden.subprocess, "Popen", Worker),
            patch.object(snapshot, "UPDATE_GOLDEN", False),
        ):
            for jobs in (1, 2):
                work = root / f"jobs-{jobs}"
                work.mkdir()
                wallace_golden._run_cases(cases, jobs, False, work)
            processes.clear()
            mismatch = True
            work = root / "mismatch"
            work.mkdir()
            try:
                wallace_golden._run_cases(cases, 2, False, work)
            except snapshot.SnapshotMismatchError as error:
                assert_value(str(error).startswith("a: line 1, Tick 5"), True)
                assert_value("Expected:" in str(error) and "Actual:" in str(error), True)
            else:
                raise TestError("Expected the first golden mismatch to stop the batch")
            assert_value(len(processes), 2)
            assert_value(processes[1].terminated, True)
            mismatch = False
            require_refill = True
            processes.clear()
            third = root / "c.json"
            third.write_text(json.dumps({"mission": 1, "total_ticks": 5}), encoding="utf-8")
            third.with_suffix(".golden").write_text("c\n", encoding="utf-8")
            work = root / "refill"
            work.mkdir()
            wallace_golden._run_cases([*cases, third], 2, False, work)
            assert_value(len(processes), 3)

        source = root / "source"
        scene = source / "ww_mvp/campaign/william_wallace/scenario.json"
        scene.parent.mkdir(parents=True)
        scene.write_text(
            json.dumps(
                {
                    "ScenarioDefinition": {
                        "source_file": "1_Wallace_1.aoe2scenario",
                        "original_scenario": {"mission": 1},
                    }
                }
            ),
            encoding="utf-8",
        )
        definition = source / "unit.nyan"
        definition.write_bytes(b"original")
        (source / "unused.png").write_bytes(b"media is not needed by the headless loader")

        class Assets:
            def __getitem__(self, _key):
                return self

            def resolve_native_path(self):
                return str(source)

        with patch("openage.assets.get_asset_path", return_value=Assets()):
            frozen = root / "frozen"
            wallace_golden._freeze_campaign(frozen, 1)
            definition.write_bytes(b"changed by converter")
            assert_value((frozen / "converted/unit.nyan").read_bytes(), b"original")
            assert_value((frozen / "converted/unused.png").exists(), False)
            with assert_raises(TestError):
                wallace_golden._freeze_campaign(root / "wrong-mission", 2)
