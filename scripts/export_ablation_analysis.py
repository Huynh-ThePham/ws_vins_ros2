#!/usr/bin/env python3
"""Export per-run ablation metrics plus paired deltas (plan P1.7/P1.8)."""
from __future__ import annotations

import argparse
import csv
import math
import statistics
import sys
from collections import defaultdict
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(_SCRIPT_DIR / "lib"))
from sem_geodf_ablation_common import RunRecord, iter_run_records, mean_std  # noqa: E402

MAIN_METHODS = ["baseline", "geodf", "semantic", "union_noweight", "union_weight"]


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
        "seed": rec.manifest.get("seed"),
        "status": rec.status,
        "failed": 0 if rec.status == "ok" else 1,
        "qc_ok": rec.qc_ok,
        "qc_issues": ",".join(rec.qc_issues),
        "oracle_ablation": rec.oracle_ablation,
        "sem_policy_dynamic_level": rec.sem_policy_dynamic_level,
        "protocol_fair": rec.protocol_fair,
        "protocol_tag": rec.manifest.get("protocol_tag"),
        "git_sha": rec.git_sha,
        "config_hash": rec.manifest.get("resolved_config_sha256") or rec.manifest.get("config_hash"),
        "trajectory_coverage": rec.manifest.get("trajectory_coverage")
            or rec.metrics.get("trajectory_coverage"),
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


def _percentile(vals: list[float], pct: float) -> float:
    if not vals:
        return float("nan")
    ordered = sorted(vals)
    idx = min(len(ordered) - 1, max(0, int(round((pct / 100.0) * (len(ordered) - 1)))))
    return ordered[idx]


def bootstrap_ci(vals: list[float], n_boot: int = 500, alpha: float = 0.05) -> tuple[float, float]:
    if not vals:
        return float("nan"), float("nan")
    if len(vals) == 1:
        return vals[0], vals[0]
    rng_state = 0
    samples = []
    for i in range(n_boot):
        # Deterministic LCG bootstrap — no numpy dependency.
        picks = []
        for j in range(len(vals)):
            rng_state = (1103515245 * (rng_state + i * 17 + j) + 12345) & 0x7FFFFFFF
            picks.append(vals[rng_state % len(vals)])
        samples.append(statistics.mean(picks))
    samples.sort()
    lo = samples[int(alpha / 2 * len(samples))]
    hi = samples[int((1 - alpha / 2) * len(samples)) - 1]
    return lo, hi


def cvar(vals: list[float], alpha: float = 0.20) -> float:
    if not vals:
        return float("nan")
    ordered = sorted(vals, reverse=True)
    k = max(1, int(math.ceil(alpha * len(ordered))))
    return statistics.mean(ordered[:k])


