#!/usr/bin/env python3
"""Summarize VIODE baseline vs SAD-VINS semantic filter."""
from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path
from typing import Any


def load_metrics(d: Path) -> dict[str, Any] | None:
    f = d / "eval" / "metrics.json"
    if not f.is_file():
        return None
    try:
        return json.loads(f.read_text())
    except json.JSONDecodeError:
        return None


def mean_reject_ratio(d: Path) -> float | None:
    f = d / "sem_stats.csv"
    if not f.is_file():
        return None
    ratios = []
    with f.open() as fh:
        for row in csv.DictReader(fh):
            try:
                ratios.append(float(row["reject_ratio"]))
            except (KeyError, ValueError):
                pass
    return sum(ratios) / len(ratios) if ratios else None


def mean_dynamic_pct(d: Path) -> float | None:
    f = d / "sem_stats.csv"
    if not f.is_file():
        return None
    vals = []
    with f.open() as fh:
        for row in csv.DictReader(fh):
            try:
                vals.append(float(row["dynamic_pixel_ratio"]))
            except (KeyError, ValueError):
                pass
    return 100.0 * sum(vals) / len(vals) if vals else None


def _fmt(v, p=3):
    return f"{v:.{p}f}" if isinstance(v, (int, float)) else "—"


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, default=Path("results/sad_viode"))
    ap.add_argument("--env", default="city_day")
    ap.add_argument("--levels", default="0_none 1_low 2_mid 3_high")
    ap.add_argument("--out", type=Path, default=None)
    args = ap.parse_args()

    levels = args.levels.split()
    lines = [
        f"# VIODE {args.env} — SAD-VINS vs baseline (stereo + IMU)",
        "",
        "| Level | Base ATE | SAD ATE | Δ ATE | Base RPE | SAD RPE | Reject% | DynPx% |",
        "|-------|--------:|--------:|------:|---------:|--------:|--------:|-------:|",
    ]

    for level in levels:
        base = load_metrics(args.root / f"{args.env}_{level}_baseline")
        sad = load_metrics(args.root / f"{args.env}_{level}_sad_sem")
        b_ate = base.get("ate_rmse_m") if base else None
        s_ate = sad.get("ate_rmse_m") if sad else None
        b_rpe = base.get("rpe_rmse_m") if base else None
        s_rpe = sad.get("rpe_rmse_m") if sad else None
        impr = None
        if isinstance(b_ate, (int, float)) and isinstance(s_ate, (int, float)) and b_ate > 0:
            impr = 100.0 * (s_ate - b_ate) / b_ate
        rr = mean_reject_ratio(args.root / f"{args.env}_{level}_sad_sem")
        dp = mean_dynamic_pct(args.root / f"{args.env}_{level}_sad_sem")
        lines.append(
            "| "
            + " | ".join([
                level,
                _fmt(b_ate), _fmt(s_ate),
                (f"{impr:+.1f}%" if isinstance(impr, float) else "—"),
                _fmt(b_rpe), _fmt(s_rpe),
                (f"{rr * 100:.2f}%" if isinstance(rr, float) else "—"),
                (f"{dp:.2f}%" if isinstance(dp, float) else "—"),
            ])
            + " |"
        )

    text = "\n".join(lines) + "\n"
    out = args.out or (args.root / f"{args.env}_sad_summary.md")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(text)
    print(text)


if __name__ == "__main__":
    main()
