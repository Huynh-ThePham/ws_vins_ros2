#!/usr/bin/env python3
"""Sample process CPU/RAM of the VIO node during a benchmark run (no rebuild).

Top-tier submissions expect an end-to-end computational-cost statement, not just
the GeoDF module time. The in-pipeline C++ timer (geo_ms) covers the module; this
sampler covers the whole process so baseline vs adaptive resource usage (peak RSS,
mean/peak CPU%, thread count) can be reported and compared.

Resolves the node PID by name, polls /proc/<pid>/stat + status until it exits,
then writes <out>. All threads are included (utime+stime are process-wide).
"""
from __future__ import annotations

import argparse
import json
import os
import time
from pathlib import Path

CLK_TCK = os.sysconf("SC_CLK_TCK")
PAGE_KB = os.sysconf("SC_PAGE_SIZE") // 1024


def find_pid(name: str) -> int | None:
    for p in Path("/proc").iterdir():
        if not p.name.isdigit():
            continue
        try:
            comm = (p / "comm").read_text().strip()
        except Exception:
            continue
        if comm == name or comm.startswith(name[:15]):
            return int(p.name)
    return None


def read_proc(pid: int) -> tuple[float, float, int] | None:
    """Return (cpu_jiffies, rss_mb, n_threads) or None if gone."""
    try:
        stat = (Path("/proc") / str(pid) / "stat").read_text()
        # field 14 utime, 15 stime, 20 num_threads, 24 rss (pages) — after comm in parens
        rparen = stat.rfind(")")
        fields = stat[rparen + 2:].split()
        utime = int(fields[11])
        stime = int(fields[12])
        nthreads = int(fields[17])
        rss_pages = int(fields[21])
        return (utime + stime), rss_pages * PAGE_KB / 1024.0, nthreads
    except Exception:
        return None


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", default="pht_vio_node")
    ap.add_argument("--pid", type=int, default=None)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--interval-ms", type=int, default=500)
    ap.add_argument("--max-wait-s", type=float, default=15.0,
                    help="max seconds to wait for the process to appear")
    args = ap.parse_args()

    pid = args.pid
    t_wait = time.time()
    while pid is None and (time.time() - t_wait) < args.max_wait_s:
        pid = find_pid(args.name)
        if pid is None:
            time.sleep(0.2)
    if pid is None:
        args.out.write_text(json.dumps({"error": "process not found", "name": args.name}) + "\n")
        return

    interval = args.interval_ms / 1000.0
    samples_cpu: list[float] = []
    rss_vals: list[float] = []
    thread_vals: list[int] = []
    prev = read_proc(pid)
    prev_wall = time.time()
    t_start = prev_wall

    while True:
        time.sleep(interval)
        cur = read_proc(pid)
        if cur is None:
            break
        now = time.time()
        rss_vals.append(cur[1])
        thread_vals.append(cur[2])
        if prev is not None:
            d_jiffies = cur[0] - prev[0]
            d_wall = now - prev_wall
            if d_wall > 0:
                cpu_pct = 100.0 * (d_jiffies / CLK_TCK) / d_wall
                samples_cpu.append(cpu_pct)
        prev, prev_wall = cur, now

    wall_s = time.time() - t_start
    result = {
        "pid": pid,
        "name": args.name,
        "wall_s": round(wall_s, 2),
        "n_samples": len(rss_vals),
        "peak_rss_mb": round(max(rss_vals), 1) if rss_vals else None,
        "mean_rss_mb": round(sum(rss_vals) / len(rss_vals), 1) if rss_vals else None,
        "mean_cpu_pct": round(sum(samples_cpu) / len(samples_cpu), 1) if samples_cpu else None,
        "peak_cpu_pct": round(max(samples_cpu), 1) if samples_cpu else None,
        "peak_threads": max(thread_vals) if thread_vals else None,
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(f"[resource] peak_rss={result['peak_rss_mb']}MB mean_cpu={result['mean_cpu_pct']}% -> {args.out}")


if __name__ == "__main__":
    main()
