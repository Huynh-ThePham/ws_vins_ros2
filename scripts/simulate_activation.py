#!/usr/bin/env python3
"""Offline replay of the GeoDF scene-aware activation gate.

Reads per-frame geo_df_stats.csv (which logs instantaneous ransac_outliers and
scored) and replays the exact C++ EMA + hysteresis logic for given parameters,
reporting the fraction of frames the hard-rejection would be ARMED. This lets us
tune (alpha, activate_ratio, deactivate_frac) so static scenes stay near 0%
activation while dynamic scenes activate, without re-running VINS.

Usage:
    simulate_activation.py STATS.csv [STATS2.csv ...] \
        --alpha 0.2 --ratio 0.10 --deact 0.6
    simulate_activation.py STATS... --sweep
"""
from __future__ import annotations

import argparse
import csv
from pathlib import Path


def load_signal(path: Path) -> list[float]:
    sig = []
    with path.open() as f:
        for row in csv.DictReader(f):
            try:
                scored = float(row["scored"])
                out = float(row["ransac_outliers"])
            except (KeyError, ValueError):
                continue
            sig.append(out / scored if scored > 0 else 0.0)
    return sig


def replay(sig: list[float], alpha: float, ratio: float, deact: float) -> float:
    """Return fraction of frames ARMED, matching feature_tracker.cpp exactly."""
    if not sig:
        return float("nan")
    ema = -1.0
    active = False
    hi, lo = ratio, ratio * deact
    n_active = 0
    for s in sig:
        ema = s if ema < 0 else alpha * s + (1 - alpha) * ema
        active = (ema >= lo) if active else (ema >= hi)
        n_active += 1 if active else 0
    return n_active / len(sig)


def name_of(p: Path) -> str:
    return p.parent.name if p.name == "geo_df_stats.csv" else p.stem


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("paths", nargs="+", type=Path)
    ap.add_argument("--alpha", type=float, default=0.2)
    ap.add_argument("--ratio", type=float, default=0.10)
    ap.add_argument("--deact", type=float, default=0.6)
    ap.add_argument("--sweep", action="store_true")
    args = ap.parse_args()

    sigs = {name_of(p): load_signal(p) for p in args.paths}

    if not args.sweep:
        print(f"alpha={args.alpha} ratio={args.ratio} deact={args.deact}\n")
        print(f"{'run':40s} {'frames':>7s} {'active%':>8s}")
        print("-" * 58)
        for name, sig in sigs.items():
            print(f"{name:40s} {len(sig):7d} {100*replay(sig,args.alpha,args.ratio,args.deact):8.1f}")
        return

    # Sweep: show active% per run for a grid of (alpha, ratio).
    alphas = [0.15, 0.2, 0.3]
    ratios = [0.08, 0.10, 0.12, 0.15]
    for alpha in alphas:
        print(f"\n===== alpha={alpha}, deact={args.deact} =====")
        head = f"{'run':40s} " + " ".join(f"r={r:.2f}" for r in ratios)
        print(head)
        print("-" * len(head))
        for name, sig in sigs.items():
            cells = " ".join(f"{100*replay(sig,alpha,r,args.deact):5.1f}" for r in ratios)
            print(f"{name:40s} {cells}")


if __name__ == "__main__":
    main()
