#!/usr/bin/env python3
# Copyright 2026 the openage authors. See copying.md for legal info.

"""
Wwise HIRC -> wem_id mapping builder for AoE2 DE.

Builds a JSON file containing:
  {
    "wwise_sound_id": [wem_id, ...],
    ...
  }

so that workers in the convert pipeline can resolve
``graphic.wwise_sound_id`` -> wem_id without re-parsing .pck HIRC.

Reads .pck files under ``--pck-dir``, uses wwiser (bnnm) for HIRC parsing,
outputs JSON to ``--out``.

CLI:
    build_mapping.py --pck-dir DIR --out PATH [--refresh]

The mapping is cached on disk. Set OPENAGE_WWISE_MAP_PATH to override the
default location; the converter will look there before rebuilding.
"""
from __future__ import annotations

import argparse
import json
import os
import struct
import sys
import time
from pathlib import Path


# Reuse the extract.py module for pck parsing helpers
_HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(_HERE.parent))
from openage.convert.tool.wwise.extract import _read_pck_header, _iter_chunks  # noqa: E402


DEFAULT_MAP_PATH = Path("/tmp/openage_wwise_mapping.json")


# ----------------------------- pck sub-bank extraction ---------------------

def extract_sub_banks(pck_path: Path, out_dir: Path) -> list[Path]:
    """Extract each sub-bank from .pck as a standalone .bnk file.

    Sub-banks are bounded by BKHD chunks. Each .bnk is a sequence of
    {BKHD, DIDX, DATA, HIRC, INIT, STMG, ENVS, PLAT} chunks. We require
    BKHD first so wwiser can detect the version.
    """
    out_dir.mkdir(parents=True, exist_ok=True)
    with open(pck_path, "rb") as f:
        buf = f.read()
    body_start = _read_pck_header(buf)
    out_paths = []
    cur: list[tuple[bytes, int, int]] = []   # (tag, body_off, size)
    sub_idx = 0

    def flush():
        nonlocal sub_idx, cur
        if not cur:
            return
        bkhd = next((c for c in cur if c[0] == b"BKHD"), None)
        if bkhd is None:
            cur = []
            return
        ordered = [bkhd] + [c for c in cur if c[0] != b"BKHD"]
        body = b"".join(b"".join((c[0], struct.pack("<I", c[2]),
                                   buf[c[1]:c[1] + c[2]]))
                         for c in ordered)
        out = out_dir / f"{pck_path.stem}.{sub_idx:03d}.bnk"
        with open(out, "wb") as f:
            f.write(body)
        out_paths.append(out)
        sub_idx += 1
        cur = []

    off = body_start
    n = len(buf)
    while off + 8 <= n:
        tag = bytes(buf[off:off + 4])
        size = struct.unpack_from("<I", buf, off + 4)[0]
        body_off = off + 8
        end = body_off + size
        if tag == b"BKHD":
            flush()
        if tag in (b"BKHD", b"DIDX", b"DATA", b"HIRC", b"INIT", b"STMG",
                   b"ENVS", b"PLAT"):
            cur.append((tag, body_off, size))
        off = end
    flush()
    return out_paths


# ----------------------------- wwiser HIRC walk ---------------------------

# Lazily import wwiser (we don't want a hard dep at openage runtime).
_wwiser = None
def _get_wwiser_parser():
    global _wwiser
    if _wwiser is None:
        # Look for wwiser under <project_root>/.bin/wwiser/wwiser-master/
        # (created by running tar -xzf wwiser.tar.gz there). Override via env.
        env_dir = os.environ.get("OPENAGE_WWISER_DIR", "")
        candidates = [Path(env_dir)] if env_dir else []
        proj_root = Path(__file__).resolve().parents[4]   # openage/convert/tool/wwise → openage/convert/tool → openage/convert → openage → project_root
        candidates.append(proj_root / ".bin" / "wwiser" / "wwiser-master")
        candidates.append(Path("/tmp/wwiser-master"))
        for d in candidates:
            if d.is_dir() and (d / "wwiser").is_dir():
                sys.path.insert(0, str(d))
                try:
                    from wwiser.parser.wparser import Parser  # noqa: F401
                    from wwiser.parser import wmodel  # noqa: F401
                    _wwiser = (Parser, wmodel)
                    return _wwiser
                except ImportError:
                    continue
        raise RuntimeError(
            f"wwiser not found in {candidates}. Install it from "
            f"https://github.com/bnnm/wwiser into one of those dirs."
        )
    return _wwiser


