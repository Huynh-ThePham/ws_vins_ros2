#!/usr/bin/env python3
"""Summarize Semantic–GeoDF ablation runs (ATE + fusion stats)."""
from __future__ import annotations

import argparse
import csv
import statistics
import sys
from collections import defaultdict
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(_SCRIPT_DIR / "lib"))
from sem_geodf_ablation_common import (  # noqa: E402
    METHODS_ORDER_DISPLAY,
    RunRecord,
    iter_run_records,
    mean_std,
)


def fusion_stats(run_dir: Path) -> dict[str, float]:
    stats_path = run_dir / "sem_geodf_stats.csv"
    if not stats_path.is_file():
        return {}
    rows = list(csv.DictReader(stats_path.open()))
    if not rows:
        return {}

    def mean_col(col: str) -> float:
        vals = [float(r[col]) for r in rows if r.get(col) not in (None, "")]
        return statistics.mean(vals) if vals else 0.0

    def frac_col(col: str) -> float:
        vals = [float(r[col]) for r in rows if r.get(col) not in (None, "")]
        return sum(vals) / len(vals) if vals else 0.0

    out = {
        "sem_active_frac": frac_col("sem_scene_active"),
        "geo_active_frac": frac_col("geo_frame_active"),
        "sem_mask_applied_frac": frac_col("sem_mask_applied"),
        "avg_reject": mean_col("rejected"),
    }
    if "sem_confirmed" in rows[0]:
        out["avg_sem_confirmed"] = mean_col("sem_confirmed")
    if "sem_policy_state" in rows[0]:
        out["avg_sem_policy_state"] = mean_col("sem_policy_state")
    if "sem_policy_hold" in rows[0]:
        out["avg_sem_policy_hold"] = mean_col("sem_policy_hold")
    if "sem_geo_overlap_ema" in rows[0]:
        out["avg_sem_geo_overlap_ema"] = mean_col("sem_geo_overlap_ema")
    if "sem_policy_trigger_burst" in rows[0]:
        out["trigger_burst_frac"] = frac_col("sem_policy_trigger_burst")
    if "sem_policy_trigger_strong" in rows[0]:
        out["trigger_strong_frac"] = frac_col("sem_policy_trigger_strong")
    if "sem_policy_trigger_overlap" in rows[0]:
        out["trigger_overlap_frac"] = frac_col("sem_policy_trigger_overlap")
    return out


def aggregate_records(records: list[RunRecord]) -> tuple[
    dict[tuple[str, str], list[float]],
    dict[str, dict[str, float]],
    list[RunRecord],
]:
    by_key_method: dict[tuple[str, str], list[float]] = defaultdict(list)
    fusion_by_key: dict[str, dict[str, float]] = {}
    qc_failed: list[RunRecord] = []

    for rec in records:
        if not rec.qc_ok:
            qc_failed.append(rec)
            continue
        if rec.ate_rmse_m is None:
            continue
        by_key_method[(rec.scene, rec.method)].append(float(rec.ate_rmse_m))
        if rec.method in ("sem_geodf", "sem_geodf_mask_gated", "sequential"):
            fs = fusion_stats(rec.run_dir)
            if fs:
                fusion_by_key[f"{rec.scene}:{rec.method}"] = fs

    return by_key_method, fusion_by_key, qc_failed


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument(
        "--include-oracle",
        action="store_true",
        help="include oracle-labelled VIODE policy override runs",
    )
    ap.add_argument(
        "--require-manifest",
        action="store_true",
        help="skip runs without run_manifest.json",
    )
    args = ap.parse_args()

    records = iter_run_records(
        args.root,
        exclude_oracle=not args.include_oracle,
        main_online_only=not args.include_oracle,
    )
    if args.require_manifest:
        records = [r for r in records if r.manifest]

    by_key_method, fusion_by_key, qc_failed = aggregate_records(records)

    lines = [
        "# Semantic–GeoDF ablation summary",
        "",
        f"Runs scanned: {len(records)} (QC-pass: {len(records) - len(qc_failed)})",
        f"Oracle runs excluded: {not args.include_oracle}",
        "",
        "## ATE RMSE (m) — mean ± std over trials",
        "",
        "| Scene | Method | ATE mean | ATE std | n |",
        "|-------|--------|----------|---------|---|",
    ]

    scenes = sorted({k for k, _ in by_key_method})
    for scene in scenes:
        for method in METHODS_ORDER_DISPLAY:
            vals = by_key_method.get((scene, method), [])
            if not vals:
                continue
            mean, std, n = mean_std(vals)
            lines.append(f"| {scene} | {method} | {mean:.4f} | {std:.4f} | {n} |")

    lines.extend(["", "## Fusion vs adaptive (ATE delta)", ""])
    for scene in scenes:
        adapt = by_key_method.get((scene, "adaptive"))
        fusion = by_key_method.get((scene, "sem_geodf"))
        if not adapt or not fusion:
            continue
        a_mean, _, _ = mean_std(adapt)
        f_mean, _, _ = mean_std(fusion)
        pct = 100.0 * (f_mean - a_mean) / a_mean if a_mean > 0 else 0.0
        verdict = "better" if f_mean < a_mean else ("worse" if f_mean > a_mean else "tie")
        lines.append(
            f"- **{scene}**: adaptive={a_mean:.4f} fusion={f_mean:.4f} "
            f"Δ={pct:+.1f}% ({verdict})"
        )

    if fusion_by_key:
        lines.extend(["", "## Fusion stats", ""])
        for key, fs in sorted(fusion_by_key.items()):
            extra = ""
            if "trigger_burst_frac" in fs:
                extra = (
                    f", burst={fs.get('trigger_burst_frac', 0):.1%}, "
                    f"strong={fs.get('trigger_strong_frac', 0):.1%}, "
                    f"overlap={fs.get('trigger_overlap_frac', 0):.1%}"
                )
            lines.append(
                f"- `{key}`: sem_active={fs.get('sem_active_frac', 0):.1%}, "
                f"geo_active={fs.get('geo_active_frac', 0):.1%}, "
                f"mask_applied={fs.get('sem_mask_applied_frac', 0):.1%}, "
                f"avg_reject={fs.get('avg_reject', 0):.2f}, "
                f"policy_state={fs.get('avg_sem_policy_state', 0):.2f}, "
                f"overlap_ema={fs.get('avg_sem_geo_overlap_ema', 0):.2f}"
                f"{extra}"
            )

    if qc_failed:
        lines.extend(["", "## QC failures (excluded from means)", ""])
        for rec in sorted(qc_failed, key=lambda r: (r.scene, r.method, str(r.run_dir))):
            lines.append(
                f"- `{rec.scene}` `{rec.method}`: {', '.join(rec.qc_issues)} "
                f"(`{rec.run_dir.name}`)"
            )

    args.out.write_text("\n".join(lines) + "\n")
    print(args.out.read_text())


if __name__ == "__main__":
    main()
