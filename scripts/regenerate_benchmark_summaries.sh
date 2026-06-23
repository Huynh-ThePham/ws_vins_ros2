#!/usr/bin/env bash
# Regenerate results/euroc_*_summary.json from existing eval/metrics.json (no re-run).
set -eo pipefail
WS="$(cd "$(dirname "$0")/.." && pwd)"
BASELINE="${BASELINE_WS:-/home/theph/ws_vins}"

python3 << PY
import json, os

ws = "${WS}"
baseline = "${BASELINE}"
starts = {
    "MH_01_easy": "40", "MH_02_easy": "35", "MH_03_medium": "17p5",
    "MH_04_difficult": "15", "MH_05_difficult": "15",
}

vio_rows, loop_rows = [], []
for seq, tag in starts.items():
    vio_run = f"{seq}_stereo_imu_s{tag}"
    loop_run = f"{seq}_stereo_imu_loop_s{tag}"
    vio_m = os.path.join(ws, "results", vio_run, "eval", "metrics.json")
    loop_m = os.path.join(ws, "results", loop_run, "eval", "metrics.json")
    base_m = os.path.join(baseline, "results", vio_run, "eval", "metrics.json")

    if os.path.isfile(vio_m):
        mod = json.load(open(vio_m))
        base = json.load(open(base_m)) if os.path.isfile(base_m) else {}
        b = base.get("ate_rmse_m")
        m = mod.get("ate_rmse_m")
        delta = (m - b) / b * 100 if b else 0
        vio_rows.append({
            "sequence": seq,
            "baseline_ate_m": b,
            "post_refactor_ate_m": m,
            "delta_pct": round(delta, 2) if b else None,
            "poses_baseline": base.get("n_poses_est"),
            "poses_post_refactor": mod.get("n_poses_est"),
        })

    if os.path.isfile(loop_m):
        mod = json.load(open(loop_m))
        loop_rows.append({
            "sequence": seq,
            "ate_m": mod.get("ate_rmse_m"),
            "poses": mod.get("n_poses_est"),
        })

vio_out = {
    "benchmark": "EuRoC Machine Hall stereo+IMU (post VinsConfig + loop_closure decouple)",
    "modular_workspace": ws,
    "baseline_workspace": baseline,
    "protocol": "bag_start per OpenVINS convention, evo APE SE(3)",
    "results": vio_rows,
}
json.dump(vio_out, open(os.path.join(ws, "results", "euroc_post_refactor_summary.json"), "w"), indent=2)
json.dump(loop_rows, open(os.path.join(ws, "results", "euroc_loop_post_refactor_summary.json"), "w"), indent=2)
print(f"VIO summary: {len(vio_rows)} sequences")
print(f"Loop summary: {len(loop_rows)} sequences")
PY
