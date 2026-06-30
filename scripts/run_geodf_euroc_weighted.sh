#!/usr/bin/env bash
# Run GeoDF-Weighted (Paper #2 proposed) on EuRoC static-safety sequences.
#
# Usage: ./scripts/run_geodf_euroc_weighted.sh [N] [SEQUENCES...]
set -eo pipefail

N="${1:-5}"
shift || true
if [ "$#" -eq 0 ]; then
    set -- MH_01_easy MH_02_easy MH_03_medium MH_04_difficult MH_05_difficult
fi

WS="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=scripts/lib/geodf_common.sh
source "${WS}/scripts/lib/geodf_common.sh"

EUROC="$(resolve_euroc_root)" || {
    echo "EuRoC not found. Set EUROC_ROOT=/path/to/euroc" >&2
    exit 1
}

source_ros2_ws "$WS"
MODE="$(geodf_method_to_mode weighted)"
OUT_ROOT="${WS}/results/euroc_repeat"
EUROC_CFG="$(ros2 pkg prefix pht_vio_ros)/share/pht_vio_ros/config/euroc"
mkdir -p "$OUT_ROOT" "${WS}/logs"

for SEQ in "$@"; do
    GROUP="$(euroc_group_for_seq "$SEQ")" || continue
    START="$(euroc_bag_start_s "$SEQ")"
    BAG="$(resolve_euroc_ros2_bag "$SEQ" "$WS")"
    GT="${EUROC}/${GROUP}/${SEQ}/${SEQ}/mav0/state_groundtruth_estimate0/data.csv"
    [ -d "$BAG" ] || bash "${WS}/scripts/euroc_prepare.sh" "$SEQ"
    [ -f "$GT" ] || { echo "[skip] no GT $SEQ"; continue; }
    RUN_CFG="${EUROC_CFG}/euroc_${MODE}_config_run_${SEQ}.yaml"
    for i in $(seq 1 "$N"); do
        out="${OUT_ROOT}/${SEQ}_weighted/trial_${i}"
        if [ "${FORCE:-0}" != "1" ] && [ -f "${out}/eval/metrics.json" ]; then
            echo "[have] ${SEQ}_weighted trial ${i}"
            continue
        fi
        mkdir -p "$out"
        cp "${EUROC_CFG}/euroc_${MODE}_config.yaml" "$RUN_CFG"
        sed -i "s|output_path: \"~/output/\"|output_path: \"${out}/\"|" "$RUN_CFG"
        sed -i "s|pose_graph_save_path: \"~/output/pose_graph/\"|pose_graph_save_path: \"${out}/pose_graph/\"|" "$RUN_CFG"
        echo "=== ${SEQ}_weighted trial ${i}/${N} start=${START}s ==="
        killall -9 pht_vio_node 2>/dev/null || true
        sleep 1
        run_pht_vio_benchmark "$RUN_CFG" "$out" "$BAG" "$START" 1.0
        python3 "${WS}/scripts/evaluate_trajectory.py" \
            "${out}/vio.csv" "$GT" "${out}/eval" --no-plot \
            --run-name "${SEQ}_weighted_t${i}" || echo "[warn] eval failed ${SEQ} t${i}"
    done
done
echo "[euroc weighted] done -> ${OUT_ROOT}"
