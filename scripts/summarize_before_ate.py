#!/usr/bin/env python3
"""Summarize a BEFORE/AFTER ATE matrix without dropping failed trials."""
from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
from collections import defaultdict
from pathlib import Path

import sys

sys.path.insert(0, str(Path(__file__).resolve().parent / "lib"))
from sem_geodf_ablation_common import iter_run_records  # noqa: E402

FAILURE_PENALTY = 10.0
DIVERGED = 5.0


def coverage(run_dir: Path) -> float | None:
    traj = run_dir / "vio.csv"
    if not traj.is_file() or traj.stat().st_size == 0:
        return 0.0
    try:
        lines = [ln for ln in traj.read_text().splitlines() if ln.strip() and not ln.startswith("#")]
        return float(len(lines))
    except OSError:
        return None


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()
    root = args.root.resolve()
    records = iter_run_records(root, exclude_oracle=False)
    cells: dict[tuple[str, str], list] = defaultdict(list)
    for rec in records:
        cells[(rec.scene, rec.method)].append(rec)

    rows = []
    lines = [
        f"# BEFORE/AFTER table for `{root}`",
        "",
        "| scene | method | attempted | ok | failed | diverged | median ATE | mean±std | SR | coverage(med rows) | failure-penalized ATE |",
        "|---|---|---:|---:|---:|---:|---:|---|---:|---:|---:|",
    ]
    for (scene, method), recs in sorted(cells.items()):
        attempted = len(recs)
        ok_vals = []
        failed = 0
        diverged = 0
        covs = []
        for rec in recs:
            covs.append(coverage(rec.run_dir) or 0.0)
            if not rec.qc_ok or rec.ate_rmse_m is None or rec.status != "ok":
                failed += 1
                continue
            if rec.ate_rmse_m > DIVERGED:
                diverged += 1
                continue
            ok_vals.append(rec.ate_rmse_m)
        ok = len(ok_vals)
        sr = ok / attempted if attempted else 0.0
        if ok_vals:
            med = statistics.median(ok_vals)
            mean = statistics.mean(ok_vals)
            std = statistics.stdev(ok_vals) if ok > 1 else 0.0
            mean_std = f"{mean:.4f}±{std:.4f}"
        else:
            med = float("nan")
            mean_std = "---"
        penalized_vals = list(ok_vals) + [FAILURE_PENALTY] * (failed + diverged)
        pen = statistics.mean(penalized_vals) if penalized_vals else float("nan")
        cov_med = statistics.median(covs) if covs else 0.0
        lines.append(
            f"| {scene} | {method} | {attempted} | {ok} | {failed} | {diverged} | "
            f"{med if ok_vals else float('nan'):.4f} | {mean_std} | {sr:.2f} | "
            f"{cov_med:.0f} | {pen:.4f} |"
        )
        rows.append({
            "scene": scene, "method": method, "attempted": attempted, "ok": ok,
            "failed": failed, "diverged": diverged,
            "median_ate": med if ok_vals else None,
            "mean_ate": statistics.mean(ok_vals) if ok_vals else None,
            "std_ate": statistics.stdev(ok_vals) if ok > 1 else (0.0 if ok_vals else None),
            "success_rate": sr, "coverage_median_rows": cov_med,
            "failure_penalized_ate": pen,
            "trials": [
                {
                    "trial": r.trial, "status": r.status, "ate": r.ate_rmse_m,
                    "qc_ok": r.qc_ok, "dir": str(r.run_dir),
                }
                for r in recs
            ],
        })

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(lines) + "\n")
    json_path = args.out.with_suffix(".json")
    json_path.write_text(json.dumps(rows, indent=2, allow_nan=False, default=lambda x: None) + "\n")
    csv_path = args.out.with_suffix(".csv")
    with csv_path.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=[
            "scene", "method", "attempted", "ok", "failed", "diverged",
            "median_ate", "mean_ate", "std_ate", "success_rate",
            "coverage_median_rows", "failure_penalized_ate"])
        w.writeheader()
        for r in rows:
            w.writerow({k: r.get(k) for k in w.fieldnames})
    print(f"wrote {args.out}")
    print(f"wrote {json_path}")
    print(f"wrote {csv_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
