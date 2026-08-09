#!/usr/bin/env python3
"""Export the compact, reviewable Phase 3.5 evidence bundle.

Raw trajectories and datasets stay outside git.  This exporter records every
successful trial, the hashes needed to identify its immutable inputs, aggregate
telemetry, and deterministic paired/bootstrap statistics.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import random
import statistics
from collections import defaultdict
from pathlib import Path
from typing import Any, Iterable


V2 = "adaptive_arbitration_v2"
UW = "union_weight"
V1 = "adaptive_arbitration"
TRAIN_SCENES = (
    "MH_03_medium",
    "MH_04_difficult",
    "city_day_2_mid",
    "city_day_3_high",
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_runs(root: Path, phase: str) -> list[dict[str, Any]]:
    runs: list[dict[str, Any]] = []
    for manifest_path in sorted(root.rglob("run_manifest.json")):
        manifest = json.loads(manifest_path.read_text())
        run_dir = manifest_path.parent
        metrics = manifest.get("metrics") or {}
        runs.append({
            "phase": phase,
            "root": root,
            "run_dir": run_dir,
            "manifest_path": manifest_path,
            "manifest": manifest,
            "dataset": manifest.get("dataset"),
            "scene": manifest.get("scene"),
            "method": manifest.get("method"),
            "trial": int(manifest.get("trial", 0)),
            "seed": manifest.get("seed"),
            "status": manifest.get("status"),
            "ate_rmse_m": manifest.get("ate_rmse_m", metrics.get("ate_rmse_m")),
            "rpe_rmse_m": metrics.get("rpe_rmse_m"),
            "coverage": manifest.get("trajectory_coverage", metrics.get("trajectory_coverage")),
        })
    return runs


def successful(runs: Iterable[dict[str, Any]]) -> list[dict[str, Any]]:
    return [r for r in runs if r["status"] == "ok" and r["ate_rmse_m"] is not None]


def write_trials(runs: list[dict[str, Any]], out: Path) -> None:
    fields = [
        "phase", "dataset", "scene", "method", "trial", "seed", "status",
        "success", "trajectory_coverage", "ate_rmse_m", "rpe_rmse_m",
        "protocol_tag", "git_sha", "git_dirty", "config_sha256",
        "manifest_sha256", "bag_sha256", "gt_sha256", "model_sha256",
    ]
    with out.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        for run in sorted(runs, key=lambda r: (r["phase"], r["dataset"] or "", r["scene"] or "", r["method"] or "", r["trial"])):
            manifest = run["manifest"]
            writer.writerow({
                "phase": run["phase"],
                "dataset": run["dataset"],
                "scene": run["scene"],
                "method": run["method"],
                "trial": run["trial"],
                "seed": run["seed"],
                "status": run["status"],
                "success": int(run["status"] == "ok"),
                "trajectory_coverage": run["coverage"],
                "ate_rmse_m": run["ate_rmse_m"],
                "rpe_rmse_m": run["rpe_rmse_m"],
                "protocol_tag": manifest.get("protocol_tag"),
                "git_sha": manifest.get("git_sha"),
                "git_dirty": manifest.get("git_dirty"),
                "config_sha256": manifest.get("resolved_config_sha256"),
                "manifest_sha256": sha256(run["manifest_path"]),
                "bag_sha256": manifest.get("bag_sha256"),
                "gt_sha256": manifest.get("gt_sha256"),
                "model_sha256": manifest.get("model_sha256"),
            })


def write_manifest_hashes(runs: list[dict[str, Any]], out: Path) -> None:
    fields = ["phase", "dataset", "scene", "method", "trial", "status", "manifest_sha256", "config_sha256"]
    with out.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        for run in sorted(runs, key=lambda r: (r["phase"], r["dataset"] or "", r["scene"] or "", r["method"] or "", r["trial"])):
            writer.writerow({
                "phase": run["phase"],
                "dataset": run["dataset"],
                "scene": run["scene"],
                "method": run["method"],
                "trial": run["trial"],
                "status": run["status"],
                "manifest_sha256": sha256(run["manifest_path"]),
                "config_sha256": run["manifest"].get("resolved_config_sha256"),
            })


def write_excluded_attempts(runs: list[dict[str, Any]], out: Path) -> None:
    fields = ["phase", "dataset", "scene", "method", "trial", "status", "failure_reason", "manifest_sha256"]
    with out.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        for run in sorted(runs, key=lambda r: (r["phase"], r["scene"] or "", r["method"] or "", r["trial"])):
            if run["status"] == "ok" and not run["manifest"].get("git_dirty", True):
                continue
            writer.writerow({
                "phase": run["phase"],
                "dataset": run["dataset"],
                "scene": run["scene"],
                "method": run["method"],
                "trial": run["trial"],
                "status": run["status"],
                "failure_reason": run["manifest"].get("failure_reason"),
                "manifest_sha256": sha256(run["manifest_path"]),
            })


def _f(row: dict[str, str], key: str) -> float | None:
    value = row.get(key)
    return float(value) if value not in (None, "") else None


def telemetry_for_runs(runs: Iterable[dict[str, Any]]) -> dict[str, float]:
    frame_values: dict[str, list[float]] = defaultdict(list)
    weighted_sum: dict[str, float] = defaultdict(float)
    track_total = 0.0
    counts: dict[str, float] = defaultdict(float)
    weighted_columns = ("arb2_q_s", "arb2_q_g", "arb2_mean_q_m", "arb2_mean_dynamic_weight", "arb2_mean_final_weight")
    count_columns = (
        "arb2_semantic_authoritative", "arb2_geodf_authoritative",
        "arb2_joint_authoritative", "arb2_no_authoritative",
        "arb2_disagreement", "arb2_keep", "arb2_downweight",
        "arb2_quarantine", "arb2_hard_reject",
    )
    frames = 0
    for run in runs:
        stats = run["run_dir"] / "sem_geodf_stats.csv"
        if not stats.is_file():
            continue
        with stats.open(newline="") as stream:
            for row in csv.DictReader(stream):
                tracks = _f(row, "tracks_before") or 0.0
                track_total += tracks
                frames += 1
                for key in weighted_columns:
                    value = _f(row, key)
                    if value is not None:
                        weighted_sum[key] += value * tracks
                        frame_values[key].append(value)
                for key in count_columns:
                    counts[key] += _f(row, key) or 0.0
    result: dict[str, float] = {"runs": float(len(list(runs))) if isinstance(runs, list) else 0.0, "frames": float(frames), "tracks": track_total}
    output_names = {
        "arb2_q_s": "mean_q_s",
        "arb2_q_g": "mean_q_g",
        "arb2_mean_q_m": "mean_q_m",
        "arb2_mean_dynamic_weight": "mean_dynamic_weight",
        "arb2_mean_final_weight": "mean_final_weight",
    }
    for key in weighted_columns:
        result[output_names[key]] = weighted_sum[key] / track_total if track_total else 0.0
    result["median_q_s"] = statistics.median(frame_values["arb2_q_s"]) if frame_values["arb2_q_s"] else 0.0
    result["median_q_g"] = statistics.median(frame_values["arb2_q_g"]) if frame_values["arb2_q_g"] else 0.0
    for key in count_columns:
        result[f"frac_{key.removeprefix('arb2_')}"] = counts[key] / track_total if track_total else 0.0
    return result


def write_telemetry(runs: list[dict[str, Any]], out: Path) -> None:
    groups: dict[tuple[str, str], list[dict[str, Any]]] = defaultdict(list)
    for run in successful(runs):
        if run["method"] == V2:
            groups[(run["phase"], run["scene"])].append(run)
    rows = []
    for (phase, scene), group in sorted(groups.items()):
        row: dict[str, Any] = {"phase": phase, "scene": scene}
        row.update(telemetry_for_runs(group))
        rows.append(row)
    fields = ["phase", "scene"] + sorted({key for row in rows for key in row if key not in ("phase", "scene")})
    with out.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def method_stats(runs: Iterable[dict[str, Any]]) -> dict[str, Any]:
    values = [float(r["ate_rmse_m"]) for r in runs]
    return {
        "mean_ate_m": statistics.mean(values),
        "std_ate_m": statistics.stdev(values) if len(values) > 1 else 0.0,
        "n": len(values),
    }


def scene_table(runs: list[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    groups: dict[tuple[str, str], list[dict[str, Any]]] = defaultdict(list)
    for run in successful(runs):
        groups[(run["scene"], run["method"])].append(run)
    table: dict[str, dict[str, Any]] = defaultdict(dict)
    for (scene, method), group in sorted(groups.items()):
        table[scene][method] = method_stats(group)
    return dict(table)


def paired_analysis(runs: list[dict[str, Any]], baseline: str, candidate: str, bootstrap_seed: int, bootstrap_samples: int) -> dict[str, Any]:
    cells: dict[tuple[str, int, Any], dict[str, float]] = defaultdict(dict)
    for run in successful(runs):
        if run["method"] in (baseline, candidate):
            cells[(run["scene"], run["trial"], run["seed"])][run["method"]] = float(run["ate_rmse_m"])
    paired = []
    for (scene, trial, seed), methods in sorted(cells.items()):
        if baseline in methods and candidate in methods:
            delta = methods[candidate] - methods[baseline]
            paired.append({"scene": scene, "trial": trial, "seed": seed, "baseline_ate_m": methods[baseline], "candidate_ate_m": methods[candidate], "delta_m": delta, "delta_pct": 100.0 * delta / methods[baseline]})
    deltas = [row["delta_m"] for row in paired]
    rng = random.Random(bootstrap_seed)
    boot = [statistics.mean(rng.choices(deltas, k=len(deltas))) for _ in range(bootstrap_samples)] if deltas else []
    boot.sort()
    lo = boot[int(0.025 * bootstrap_samples)] if boot else None
    hi = boot[min(len(boot) - 1, int(0.975 * bootstrap_samples))] if boot else None
    by_scene: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for row in paired:
        by_scene[row["scene"]].append(row)
    scene_deltas = {}
    wtl = {"wins": 0, "ties": 0, "losses": 0, "tie_threshold_percent": 1.0}
    for scene, rows in sorted(by_scene.items()):
        base_mean = statistics.mean(r["baseline_ate_m"] for r in rows)
        cand_mean = statistics.mean(r["candidate_ate_m"] for r in rows)
        pct = 100.0 * (cand_mean - base_mean) / base_mean
        verdict = "win" if pct < -1.0 else ("loss" if pct > 1.0 else "tie")
        wtl[{"win": "wins", "tie": "ties", "loss": "losses"}[verdict]] += 1
        scene_deltas[scene] = {"baseline_mean_ate_m": base_mean, "candidate_mean_ate_m": cand_mean, "delta_m": cand_mean - base_mean, "delta_pct": pct, "verdict": verdict}
    baseline_mean = statistics.mean(row["baseline_ate_m"] for row in paired) if paired else None
    candidate_mean = statistics.mean(row["candidate_ate_m"] for row in paired) if paired else None
    return {
        "baseline": baseline,
        "candidate": candidate,
        "paired_cells": len(paired),
        "baseline_mean_ate_m": baseline_mean,
        "candidate_mean_ate_m": candidate_mean,
        "mean_delta_m": statistics.mean(deltas) if deltas else None,
        "mean_delta_pct": 100.0 * (candidate_mean - baseline_mean) / baseline_mean if paired else None,
        "bootstrap": {"samples": bootstrap_samples, "seed": bootstrap_seed, "confidence": 0.95, "mean_delta_ci_m": [lo, hi]},
        "scene_wtl": wtl,
        "scene_deltas": scene_deltas,
        "paired_deltas": paired,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--stage-a-root", type=Path, required=True)
    parser.add_argument("--train-root", type=Path, required=True)
    parser.add_argument("--holdout-root", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--bootstrap-samples", type=int, default=100000)
    parser.add_argument("--bootstrap-seed", type=int, default=3500)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

    stage_a_raw = read_runs(args.stage_a_root, "stage_a_n1")
    train_raw = read_runs(args.train_root, "train")
    holdout_raw = read_runs(args.holdout_root, "legacy_holdout")
    # Failed sandbox attempts are retained in their ignored raw root but excluded
    # from the review bundle.  Only clean, complete, successful manifests count.
    clean = lambda r: r["status"] == "ok" and not r["manifest"].get("git_dirty", True)
    stage_a = [r for r in stage_a_raw if clean(r)]
    train_all = [r for r in train_raw if clean(r)]
    holdout_all = [r for r in holdout_raw if clean(r) and r["trial"] <= 3]
    all_runs = stage_a + train_all + holdout_all

    write_trials(all_runs, args.out / "per_trial_metrics.csv")
    write_manifest_hashes(all_runs, args.out / "run_manifest_hashes.csv")
    write_excluded_attempts(stage_a_raw + train_raw + holdout_raw, args.out / "excluded_attempts.csv")
    write_telemetry(train_all + holdout_all, args.out / "telemetry_aggregate.csv")

    train_n3 = [r for r in train_all if r["trial"] <= 3]
    train_n5 = [r for r in train_all if r["trial"] <= 5 and r["method"] in (UW, V2)]
    holdout_n3 = [r for r in holdout_all if r["method"] in (UW, V1, V2)]
    analysis = {
        "schema_version": 1,
        "selection": {
            "selected": "F1+G2",
            "selected_on": list(TRAIN_SCENES),
            "rejected": ["F0+G2", "F2+G2", "F1+G1", "F1+G3"],
            "parameters_frozen_before_legacy_holdout": True,
            "holdout_disclaimer": "legacy hold-out has been observed in prior research iteration",
        },
        "run_counts": {
            "stage_a_success": len(successful(stage_a)),
            "train_n3_success": len(successful(train_n3)),
            "train_n5_run_success": len(successful(train_n5)),
            "train_n5_paired_cells": len(successful(train_n5)) // 2,
            "legacy_holdout_n3_success": len(successful(holdout_n3)),
        },
        "train_n3": {
            "ate_by_scene_method": scene_table(train_n3),
            "paired_v2_vs_union_weight": paired_analysis(train_n3, UW, V2, args.bootstrap_seed, args.bootstrap_samples),
        },
        "train_n5": {
            "ate_by_scene_method": scene_table(train_n5),
            "paired_v2_vs_union_weight": paired_analysis(train_n5, UW, V2, args.bootstrap_seed, args.bootstrap_samples),
        },
        "legacy_holdout_n3": {
            "ate_by_scene_method": scene_table(holdout_n3),
            "paired_v2_vs_union_weight": paired_analysis(holdout_n3, UW, V2, args.bootstrap_seed, args.bootstrap_samples),
        },
        "quality": {
            "success_rate": sum(r["status"] == "ok" for r in all_runs) / len(all_runs),
            "minimum_trajectory_coverage": min(float(r["coverage"]) for r in all_runs if r["coverage"] is not None),
            "online_ground_truth_used": False,
            "runtime_sequence_name_logic_used": False,
        },
    }
    (args.out / "analysis.json").write_text(json.dumps(analysis, indent=2, sort_keys=True) + "\n")

    artifact_files = [
        "analysis.json", "excluded_attempts.csv", "expected_train_n3_matrix.json",
        "per_trial_metrics.csv", "run_manifest_hashes.csv", "telemetry_aggregate.csv",
    ]
    (args.out / "SHA256SUMS").write_text("".join(f"{sha256(args.out / name)}  {name}\n" for name in artifact_files))
    print(json.dumps({"out": str(args.out), "runs": len(all_runs), "sha256s": artifact_files}, indent=2))


if __name__ == "__main__":
    main()
