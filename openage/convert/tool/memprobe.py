# Copyright 2026 the openage authors. See copying.md for legal info.

"""
Light-weight memory instrumentation for the convert pipeline.

Activated by setting OPENAGE_MEMPROBE=1 in the environment.

Provides:
  * A background thread that polls VmRSS every 1 second and emits an INFO
    log line so the trajectory of memory growth is visible alongside the
    normal convert output. Use this to spot the moment the process
    crosses a memory budget (e.g. 3 GiB) and decide whether to kill it.

  * A SIGUSR1 handler that dumps the top 20 tracemalloc allocation sites.
    To get a snapshot while the convert is running:
        kill -USR1 <pid>
    The dump lands in the convert log.

  * `snapshot(label)` callable from anywhere in the convert pipeline that
    does a gc.collect() + tracemalloc snapshot + top-20 dump, so we get
    a forced dump at known stage boundaries without needing user signal.

  * `set_interval(seconds)` to dynamically tighten the RSS polling cadence
    if memory is growing fast.

This module deliberately stays small and dependency-free so it can be
imported from anywhere in the convert pipeline without circular-import
trouble.
"""
from __future__ import annotations

import gc
import os
import signal
import threading
import time
import tracemalloc


from ...log import info

_RSS_INTERVAL_S_DEFAULT = 1.0
_TRACEMALLOC_TOP = 20


def _read_rss_mib() -> float:
    """Return current RSS in MiB from /proc/self/status, or -1 on error."""
    try:
        with open("/proc/self/status") as f:
            for line in f:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1]) / 1024.0
    except OSError:
        pass
    return -1.0


def _rss_sampler(stop: threading.Event, interval_s: float) -> None:
    """Background loop that logs RSS every interval_s seconds."""
    # Give the convert pipeline a moment to do its first big allocation
    # before we start sampling so the first sample is meaningful.
    time.sleep(interval_s)
    while not stop.wait(interval_s):
        info("MEMPROBE rss=%.1f MiB  objs=%d",
             _read_rss_mib(), len(gc.get_objects()))


def _dump_tracemalloc(label: str) -> None:
    """Force gc + take a tracemalloc snapshot + log the top allocation sites."""
    if not tracemalloc.is_tracing():
        info("MEMPROBE snapshot(%s): tracemalloc not running", label)
        return

    gc.collect()
    rss = _read_rss_mib()
    objs = len(gc.get_objects())

    snapshot = tracemalloc.take_snapshot()
    stats = snapshot.statistics("filename")
    info("MEMPROBE snapshot(%s) rss=%.1f MiB objs=%d top-%d sites:",
         label, rss, objs, _TRACEMALLOC_TOP)
    for stat in stats[:_TRACEMALLOC_TOP]:
        info("MEMPROBE   %s", stat)


def snapshot(label: str) -> None:
    """
    Public hook for forcing a tracemalloc dump at a known stage boundary.

    Call sites should pass a short, distinctive label (e.g. "post-pregen"
    or "after sprite atlas 42") so the dump is easy to grep in the log.
    """
    _dump_tracemalloc(label)


