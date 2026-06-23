#!/usr/bin/env bash
# Run all Machine Hall stereo+IMU benchmarks and summarize ATE.
set -eo pipefail
WS="$(cd "$(dirname "$0")/.." && pwd)"
SCRIPT="${WS}/scripts/run_euroc_benchmark.sh"
SUMMARY="${WS}/results/euroc_post_refactor_summary.json"

killall -9 pht_vio_node pht_loop_closure_node 2>/dev/null || true
sleep 1

SEQ_LIST=(MH_01_easy MH_02_easy MH_03_medium MH_04_difficult MH_05_difficult)
RESULTS=()

for SEQ in "${SEQ_LIST[@]}"; do
  echo "========== $SEQ =========="
  if ! "$SCRIPT" "$SEQ" stereo_imu; then
    echo "FAILED: $SEQ" >&2
    exit 1
  fi
done

python3 << PY
import json, glob, os
ws = "${WS}"
baseline_ws = "/home/theph/ws_vins"
rows = []
for seq in ["MH_01_easy", "MH_02_easy", "MH_03_medium", "MH_04_difficult", "MH_05_difficult"]:
    starts = {"MH_01_easy": "40", "MH_02_easy": "35", "MH_03_medium": "17p5", "MH_04_difficult": "15", "MH_05_difficult": "15"}
    tag = starts[seq]
    run = f"{seq}_stereo_imu_s{tag}"
    mod_path = os.path.join(ws, "results", run, "eval", "metrics.json")
    base_path = os.path.join(baseline_ws, "results", run, "eval", "metrics.json")
    mod = json.load(open(mod_path))
    base = json.load(open(base_path))
    b_ate = base["ate_rmse_m"]
    m_ate = mod["ate_rmse_m"]
    delta = (m_ate - b_ate) / b_ate * 100 if b_ate else 0
    rows.append({
        "sequence": seq,
        "baseline_ate_m": b_ate,
        "post_refactor_ate_m": m_ate,
        "delta_pct": round(delta, 2),
        "poses_baseline": base.get("n_poses_est"),
        "poses_post_refactor": mod.get("n_poses_est"),
    })
    print(f"{seq}: baseline={b_ate:.6f} post_refactor={m_ate:.6f} delta={delta:+.2f}%")

out = {
    "benchmark": "EuRoC Machine Hall stereo+IMU (post VinsConfig + loop_closure decouple)",
    "modular_workspace": ws,
    "baseline_workspace": baseline_ws,
    "protocol": "bag_start per OpenVINS convention, evo APE SE(3)",
    "results": rows,
}
json.dump(out, open("${SUMMARY}", "w"), indent=2)
print("Wrote", "${SUMMARY}")
PY
