#!/usr/bin/env python3
"""Convert VINS-Fusion vio.csv + EuRoC ground-truth to TUM format and run evo_ape.

Usage:
    evaluate_trajectory.py <vio.csv> <groundtruth/data.csv> <out_dir>

Writes <out_dir>/{est_tum.txt, gt_tum.txt, ate.txt, rpe.txt} plus an evo plot.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path


def _parse_evo_metrics(text: str) -> dict[str, float]:
    """Extract rmse/mean/median/max from evo stdout."""
    out: dict[str, float] = {}
    for key in ("rmse", "mean", "median", "max", "min", "std"):
        m = re.search(rf"^\s*{key}\s+([0-9.eE+-]+)\s*$", text, re.MULTILINE)
        if m:
            out[key] = float(m.group(1))
    return out


def _parse_run_metadata(eval_dir: Path, run_name: str = "") -> dict:
    """Infer sequence/mode/loop/skip from results/<name>/eval parent folder."""
    name = run_name or eval_dir.parent.name
    loop = "_loop" in name
    base = name.replace("_loop", "")
    bag_start_s = 0.0
    m = re.match(r"^(?P<prefix>.+)_s(?P<skip>\d+(?:p\d+)?)$", base)
    if m:
        base = m.group("prefix")
        bag_start_s = float(m.group("skip").replace("p", "."))

    sequence, mode = base, "unknown"
    for candidate in ("stereo_imu", "mono_imu", "stereo"):
        suffix = f"_{candidate}"
        if base.endswith(suffix):
            sequence = base[: -len(suffix)]
            mode = candidate
            break

    return {
        "run_dir": name,
        "sequence": sequence,
        "mode": mode,
        "loop_fusion": loop,
        "bag_start_s": bag_start_s,
    }


def _convert(src: Path, dst: Path, label: str, nfields_min: int) -> int:
    """Convert EuRoC-style CSV (ns timestamp + position + quaternion) to TUM."""
    n = 0
    with src.open() as f, dst.open("w") as out:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = [p for p in line.replace(" ", "").split(",") if p != ""]
            if len(parts) < nfields_min:
                continue
            try:
                ts_ns = int(parts[0])
                x, y, z = float(parts[1]), float(parts[2]), float(parts[3])
                qw = float(parts[4])
                qx = float(parts[5])
                qy = float(parts[6])
                qz = float(parts[7])
            except ValueError:
                continue
            ts_s = ts_ns * 1e-9
            out.write(f"{ts_s:.9f} {x:.6f} {y:.6f} {z:.6f} "
                      f"{qx:.9f} {qy:.9f} {qz:.9f} {qw:.9f}\n")
            n += 1
    print(f"[{label}] {n} poses -> {dst}")
    return n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("est_csv", type=Path, help="VINS-Fusion vio.csv")
    ap.add_argument("gt_csv", type=Path, help="EuRoC mav0/state_groundtruth_estimate0/data.csv")
    ap.add_argument("out_dir", type=Path)
    ap.add_argument("--no-plot", action="store_true", help="skip evo plot generation")
    ap.add_argument("--run-name", type=str, default="", help="results folder name for metadata")
    args = ap.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    est = args.out_dir / "est_tum.txt"
    gt = args.out_dir / "gt_tum.txt"

    n_est = _convert(args.est_csv, est, "est", 8)
    n_gt = _convert(args.gt_csv, gt, "gt", 8)
    if n_est == 0 or n_gt == 0:
        print("ERROR: empty trajectory.", file=sys.stderr)
        sys.exit(1)

    cmd_ate = [
        "evo_ape", "tum", str(gt), str(est),
        "-a",
        "--t_max_diff", "0.02",
        "--no_warnings",
    ]
    print("[evo_ape SE(3)]", " ".join(cmd_ate))
    res = subprocess.run(cmd_ate, capture_output=True, text=True)
    print(res.stdout)
    if res.returncode != 0:
        print(res.stderr, file=sys.stderr)
        sys.exit(res.returncode)
    (args.out_dir / "ate.txt").write_text(res.stdout + "\n" + res.stderr)
    ate_metrics = _parse_evo_metrics(res.stdout)

    cmd_rpe = [
        "evo_rpe", "tum", str(gt), str(est),
        "-a",
        "--t_max_diff", "0.02",
        "--delta", "1", "--delta_unit", "m",
        "--no_warnings",
    ]
    print("[evo_rpe 1s]", " ".join(cmd_rpe))
    res = subprocess.run(cmd_rpe, capture_output=True, text=True)
    print(res.stdout)
    if res.returncode != 0:
        print(res.stderr, file=sys.stderr)
        sys.exit(res.returncode)
    (args.out_dir / "rpe.txt").write_text(res.stdout + "\n" + res.stderr)
    rpe_metrics = _parse_evo_metrics(res.stdout)

    meta = _parse_run_metadata(args.out_dir, args.run_name)
    if args.run_name:
        meta["run_dir"] = args.run_name
    metrics = {
        **meta,
        "ate_rmse_m": ate_metrics.get("rmse"),
        "ate_mean_m": ate_metrics.get("mean"),
        "ate_median_m": ate_metrics.get("median"),
        "ate_max_m": ate_metrics.get("max"),
        "rpe_rmse_m": rpe_metrics.get("rmse"),
        "n_poses_est": n_est,
        "n_poses_gt": n_gt,
    }
    (args.out_dir / "metrics.json").write_text(json.dumps(metrics, indent=2) + "\n")
    rmse = metrics["ate_rmse_m"]
    if rmse is not None:
        print(f"[metrics] ate_rmse={rmse:.4f} m -> {args.out_dir / 'metrics.json'}")
    else:
        print(f"[metrics] wrote {args.out_dir / 'metrics.json'} (no rmse parsed)")

    if not args.no_plot:
        plot_path = args.out_dir / "trajectory.png"
        cmd_plot = [
            "evo_traj", "tum", str(est),
            "--ref", str(gt),
            "-a",
            "--t_max_diff", "0.02",
            "--plot_mode", "xy",
            "--save_plot", str(plot_path),
        ]
        print("[evo_traj plot]", " ".join(cmd_plot))
        env = os.environ.copy()
        env["MPLBACKEND"] = "Agg"
        subprocess.run(cmd_plot, env=env, check=False)


if __name__ == "__main__":
    main()
