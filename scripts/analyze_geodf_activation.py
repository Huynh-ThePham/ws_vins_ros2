#!/usr/bin/env python3
"""Design the scene-aware activation gate from per-frame geo_df_stats.csv.

For each run, look at frame-level signals that should separate static frames
(no real dynamics -> we want pass-through) from dynamic frames (-> activate):
  - outlier_ratio   = ransac_outliers / scored
  - candidate_ratio = candidates / scored
Reports percentiles + fraction of frames above candidate thresholds, so we can
pick a threshold that skips static frames while activating on dynamic ones.
"""
from __future__ import annotations

import argparse
import csv
from pathlib import Path


def load(path: Path) -> list[dict]:
    rows = []
    with path.open() as f:
        for row in csv.DictReader(f):
            try:
                scored = float(row["scored"])
                if scored <= 0:
                    continue
                rows.append({
                    "outlier_ratio": float(row["ransac_outliers"]) / scored,
                    "candidate_ratio": float(row["candidates"]) / scored,
                    "reject_ratio": float(row["reject_ratio"]),
                    "max_sampson": float(row["max_sampson"]),
                })
            except (KeyError, ValueError, ZeroDivisionError):
                continue
    return rows


def pct(xs: list[float], q: float) -> float:
    if not xs:
        return float("nan")
    xs = sorted(xs)
    i = min(len(xs) - 1, max(0, int(round(q * (len(xs) - 1)))))
    return xs[i]


def frac_above(xs: list[float], thr: float) -> float:
    return sum(1 for x in xs if x >= thr) / len(xs) if xs else float("nan")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("paths", nargs="+", type=Path)
    ap.add_argument("--signal", default="outlier_ratio",
                    choices=["outlier_ratio", "candidate_ratio"])
    args = ap.parse_args()

    print(f"signal = {args.signal}\n")
    hdr = f"{'run':40s} {'n':>5s} {'p50':>7s} {'p75':>7s} {'p90':>7s} {'p95':>7s} {'max':>7s}"
    print(hdr)
    print("-" * len(hdr))
    data = {}
    for p in args.paths:
        rows = load(p)
        sig = [r[args.signal] for r in rows]
        data[p] = sig
        name = p.parent.name if p.name == "geo_df_stats.csv" else p.stem
        print(f"{name:40s} {len(sig):5d} "
              f"{pct(sig,0.50):7.3f} {pct(sig,0.75):7.3f} {pct(sig,0.90):7.3f} "
              f"{pct(sig,0.95):7.3f} {max(sig) if sig else float('nan'):7.3f}")

    print("\nFraction of frames with signal >= threshold:")
    thresholds = [0.03, 0.05, 0.07, 0.10, 0.15, 0.20]
    th_hdr = f"{'run':40s} " + " ".join(f">={t:.2f}" for t in thresholds)
    print(th_hdr)
    print("-" * len(th_hdr))
    for p, sig in data.items():
        name = p.parent.name if p.name == "geo_df_stats.csv" else p.stem
        print(f"{name:40s} " + " ".join(f"{frac_above(sig,t):5.2f} " for t in thresholds))


if __name__ == "__main__":
    main()
