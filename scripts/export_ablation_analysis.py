#!/usr/bin/env python3
"""Export per-run ablation metrics for offline analysis (weak methods, gate stats)."""
from __future__ import annotations

import argparse
import csv
import statistics
import sys
from collections import defaultdict
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(_SCRIPT_DIR / "lib"))
from sem_geodf_ablation_common import RunRecord, iter_run_records, mean_std  # noqa: E402


def csv_stats(path: Path, cols: list[str]) -> dict[str, float]:
    if not path.is_file():
        return {}
    rows = list(csv.DictReader(path.open()))
    if not rows:
        return {}
    out: dict[str, float] = {"frames": float(len(rows))}
    for col in cols:
        vals = [float(r[col]) for r in rows if r.get(col) not in (None, "")]
        if vals:
            out[f"{col}_mean"] = statistics.mean(vals)
            out[f"{col}_max"] = max(vals)
    return out


def record_to_row(rec: RunRecord) -> dict:
    row: dict = {
        "run_dir": str(rec.run_dir),
        "scene": rec.scene,
        "method": rec.method,
        "ate_rmse_m": rec.ate_rmse_m,
        "rpe_rmse_m": rec.rpe_rmse_m,
        "n_poses_est": rec.metrics.get("n_poses_est"),
        "n_poses_gt": rec.metrics.get("n_poses_gt"),
        "bag_rate": rec.bag_rate,
        "yolo": rec.yolo,
        "dataset": rec.manifest.get("dataset"),
        "trial": rec.trial,
        "status": rec.status,
        "qc_ok": rec.qc_ok,
        "qc_issues": ",".join(rec.qc_issues),
        "oracle_ablation": rec.oracle_ablation,
        "sem_policy_dynamic_level": rec.sem_policy_dynamic_level,
        "protocol_fair": rec.protocol_fair,
        "git_sha": rec.git_sha,
        "config_hash": rec.manifest.get("config_hash"),
    }
    row.update(
        csv_stats(
            rec.run_dir / "sem_geodf_stats.csv",
            [
                "sem_scene_active",
                "geo_frame_active",
                "sem_mask_trusted",
                "sem_mask_lag_ms",
                "rejected",
                "reject_ratio",
                "sem_confirmed",
                "geo_candidates",
                "sem_policy_state",
                "sem_policy_hold",
                "sem_geo_overlap",
                "sem_geo_overlap_ema",
                "sem_policy_hard_reject",
                "sem_policy_trigger_burst",
                "sem_policy_trigger_strong",
                "sem_policy_trigger_overlap",
            ],
        )
    )
    row.update(
        csv_stats(
            rec.run_dir / "geo_df_stats.csv",
            ["ransac_outliers", "rejected", "geo_activation_ema"],
        )
    )
    row.update(
        csv_stats(
            rec.run_dir / "sem_stats.csv",
            ["rejected", "ratio", "dynamic_pixel_ratio", "sem_candidates", "sem_confirmed"],
        )
    )
    if "vio_log_missing_or_empty" in rec.qc_issues:
        row["vio_log_issue"] = "missing_or_empty"
    elif "vio_log_contains_error" in rec.qc_issues:
        row["vio_log_issue"] = "contains_error"
    return row


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, required=True)
    ap.add_argument("--out-csv", type=Path, required=True)
    ap.add_argument("--out-md", type=Path, required=True)
    ap.add_argument("--include-oracle", action="store_true")
    args = ap.parse_args()

    records = iter_run_records(
        args.root,
        exclude_oracle=not args.include_oracle,
        main_online_only=not args.include_oracle,
    )
    rows_out = [record_to_row(rec) for rec in records]

    if not rows_out:
        args.out_csv.write_text("scene,method,status\n")
        args.out_md.write_text("# Ablation analysis\n\nNo runs found.\n")
        return

    fieldnames = sorted({k for r in rows_out for k in r})
    with args.out_csv.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        w.writerows(rows_out)

    qc_ok_records = [r for r in records if r.qc_ok and r.ate_rmse_m is not None]
    scene_method_vals: dict[str, dict[str, list[float]]] = defaultdict(lambda: defaultdict(list))
    for rec in qc_ok_records:
        scene_method_vals[rec.scene][rec.method].append(float(rec.ate_rmse_m))  # type: ignore[arg-type]

    lines = [
        "# Ablation analysis export",
        "",
        f"Runs: {len(rows_out)} (QC-pass with ATE: {len(qc_ok_records)})",
        f"CSV: `{args.out_csv}`",
        f"Oracle excluded: {not args.include_oracle}",
        "",
        "## Fusion vs adaptive (ATE delta, mean over trials)",
        "",
    ]
    for scene in sorted(scene_method_vals):
        adapt_vals = scene_method_vals[scene].get("adaptive", [])
        fusion_vals = scene_method_vals[scene].get("sem_geodf", [])
        if not adapt_vals or not fusion_vals:
            continue
        a_mean, _, _ = mean_std(adapt_vals)
        f_mean, _, n = mean_std(fusion_vals)
        pct = 100.0 * (f_mean - a_mean) / a_mean if a_mean > 0 else 0.0
        tag = "WORSE" if f_mean > a_mean * 1.05 else ("BETTER" if f_mean < a_mean * 0.95 else "similar")
        lines.append(
            f"- **{scene}**: adaptive={a_mean:.4f} fusion={f_mean:.4f} "
            f"Δ={pct:+.1f}% ({tag}) n_fusion={n}"
        )

    lines.extend(["", "## Runs missing ATE or QC issues", ""])
    issues = [r for r in records if not r.qc_ok or r.ate_rmse_m is None]
    if not issues:
        lines.append("- none")
    else:
        for rec in issues:
            lines.append(
                f"- `{rec.scene}` `{rec.method}` status={rec.status} "
                f"issues={','.join(rec.qc_issues) or 'no_ate'}"
            )

    args.out_md.write_text("\n".join(lines) + "\n")
    print(f"[export] {len(rows_out)} runs -> {args.out_csv}")


if __name__ == "__main__":
    main()
