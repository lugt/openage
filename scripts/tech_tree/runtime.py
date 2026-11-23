"""Optional audit of the installed native nyan conversion using the game engine."""

import json
import os
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory

from .catalog import write_json


def audit_runtime(root, output):
    launcher = root / "bin/run"
    with TemporaryDirectory(prefix="openage-tech-runtime-") as temporary:
        destination = Path(temporary) / "native.json"
        environment = dict(os.environ, OPENAGE_TECH_TREE_OUTPUT=str(destination))
        log = output / "runtime-audit.log"
        with log.open("w") as stream:
            result = subprocess.run([str(launcher), "test", "-d", "gamestate.tests.simulation_demo", "52"],
                                    cwd=root, env=environment, stdout=stream, stderr=subprocess.STDOUT, check=False)
        text = log.read_text()
        if result.returncode or "DEMO 52: PASS" not in text or any(marker in text for marker in ("FATAL:", "uncaught exception", "SIGSEGV")):
            raise RuntimeError(f"Native civilization audit failed. Inspect {log}")
        data = json.loads(destination.read_text())
        if data["failures"]:
            raise RuntimeError("Some native civilization setup patches failed")
        write_json(output / "runtime-native.json.gz", data)
        summary = {
            "civilization_records": len(data["civilizations"]),
            "player_civilizations": sum(not c["gaia"] for c in data["civilizations"]),
            "nyan_objects": len(data["objects"]), "failures": data["failures"],
            "data": "runtime-native.json.gz",
        }
        write_json(output / "runtime-audit.json", summary)
        return summary