def _walk_collect(node, target_name, out, wmodel):
    """Recursively collect nodes whose name == target_name."""
    if hasattr(node, "get_name") and node.get_name() == target_name:
        out.append(node)
    if hasattr(node, "get_children") and node.get_children():
        for c in node.get_children():
            _walk_collect(c, target_name, out, wmodel)


def _find_first_field(node, name):
    if isinstance(node, type(node)) and hasattr(node, "get_name") and node.get_name() == name:
        v = node.value() if callable(getattr(node, "value", None)) else node.value
        return v
    if hasattr(node, "get_children") and node.get_children():
        for c in node.get_children():
            r = _find_first_field(c, name)
            if r is not None:
                return r
    return None


def _find_first_field(node, name):
    if hasattr(node, "get_name") and node.get_name() == name:
        return node.value() if callable(getattr(node, "value", None)) else node.value
    if hasattr(node, "get_children") and node.get_children():
        for c in node.get_children():
            r = _find_first_field(c, name)
            if r is not None:
                return r
    return None


def _all_descendants(node):
    if hasattr(node, "get_children") and node.get_children():
        for c in node.get_children():
            yield c
            yield from _all_descendants(c)


def _extract_bank_data(bank_root, wmodel):
    events, actions, sounds, containers = {}, {}, {}, {}

    def classify(node):
        if not hasattr(node, "get_name"):
            return None
        n = node.get_name()
        if not n:
            return None
        if n == "CAkEvent":
            return "event"
        if n.startswith("CAkAction") and n != "CAkAction":
            return "action"
        if n == "CAkSound":
            return "sound"
        if n in ("CAkRanSeqCntr", "CAkActorMixer", "CAkSwitchCntr",
                 "CAkLayerCntr", "CAkMusicSegment", "CAkMusicTrack"):
            return "container"
        return None

    def walk_collect(node):
        kind = classify(node)
        if kind:
            ul_id = _find_first_field(node, "ulID")
            if ul_id is not None:
                yield kind, node, ul_id
        if hasattr(node, "get_children") and node.get_children():
            for c in node.get_children():
                yield from walk_collect(c)

    for kind, node, ul_id in walk_collect(bank_root):
        if kind == "event":
            actions_ids = []
            for sub in _all_descendants(node):
                if hasattr(sub, "get_name") and sub.get_name() == "Action":
                    aid = _find_first_field(sub, "ulActionID")
                    if aid is not None:
                        actions_ids.append(aid)
            events[ul_id] = actions_ids
        elif kind == "action":
            action_type = _find_first_field(node, "ulActionType")
            target = _find_first_field(node, "idExt")
            bank_id = None
            if action_type == 0x0403:
                bank_id = _find_first_field(node, "bankID")
                if bank_id is None:
                    bank_id = _find_first_field(node, "fileID")
            actions[ul_id] = {
                "type": action_type,
                "target_id": target,
                "bank_id": bank_id,
            }
        elif kind == "sound":
            source_id = _find_first_field(node, "sourceID")
            stream_type = _find_first_field(node, "StreamType")
            sounds[ul_id] = {"source_id": source_id, "stream_type": stream_type}
        elif kind == "container":
            children = []
            child_nodes = []
            _walk_collect(node, "Children", child_nodes, wmodel)
            for cn in child_nodes:
                for sub in _all_descendants(cn):
                    if hasattr(sub, "get_name") and sub.get_name() == "Child":
                        cid = _find_first_field(sub, "ulID")
                        if cid is not None:
                            children.append(cid)
            containers[ul_id] = {"type": "container", "children": children}

    return events, actions, sounds, containers


