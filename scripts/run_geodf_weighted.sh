#!/usr/bin/env bash
# Run GeoDF-Weighted on VIODE conditions using the prepared
# ROS 2 bags + cached ground-truth CSVs (the raw .bag files may be unmounted).
# Reuses existing trials unless FORCE=1.
#
# Usage: ./scripts/run_geodf_weighted.sh "[LEVELS]" "[ENVS]" [N]
#   LEVELS default: "0_none 1_low 2_mid 3_high"
#   ENVS   default: "city_day city_night parking_lot"
#   N      default: 5
set -eo pipefail

LEVELS="${1:-0_none 1_low 2_mid 3_high}"
ENVS="${2:-city_day city_night parking_lot}"
N="${3:-5}"

WS="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=scripts/lib/geodf_common.sh
source "${WS}/scripts/lib/geodf_common.sh"
source_ros2_ws "$WS"

OUT_ROOT="${WS}/results/viode_repeat"
CFG_DIR="$(ros2 pkg prefix pht_vio_ros)/share/pht_vio_ros/config/viode"
mode="$(geodf_method_to_mode weighted)"
mkdir -p "$OUT_ROOT" "${WS}/logs"

for env in $ENVS; do
    for level in $LEVELS; do
        bag="${WS}/data/viode_ros2/${env}/${level}/ros2_bag"
        gt="${OUT_ROOT}/${env}_${level}_gt.csv"
        [ -d "$bag" ] || { echo "[skip] no bag ${env}/${level}"; continue; }
        [ -f "$gt" ]  || { echo "[skip] no gt ${env}/${level}";  continue; }
        RUN_CFG="${CFG_DIR}/viode_${mode}_config_run_${env}_${level}.yaml"
        for i in $(seq 1 "$N"); do
            out="${OUT_ROOT}/${env}_${level}_weighted/trial_${i}"
            if [ "${FORCE:-0}" != "1" ] && [ -f "${out}/eval/metrics.json" ]; then
                echo "[have] ${env}_${level}_weighted trial ${i}"
                continue
            fi
            mkdir -p "$out"
            cp "${CFG_DIR}/viode_${mode}_config.yaml" "$RUN_CFG"
            sed -i "s|output_path: \"~/output/\"|output_path: \"${out}/\"|" "$RUN_CFG"
            sed -i "s|pose_graph_save_path: \"~/output/pose_graph/\"|pose_graph_save_path: \"${out}/pose_graph/\"|" "$RUN_CFG"
            echo "=== ${env}_${level}_weighted trial ${i}/${N} (mode=${mode}) ==="
            killall -9 pht_vio_node 2>/dev/null || true
            sleep 1
            run_pht_vio_benchmark "$RUN_CFG" "$out" "$bag" 0 1.0
            python3 "${WS}/scripts/evaluate_trajectory.py" \
                "${out}/vio.csv" "$gt" "${out}/eval" --no-plot \
                --run-name "${env}_${level}_weighted_t${i}" || echo "[warn] eval failed ${env}_${level} t${i}"
        done
    done
done
echo "[weighted] done -> ${OUT_ROOT}"
