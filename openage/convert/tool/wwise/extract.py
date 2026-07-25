#!/usr/bin/env python3
# Copyright 2026 the openage authors. See copying.md for legal info.

"""
Wwise .pck -> .opus extraction + decode pipeline for AoE2 DE.

This module implements the second half of the Wwise audio path:

  .pck (DIDX/DATA chunks)
        |
        |--- (1) _find_wem_bytes()       -- DIDX lookup -> raw .wem bytes
        v
  .wem (Wwise custom Vorbis 0xFFFF at 48 kHz mono/stereo, ~64 kbps)
        |
        |--- (2) vgmstream-cli            -- decode .wem -> .wav PCM16
        v
  .wav (PCM 16-bit, 48 kHz)
        |
        |--- (3) ffmpeg -c:a libopus      -- encode .wav -> .opus @ 96 kbps VBR
        v
  .opus (consumed by libopenage/audio/OpusDynamicLoader)

We use two binaries that are not Python deps:
  - vgmstream-cli  : https://github.com/vgmstream/vgmstream/releases (decodes .wem)
  - ffmpeg / avconv: ships the libopus encoder (Ubuntu: apt install ffmpeg)

Both are looked up on $PATH; override with the env vars below if needed.

Why Opus @ 96 kbps VBR (not the source's 64 kbps Vorbis):
  - openage's libopenage/audio/ only supports Opus (no .ogg/.wav loader).
  - Source .wem is Wwise-packaged Vorbis @ ~64 kbps (already lossy). 96 kbps
    Opus exceeds the source's lossy ceiling, so re-encoding does not lose
    audible quality. Opus at the same bitrate is perceptually better than
    Vorbis. 0.7 MB/min, fits the AoE2 music/sfx budget.

CLI usage:
  extract_wem.py <pck_dir> <wem_id> [-o out.opus] [--wav-only]
  extract_wem.py --batch <pck_dir> <mapping.json> [-o out_dir]
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

# Default binary names. Set env to override absolute paths.
VGMSTREAM_BIN_NAME = os.environ.get("OPENAGE_VGMSTREAM_BIN", "vgmstream-cli")
FFMPEG_BIN_NAME = os.environ.get("OPENAGE_FFMPEG_BIN", "ffmpeg")


def _vgmstream_path() -> str:
    """Resolve vgmstream-cli: explicit path from env, project-local bin/wwise/, or $PATH."""
    p = os.environ.get("OPENAGE_VGMSTREAM_BIN_PATH", "")
    if p and os.path.exists(p):
        return p
    # Project-local: openage/bin/wwise/vgmstream-cli (used during DE2 dev).
    proj_local = Path(__file__).resolve().parents[4] / "bin" / "wwise" / "vgmstream-cli"
    if proj_local.is_file():
        return str(proj_local)
    found = shutil.which(VGMSTREAM_BIN_NAME)
    return found or VGMSTREAM_BIN_NAME


def _ffmpeg_path() -> str:
    p = os.environ.get("OPENAGE_FFMPEG_BIN_PATH", "")
    if p and os.path.exists(p):
        return p
    found = shutil.which(FFMPEG_BIN_NAME)
    return found or FFMPEG_BIN_NAME


# ----------------------------- pck parsing --------------------------------

def _read_pck_header(buf: bytes) -> int:
    if buf[:4] != b"AKPK":
        raise ValueError(f"not an AKPK file (magic={buf[:4]!r})")
    hdr_size = struct.unpack_from("<I", buf, 4)[0]
    return 8 + hdr_size


def _iter_chunks(buf: bytes, start: int):
    off = start
    n = len(buf)
    while off + 8 <= n:
        tag = bytes(buf[off:off + 4])
        size = struct.unpack_from("<I", buf, off + 4)[0]
        body_off = off + 8
        end = body_off + size
        if tag not in (b"BKHD", b"DIDX", b"DATA", b"HIRC", b"INIT", b"STMG",
                       b"ENVS", b"PLAT", b"RIFF"):
            off += 8 + size
            continue
        if end > n:
            raise ValueError(f"chunk {tag!r} at {off} overruns file")
        yield tag, off, size, body_off
        off = end


def find_wem_bytes(pck_dir: Path, wem_id: int,
                   buf_cache: dict | None = None) -> bytes | None:
    """Find the .wem bytes for wem_id in any .pck under pck_dir.

    A .pck may contain multiple sub-banks, each with its own DIDX/DATA pair.
    DIDX entries reference offsets into THAT sub-bank's DATA chunk. The DIDX
    chunk precedes the DATA chunk in the file, so we buffer DIDX entries and
    resolve them when the matching DATA chunk arrives.
    `buf_cache` is an optional dict {Path: bytes} to memoize file reads.
    """
    for pck in sorted(pck_dir.glob("*.pck")):
        if buf_cache is not None and pck in buf_cache:
            data = buf_cache[pck]
        else:
            with open(pck, "rb") as f:
                data = f.read()
            if buf_cache is not None:
                buf_cache[pck] = data

        body_start = _read_pck_header(data)
        # Buffer DIDX entries (list of (offset, length)) that we're looking up
        # for; resolve against the next DATA chunk we see.
        # A "sub-bank" is bounded by BKHD chunks; we use that boundary to know
        # when to clear the pending entries.
        pending: list[tuple[int, int]] = []  # (doff, dlen)
        for tag, off, size, body_off in _iter_chunks(data, body_start):
            if tag == b"BKHD":
                pending = []  # new sub-bank; previous DIDX entries don't apply
            elif tag == b"DIDX":
                for i in range(size // 12):
                    wid, doff, dlen = struct.unpack_from(
                        "<III", data, body_off + i * 12
                    )
                    if wid == wem_id:
                        pending.append((doff, dlen))
            elif tag == b"DATA" and pending:
                data_off = body_off
                for doff, dlen in pending:
                    return data[data_off + doff: data_off + doff + dlen]
                pending = []  # exhausted
    return None


# ----------------------------- decode/encode ------------------------------

def _decode_wem_to_wav(wem_bytes: bytes, out_wav: Path) -> None:
    """vgmstream-cli .wem -> .wav (PCM16)."""
    vgm = _vgmstream_path()
    with tempfile.NamedTemporaryFile(suffix=".wem", delete=False) as tmp:
        tmp.write(wem_bytes)
        tmp_path = tmp.name
    try:
        cmd = [vgm, "-o", str(out_wav), tmp_path]
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
        if proc.returncode != 0:
            raise RuntimeError(
                f"vgmstream failed (rc={proc.returncode}): {proc.stderr.strip()}"
            )
        if not out_wav.exists() or out_wav.stat().st_size == 0:
            raise RuntimeError(
                f"vgmstream produced no output at {out_wav}; "
                f"stderr={proc.stderr.strip()!r}"
            )
    finally:
        try:
            os.unlink(tmp_path)
        except OSError:
            pass


def _encode_wav_to_opus(wav_path: Path, out_opus: Path,
                        bitrate: str = "96k") -> None:
    """ffmpeg .wav -> .opus (libopus VBR)."""
    ffmpeg = _ffmpeg_path()
    cmd = [
        ffmpeg, "-y", "-hide_banner", "-loglevel", "error",
        "-i", str(wav_path),
        "-c:a", "libopus",
        "-b:a", bitrate,
        "-vbr", "on",
        "-application", "audio",
        str(out_opus),
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    if proc.returncode != 0:
        raise RuntimeError(f"ffmpeg failed: {proc.stderr.strip()}")


def extract_wem(pck_dir: Path, wem_id: int, out_opus: Path,
                keep_wav: bool = False,
                buf_cache: dict | None = None) -> dict:
    """Full pipeline: find .wem -> decode -> encode -> write .opus.

    Returns dict with stats {wem_id, src_pck, wem_bytes, wav_bytes, opus_bytes, elapsed}.
    Raises RuntimeError on failure.
    """
    t0 = time.time()
    wem_bytes = find_wem_bytes(pck_dir, wem_id, buf_cache=buf_cache)
    if wem_bytes is None:
        raise RuntimeError(f"wem_id {wem_id} not found in any .pck under {pck_dir}")

    # find which .pck this came from (for logging)
    src_pck = None
    for pck in sorted(pck_dir.glob("*.pck")):
        if buf_cache is not None and pck in buf_cache:
            data = buf_cache[pck]
        else:
            with open(pck, "rb") as f:
                data = f.read()
        body_start = _read_pck_header(data)
        cur_data_off = 0
        for tag, off, size, body_off in _iter_chunks(data, body_start):
            if tag == b"DATA":
                cur_data_off = body_off
            elif tag == b"DIDX":
                for i in range(size // 12):
                    wid = struct.unpack_from("<I", data, body_off + i * 12)[0]
                    if wid == wem_id:
                        src_pck = str(pck)
                        break
                if src_pck:
                    break
        if src_pck:
            break

    out_opus.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmpdir:
        wav_path = Path(tmpdir) / f"{wem_id}.wav"
        _decode_wem_to_wav(wem_bytes, wav_path)
        _encode_wav_to_opus(wav_path, out_opus)
        wav_bytes = wav_path.stat().st_size if keep_wav else None
        if keep_wav:
            target_wav = out_opus.with_suffix(".wav")
            shutil.copy(wav_path, target_wav)

    return {
        "wem_id": wem_id,
        "src_pck": src_pck,
        "wem_bytes": len(wem_bytes),
        "wav_bytes": wav_bytes,
        "opus_bytes": out_opus.stat().st_size,
        "elapsed": time.time() - t0,
    }


def extract_wem_to_opus_bytes(wem_bytes: bytes) -> bytes:
    """In-process helper for the convert worker: bytes_in (.wem) -> bytes_out (.opus).

    Uses vgmstream-cli (decode .wem -> .wav) and ffmpeg (encode .wav -> .opus).
    Both tools run via subprocess on temp files; this is faster than a separate
    extract script for many small sounds.
    """
    with tempfile.TemporaryDirectory() as tmpdir:
        wem_path = Path(tmpdir) / "in.wem"
        wav_path = Path(tmpdir) / "out.wav"
        opus_path = Path(tmpdir) / "out.opus"
        wem_path.write_bytes(wem_bytes)
        _decode_wem_to_wav(wem_bytes, wav_path)
        _encode_wav_to_opus(wav_path, opus_path)
        return opus_path.read_bytes()


# ----------------------------- CLI ----------------------------------------

def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pck_dir", type=Path, help="directory containing .pck files")
    ap.add_argument("wem_id", nargs="?", type=lambda s: int(s, 0),
                    help="single wem_id to extract (decimal or 0x...)")
    ap.add_argument("-o", "--out", type=Path, help="output .opus path (single mode)")
    ap.add_argument("--batch", type=Path,
                    help="mapping.json produced by build_mapping.py")
    ap.add_argument("--out-dir", type=Path,
                    help="output dir for --batch (default: <pck_dir>/../opus)")
    ap.add_argument("--limit", type=int, default=0, help="process at most N wem_ids")
    ap.add_argument("--wav", action="store_true", help="also keep .wav alongside .opus")
    ap.add_argument("--bitrate", default="96k")
    args = ap.parse_args(argv)

    if args.batch:
        out_dir = args.out_dir or args.batch.parent / "opus"
        with open(args.batch) as f:
            mp = json.load(f)
        # collect unique wem_ids from mapping
        wem_set = set()
        for wems in mp["mapping"].values():
            wem_set.update(wems)
        wem_ids = sorted(wem_set)
        if args.limit:
            wem_ids = wem_ids[:args.limit]
        print(f"batch: {len(wem_ids)} wem_ids -> {out_dir}")
        ok = 0
        fail = 0
        buf_cache = {}
        for i, wid in enumerate(wem_ids):
            target = out_dir / f"{wid}.opus"
            try:
                stats = extract_wem(args.pck_dir, wid, target,
                                    keep_wav=args.wav, buf_cache=buf_cache)
                ok += 1
                if i % 50 == 0:
                    print(f"  [{i+1}/{len(wem_ids)}] wem_id {wid} -> "
                          f"{stats['opus_bytes']} bytes ({stats['elapsed']:.2f}s)",
                          flush=True)
            except Exception as e:  # pylint: disable=broad-except
                fail += 1
                if fail <= 5:
                    print(f"  [{i+1}/{len(wem_ids)}] wem_id {wid} FAILED: {e}",
                          flush=True)
        print(f"\nbatch done: ok={ok}  fail={fail}")
        return 0

    if args.wem_id is None:
        ap.error("wem_id required (or use --batch)")
    out = args.out or Path(f"{args.wem_id}.opus")
    stats = extract_wem(args.pck_dir, args.wem_id, out, keep_wav=args.wav)
    print(json.dumps(stats, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())