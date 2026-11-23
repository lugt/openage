#!/usr/bin/env python3
"""Batch-export a local AoE2 DE installation and build its technology preview."""

from argparse import ArgumentParser
from pathlib import Path
import importlib.util
import json
import sys
import time

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))
from tech_tree.catalog import Catalog, write_json
from tech_tree.reader import read_dat


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--original-dir", type=Path, default=Path.home() / "Library/Application Support/Steam/steamapps/common/AoE2DE/AgeOfEmpires2Data")
    parser.add_argument("--dat", type=Path, help="Override the local installation's DAT")
    parser.add_argument("--output", type=Path, default=ROOT / "logs/tech-trees")
    parser.add_argument("--audit-runtime", action="store_true", help="Also audit the installed native nyan modpack through bin/run demo 52")
    args = parser.parse_args()
    started = time.monotonic()
    if importlib.util.find_spec("genieutils") is None or importlib.util.find_spec("PIL") is None:
        parser.error("Install the pinned reader with python -m pip install -r scripts/requirements-tech-tree.txt")
    dat = args.dat or args.original_dir / "resources/_common/dat/empires2_x2_p1.dat"
    print(f"Reading {dat}", flush=True)
    args.output.mkdir(parents=True, exist_ok=True)
    with read_dat(dat) as (data, provenance):
        preview = Catalog(data, args.original_dir, args.output).export(provenance)
    template = (ROOT / "scripts/tech_tree/preview.html").read_text()
    embedded = json.dumps(preview, ensure_ascii=False, separators=(",", ":")).replace("<", "\\u003c")
    (args.output / "index.html").write_text(template.replace("/*__CATALOG__*/", embedded), encoding="utf-8")
    if args.audit_runtime:
        from tech_tree.runtime import audit_runtime
        print("Native runtime audit", audit_runtime(ROOT, args.output), flush=True)
    performance = {"elapsed_seconds": round(time.monotonic() - started, 2), "civilization_read_mode": "lazy-two-pass"}
    if sys.platform in ("darwin", "linux"):
        import resource
        peak = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
        performance["peak_rss_bytes"] = peak if sys.platform == "darwin" else peak * 1024
    write_json(args.output / "export-performance.json", performance)
    print(json.dumps(performance))
    print(json.dumps(preview["counts"], ensure_ascii=False))
    print(f"Preview {args.output.resolve() / 'index.html'}")


if __name__ == "__main__":
    main()
