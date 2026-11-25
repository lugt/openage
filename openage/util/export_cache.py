# Copyright 2026+ the openage authors. See copying.md for legal info.

"""Content validation for small sets of generated native files."""

import json
from hashlib import sha256
from pathlib import Path


class ExportCache:
    """Publish a cache stamp only after every generated output is complete."""

    def __init__(self, source: bytes, outputs: list[Path], version: bytes) -> None:
        self.outputs = outputs
        self.stamp = outputs[0].with_suffix(".export-cache.json")
        signature = sha256(source)
        signature.update(version)
        self.key = signature.hexdigest()

    def _hashes(self) -> dict[str, str]:
        return {path.name: sha256(path.read_bytes()).hexdigest() for path in self.outputs}

    def valid(self) -> bool:
        """Require matching input/version and all output contents."""
        try:
            cached = json.loads(self.stamp.read_text(encoding="utf-8"))
        except (FileNotFoundError, json.JSONDecodeError):
            return False
        return (
            isinstance(cached, dict)
            and cached.get("input") == self.key
            and all(path.is_file() for path in self.outputs)
            and cached.get("outputs") == self._hashes()
        )

    def commit(self) -> None:
        """Record output hashes after a successful conversion."""
        temporary = self.stamp.with_suffix(".tmp")
        temporary.write_text(
            json.dumps({"input": self.key, "outputs": self._hashes()}, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        temporary.replace(self.stamp)