def write_paired(records: list[RunRecord], out_csv: Path, out_md: Path) -> None:
    """Paired ATE deltas vs baseline and U vs U+W (P1.7), plus robustness aggregates (P1.8)."""
    by_key: dict[tuple, dict[str, RunRecord]] = defaultdict(dict)
    for rec in records:
        dataset = rec.manifest.get("dataset")
        trial = rec.trial
        seed = rec.manifest.get("seed")
        by_key[(dataset, rec.scene, trial, seed)][rec.method] = rec

    paired_rows = []
    regrets_by_scene: dict[str, list[float]] = defaultdict(list)
    failure_flags: list[int] = []
    coverage_vals: list[float] = []

    for (dataset, scene, trial, seed), methods in sorted(by_key.items(), key=lambda x: str(x[0])):
        baseline = methods.get("baseline")
        for method, rec in methods.items():
            failed = 0 if rec.status == "ok" else 1
            failure_flags.append(failed)
            cov = rec.manifest.get("trajectory_coverage") or rec.metrics.get("trajectory_coverage")
            if cov is not None:
                coverage_vals.append(float(cov))
            delta_b = None
            rel_regret = None
            if baseline is not None and baseline.ate_rmse_m is not None and \
                    rec.ate_rmse_m is not None and baseline.status == "ok" and rec.status == "ok":
                delta_b = float(rec.ate_rmse_m) - float(baseline.ate_rmse_m)
                if float(baseline.ate_rmse_m) > 0:
                    rel_regret = delta_b / float(baseline.ate_rmse_m)
                    if method == "union_weight":
                        regrets_by_scene[str(scene)].append(rel_regret)
            delta_uw = None
            u = methods.get("union_noweight")
            uw = methods.get("union_weight")
            if method == "union_weight" and u is not None and uw is not None and \
                    u.ate_rmse_m is not None and uw.ate_rmse_m is not None and \
                    u.status == "ok" and uw.status == "ok":
                delta_uw = float(uw.ate_rmse_m) - float(u.ate_rmse_m)
            paired_rows.append({
                "dataset": dataset,
                "scene": scene,
                "trial": trial,
                "seed": seed,
                "method": method,
                "ate_rmse_m": rec.ate_rmse_m,
                "paired_delta_vs_baseline": delta_b,
                "paired_delta_union_weight_minus_noweight": delta_uw if method == "union_weight" else None,
                "relative_regret_vs_baseline": rel_regret,
                "failed": failed,
                "trajectory_coverage": cov,
                "status": rec.status,
            })

    fieldnames = sorted({k for r in paired_rows for k in r}) if paired_rows else [
        "dataset", "scene", "trial", "method", "ate_rmse_m"
    ]
    with out_csv.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        w.writerows(paired_rows)

    all_regrets = [r for vals in regrets_by_scene.values() for r in vals]
    lines = [
        "# Paired ablation analysis",
        "",
        f"Paired cells: {len(by_key)}",
        f"CSV: `{out_csv}`",
        "",
        "## Robustness aggregates (union_weight vs baseline)",
        "",
        f"- failure_rate: {statistics.mean(failure_flags) if failure_flags else float('nan'):.3f}",
        f"- coverage_rate (mean trajectory_coverage): "
        f"{statistics.mean(coverage_vals) if coverage_vals else float('nan'):.3f}",
        f"- median paired relative regret: "
        f"{statistics.median(all_regrets) if all_regrets else float('nan'):.4f}",
        f"- worst_case_regret: {max(all_regrets) if all_regrets else float('nan'):.4f}",
        f"- CVaR_20: {cvar(all_regrets, 0.20):.4f}",
        f"- normalized_regret (mean): "
        f"{statistics.mean(all_regrets) if all_regrets else float('nan'):.4f}",
    ]
    if all_regrets:
        lo, hi = bootstrap_ci(all_regrets)
        lines.append(f"- bootstrap CI (mean relative regret): [{lo:.4f}, {hi:.4f}]")
    # Static-scene penalty: city_day_0_none / MH static-ish sequences.
    static_keys = [k for k in regrets_by_scene if "0_none" in k or "MH_01" in k or "MH_02" in k]
    static_vals = [r for k in static_keys for r in regrets_by_scene[k]]
    lines.append(
        f"- static_scene_penalty (mean relative regret on static/low-dynamic): "
        f"{statistics.mean(static_vals) if static_vals else float('nan'):.4f}"
    )
    lines.extend(["", "## Per-scene median paired delta vs baseline", ""])
    for scene in sorted(regrets_by_scene):
        vals = regrets_by_scene[scene]
        lines.append(f"- **{scene}**: median relative regret={statistics.median(vals):.4f} "
                     f"n={len(vals)}")

    out_md.write_text("\n".join(lines) + "\n")
    print(f"[export-paired] {len(paired_rows)} rows -> {out_csv}")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, required=True)
    ap.add_argument("--out-csv", type=Path, required=True)
    ap.add_argument("--out-md", type=Path, required=True)
    ap.add_argument("--include-oracle", action="store_true")
    ap.add_argument("--paired", action="store_true",
                    help="Also write paired deltas and robustness aggregates.")
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

    failed_or_missing = [r for r in records if r.status != "ok" or r.ate_rmse_m is None]
    lines = [
        "# Ablation analysis export",
        "",
        f"Runs: {len(rows_out)}",
        f"Failed/missing ATE cells: {len(failed_or_missing)}",
        f"CSV: `{args.out_csv}`",
        f"Oracle excluded: {not args.include_oracle}",
        "",
        "## Failed / missing cells (counted in failure rate)",
        "",
    ]
    if not failed_or_missing:
        lines.append("- none")
    else:
        for rec in failed_or_missing:
            lines.append(
                f"- `{rec.scene}` `{rec.method}` status={rec.status} "
                f"issues={','.join(rec.qc_issues) or 'no_ate'}"
            )

    args.out_md.write_text("\n".join(lines) + "\n")
    print(f"[export] {len(rows_out)} runs -> {args.out_csv}")

    if args.paired:
        paired_csv = args.out_csv.with_name(args.out_csv.stem + "_paired.csv")
        paired_md = args.out_md.with_name(args.out_md.stem + "_paired.md")
        write_paired(records, paired_csv, paired_md)


if __name__ == "__main__":
    main()
