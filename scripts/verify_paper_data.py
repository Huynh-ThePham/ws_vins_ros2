#!/usr/bin/env python3
"""Verify benchmark trial completeness/integrity for paper submission.

Scans VIODE (results/viode_repeat) and EuRoC (results/euroc_repeat) trial
trees and flags any problem that would invalidate a run BEFORE it is used in a
table: missing/empty trajectory, missing metrics, null/NaN ATE-RPE, low
coverage (possible divergence), and (for adaptive) missing geo_df_stats.

Exit code is non-zero if any hard problem is found, so it can gate a pipeline.
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

VIODE_ENVS = ["city_day", "city_night", "parking_lot"]
VIODE_LEVELS = ["0_none", "1_low", "2_mid", "3_high"]
EUROC_SEQS = ["MH_01_easy", "MH_02_easy", "MH_03_medium", "MH_04_difficult", "MH_05_difficult"]
METHODS = ["baseline", "adaptive"]


def _num(x) -> float | None:
    if isinstance(x, bool) or not isinstance(x, (int, float)):
        return None
    if math.isnan(x) or math.isinf(x):
        return None
    return float(x)


def check_trial(trial: Path, method: str, min_coverage: float) -> list[str]:
    """Return list of problems (empty = ok)."""
    problems: list[str] = []
    vio = trial / "vio.csv"
    metrics = trial / "eval" / "metrics.json"

    if not vio.is_file() or vio.stat().st_size == 0:
        problems.append("vio.csv missing/empty")
    if not metrics.is_file():
        problems.append("metrics.json missing")
        return problems

    try:
        m = json.loads(metrics.read_text())
    except Exception as e:
        problems.append(f"metrics.json unreadable ({e})")
        return problems

    if _num(m.get("ate_rmse_m")) is None:
        problems.append("ate_rmse_m null/NaN")
    if _num(m.get("rpe_rmse_m")) is None:
        problems.append("rpe_rmse_m null/NaN")

    cov = _num(m.get("coverage_pct"))
    if cov is None:
        problems.append("coverage_pct absent (re-run eval to capture)")
    elif cov < min_coverage:
        problems.append(f"low coverage {cov:.1f}% (<{min_coverage:.0f}% — possible divergence)")

    if method != "baseline":
        stats = trial / "geo_df_stats.csv"
        if not stats.is_file() or stats.stat().st_size == 0:
            problems.append("geo_df_stats.csv missing/empty (adaptive)")
    return problems


def scan(root: Path, cells: list[tuple[str, str]], n: int, min_coverage: float) -> dict:
    report = {"root": str(root), "n": n, "cells": {}, "ok": 0, "problem": 0, "missing_trials": 0}
    for key, method in cells:
        cell = {"trials": {}, "present": 0}
        for i in range(1, n + 1):
            trial = root / key / f"trial_{i}"
            if not trial.is_dir():
                report["missing_trials"] += 1
                cell["trials"][i] = ["trial dir missing"]
                continue
            cell["present"] += 1
            probs = check_trial(trial, method, min_coverage)
            cell["trials"][i] = probs
            if probs:
                report["problem"] += 1
            else:
                report["ok"] += 1
        report["cells"][key] = cell
    return report


def viode_cells() -> list[tuple[str, str]]:
    return [(f"{e}_{l}_{m}", m) for e in VIODE_ENVS for l in VIODE_LEVELS for m in METHODS]


def euroc_cells() -> list[tuple[str, str]]:
    return [(f"{s}_{m}", m) for s in EUROC_SEQS for m in METHODS]


def print_report(name: str, rep: dict) -> None:
    print(f"\n=== {name} ({rep['root']}) ===")
    print(f"  ok={rep['ok']}  problem={rep['problem']}  missing_trials={rep['missing_trials']}")
    for key, cell in rep["cells"].items():
        bad = {i: p for i, p in cell["trials"].items() if p}
        if bad:
            for i, probs in bad.items():
                print(f"  [X] {key}/trial_{i}: {'; '.join(probs)}")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--ws", type=Path, default=Path(__file__).resolve().parents[1])
    ap.add_argument("--viode-n", type=int, default=5)
    ap.add_argument("--euroc-n", type=int, default=3)
    ap.add_argument("--min-coverage", type=float, default=90.0)
    ap.add_argument("--only", choices=["viode", "euroc"], default=None)
    ap.add_argument("--out-json", type=Path, default=None)
    ap.add_argument("--strict", action="store_true",
                    help="exit non-zero if any problem OR any trial missing")
    args = ap.parse_args()

    reports = {}
    if args.only in (None, "viode"):
        reports["viode"] = scan(args.ws / "results" / "viode_repeat", viode_cells(),
                                 args.viode_n, args.min_coverage)
        print_report("VIODE N={}".format(args.viode_n), reports["viode"])
    if args.only in (None, "euroc"):
        reports["euroc"] = scan(args.ws / "results" / "euroc_repeat", euroc_cells(),
                                args.euroc_n, args.min_coverage)
        print_report("EuRoC N={}".format(args.euroc_n), reports["euroc"])

    total_problem = sum(r["problem"] for r in reports.values())
    total_missing = sum(r["missing_trials"] for r in reports.values())
    total_ok = sum(r["ok"] for r in reports.values())
    print(f"\n[verify] TOTAL ok={total_ok} problem={total_problem} missing={total_missing}")

    if args.out_json:
        args.out_json.parent.mkdir(parents=True, exist_ok=True)
        args.out_json.write_text(json.dumps(reports, indent=2) + "\n")
        print(f"[verify] wrote {args.out_json}")

    if args.strict and (total_problem or total_missing):
        raise SystemExit(1)
    if total_problem:
        raise SystemExit(2)


if __name__ == "__main__":
    main()
