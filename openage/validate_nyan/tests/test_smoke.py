# Copyright 2026 the openage authors. See copying.md for legal info.
"""
Smoke tests for the validate-nyan subcommand.

The validator must:
  - return 0 for a clean modpack,
  - return 1 for a modpack that contains a missing-FQON error,
  - return 2 for warnings in --strict mode,
  - never invoke the converter or simulation,
  - emit a JSON report when --json is given.
"""
import json
import pathlib

import pytest


def write_minimal_modpack(tmp_path: pathlib.Path) -> pathlib.Path:
    """Create a clean modpack with one tiny .nyan file."""
    (tmp_path / "manifest.json").write_text(
        json.dumps({"name": "smoke", "include": ["*.nyan"]})
    )
    (tmp_path / "a.nyan").write_text("ns Data.Test.X: pass\n")
    return tmp_path


def write_broken_modpack(tmp_path: pathlib.Path) -> pathlib.Path:
    """Create a modpack whose .nyan references a missing FQON."""
    (tmp_path / "manifest.json").write_text(
        json.dumps({"name": "broken", "include": ["*.nyan"]})
    )
    # The <Data.Bad.Missing> in a member-position type-argument is the
    # exact pattern that real bugs (e.g. ArsonResearchable) produce.
    (tmp_path / "broken.nyan").write_text(
        "import data.bad.u as u\n"
        "\n"
        "Data.Bad.U(civ.PlayerSetup):\n"
        "    PlayerSetup.name = <Data.Bad.Missing>\n"
    )
    return tmp_path


def test_clean_passes(tmp_path):
    from openage.validate_nyan.main import validate_nyan
    rc = validate_nyan([str(write_minimal_modpack(tmp_path))])
    assert rc == 0


def test_broken_returns_nonzero(tmp_path):
    from openage.validate_nyan.main import validate_nyan
    rc = validate_nyan([str(write_broken_modpack(tmp_path))])
    assert rc != 0


def test_json_report_written(tmp_path):
    from openage.validate_nyan.main import validate_nyan
    modpack = write_minimal_modpack(tmp_path)
    report_path = tmp_path / "report.json"
    rc = validate_nyan([str(modpack)], json_path=str(report_path))
    assert rc == 0
    payload = json.loads(report_path.read_text())
    assert payload["version"] == 1
    assert "errors" in payload
    assert "summary" in payload