def _walk_container(cid, containers, sounds, out_wem_ids, depth=0):
    if depth > 8:
        return
    cn = containers.get(cid)
    if not cn:
        return
    for child in cn["children"]:
        if child in sounds:
            src = sounds[child].get("source_id")
            if src is not None:
                out_wem_ids.add(src)
        elif child in containers:
            _walk_container(child, containers, sounds, out_wem_ids, depth + 1)


def build_mapping(pck_dir: Path, bnk_dir: Path | None = None) -> dict:
    """Build {wwise_sound_id (event_ulID): [wem_id, ...]} for all .pck under pck_dir.

    Returns a dict suitable for JSON serialization (keys str-formatted).
    """
    Parser, wmodel = _get_wwiser_parser()
    if bnk_dir is None:
        bnk_dir = pck_dir.parent / ".wwise_bnks"
    bnk_dir.mkdir(parents=True, exist_ok=True)

    parser = Parser()
    n_subbanks = 0
    for pck in sorted(pck_dir.glob("*.pck")):
        bnks = extract_sub_banks(pck, bnk_dir / pck.stem)
        for b in bnks:
            parser.parse_bank(str(b))
            n_subbanks += 1

    all_events, all_actions, all_sounds, all_containers = {}, {}, {}, {}
    for bank in parser.get_banks():
        ev, ac, sn, cn = _extract_bank_data(bank, wmodel)
        for k, v in ev.items():
            all_events.setdefault(k, v)
        for k, v in ac.items():
            all_actions.setdefault(k, v)
        for k, v in sn.items():
            all_sounds.setdefault(k, v)
        for k, v in cn.items():
            all_containers.setdefault(k, v)

    # Resolve
    mapping = {}
    for evt_id, action_ids in all_events.items():
        wem_ids = set()
        for aid in action_ids:
            a = all_actions.get(aid)
            if a is None:
                continue
            target = a.get("target_id")
            if target is not None:
                if target in all_sounds:
                    src = all_sounds[target].get("source_id")
                    if src is not None:
                        wem_ids.add(src)
                elif target in all_containers:
                    _walk_container(target, all_containers, all_sounds, wem_ids)
            bank_id = a.get("bank_id")
            if bank_id and bank_id != 0:
                wem_ids.add(bank_id)
        if wem_ids:
            mapping[evt_id] = sorted(wem_ids)

    return {
        "wwise_sound_id_to_wem_ids": {str(k): v for k, v in mapping.items()},
        "n_events_total": len(all_events),
        "n_events_mapped": len(mapping),
        "n_sounds_total": len(all_sounds),
        "n_subbanks": n_subbanks,
        "pck_dir": str(pck_dir),
    }


# ----------------------------- CLI ---------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pck-dir", type=Path, required=True)
    ap.add_argument("--out", type=Path, default=DEFAULT_MAP_PATH)
    ap.add_argument("--bnk-dir", type=Path, default=None)
    ap.add_argument("--force", action="store_true",
                    help="rebuild even if --out exists")
    args = ap.parse_args()

    if args.out.exists() and not args.force:
        print(f"mapping exists at {args.out}; pass --force to rebuild")
        return 0

    t0 = time.time()
    result = build_mapping(args.pck_dir, args.bnk_dir)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with open(args.out, "w") as f:
        json.dump(result, f, indent=2)
    print(f"wrote {args.out}  ({time.time()-t0:.1f}s)")
    print(f"  events total={result['n_events_total']}  "
          f"mapped={result['n_events_mapped']}  "
          f"sounds={result['n_sounds_total']}  "
          f"subbanks={result['n_subbanks']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())