def dump_heap_breakdown(label: str, exit_after: bool = True) -> None:
    """
    Ground-truth breakdown of resident heap by object type.

    Tallies every gc-tracked object by ``type(obj)`` (count + shallow
    ``sys.getsizeof``) and writes the full table to
    ``/tmp/openage_memprobe_heap.txt``, logging the top entries + the
    ``value_members.*`` subtotal.  This shows exactly where RSS lives so we
    know which lever (``__slots__``, dedup, structural) gives the biggest
    win -- without the 5-10% tracemalloc overhead (uses gc, not tracemalloc).

    If ``exit_after``, ``sys.exit(0)`` afterwards so the pipeline stops here:
    the gamespec load is the peak we want to characterize, and stopping avoids
    the long nyan/media phases so this can be iterated quickly.
    """
    import sys
    from collections import defaultdict

    gc.collect()
    rss = _read_rss_mib()
    objs = len(gc.get_objects())
    info("MEMPROBE heap-breakdown(%s) rss=%.1f MiB objs=%d",
         label, rss, objs)

    by_type = defaultdict(lambda: [0, 0])
    total_bytes = 0
    for obj in gc.get_objects():
        tname = type(obj).__module__ + "." + type(obj).__name__
        rec = by_type[tname]
        rec[0] += 1
        try:
            sz = sys.getsizeof(obj, 0)
        except Exception:  # pylint: disable=broad-except
            sz = 0
        rec[1] += sz
        total_bytes += sz

    out_path = "/tmp/openage_memprobe_heap.txt"
    with open(out_path, "w") as out:
        out.write(f"label={label} rss={rss:.1f} MiB objs={objs}\n")
        out.write(f"shallow_total={total_bytes / 1024 / 1024:.1f} MiB "
                  f"types={len(by_type)}\n\n")
        out.write("=== top 40 types by shallow bytes ===\n")
        out.write(f"{'type':<70}{'count':>12}{'MiB':>12}\n")
        for name, (cnt, byts) in sorted(by_type.items(),
                                        key=lambda kv: kv[1][1],
                                        reverse=True)[:40]:
            out.write(f"{name:<70}{cnt:>12,}{byts / 1048576:>12.1f}\n")
        out.write("\n=== top 20 types by instance count ===\n")
        out.write(f"{'type':<70}{'count':>12}{'MiB':>12}\n")
        for name, (cnt, byts) in sorted(by_type.items(),
                                        key=lambda kv: kv[1][0],
                                        reverse=True)[:20]:
            out.write(f"{name:<70}{cnt:>12,}{byts / 1048576:>12.1f}\n")
        vm_total = sum(v[1] for k, v in by_type.items()
                       if "value_members" in k)
        out.write(f"\nvalue_members subtotal: {vm_total / 1048576:.1f} MiB\n")

    info("MEMPROBE heap-breakdown(%s) shallow_total=%.1f MiB types=%d "
         "-> %s", label, total_bytes / 1048576, len(by_type), out_path)
    for name, (cnt, byts) in sorted(by_type.items(),
                                    key=lambda kv: kv[1][1],
                                    reverse=True)[:12]:
        info("MEMPROBE   %-60s %10d %8.1f MiB",
             name, cnt, byts / 1048576)

    if exit_after:
        info("MEMPROBE exiting after heap-breakdown(%s)", label)
        sys.exit(0)



def set_interval(seconds: float) -> None:
    """Change the RSS polling cadence at runtime. Takes effect on next tick."""
    enable._interval_s = max(0.25, seconds)


def _handle_sigusr1(signum, frame):  # noqa: ARG001 - signal handler signature
    """Dump tracemalloc top sites when SIGUSR1 is received."""
    _dump_tracemalloc("SIGUSR1")


def enable() -> None:
    """
    Enable periodic RSS sampling + SIGUSR1 tracemalloc dump.

    Called from convert.driver.convert_metadata() when OPENAGE_MEMPROBE=1.
    Safe to call multiple times: subsequent calls are no-ops.
    """
    if getattr(enable, "_active", False):
        return
    enable._active = True
    enable._interval_s = _RSS_INTERVAL_S_DEFAULT

    # Start tracemalloc. The C Python frames add overhead (~5-10%) so we
    # only enable it when the user explicitly opts in via the env var.
    tracemalloc.start(25)

    # Background RSS sampler.
    stop = threading.Event()
    thread = threading.Thread(target=_rss_sampler,
                              args=(stop, enable._interval_s),
                              name="openage-memprobe", daemon=True)
    thread.start()
    enable._stop_event = stop

    # Signal handler so the user can request a tracemalloc snapshot on demand.
    try:
        signal.signal(signal.SIGUSR1, _handle_sigusr1)
    except (ValueError, OSError):
        # SIGUSR1 unavailable on this platform (Windows); skip silently.
        pass

    info("MEMPROBE enabled (sampling every %.2fs; "
         "send SIGUSR1 to dump tracemalloc top-%d; "
         "snapshot() forces a dump from code)",
         enable._interval_s, _TRACEMALLOC_TOP)