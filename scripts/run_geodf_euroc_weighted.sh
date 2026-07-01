#!/usr/bin/env bash
# Run the GeoDF-Weighted method together with the baseline it is compared
# against, on EuRoC static-safety sequences (N trials each for mean +/- std).
#
# Usage: ./scripts/run_geodf_euroc_weighted.sh [N] [SEQUENCES...]
# Env:
#   METHODS    space-separated methods (default "baseline weighted")
#   EUROC_ROOT EuRoC dataset root
#   FORCE=1    redo existing trials
set -eo pipefail

N="${1:-5}"
shift || true
if [ "$#" -eq 0 ]; then
    set -- MH_01_easy MH_02_easy MH_03_medium MH_04_difficult MH_05_difficult
fi
METHODS="${METHODS:-baseline weighted}"

WS="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=scripts/lib/geodf_common.sh
source "${WS}/scripts/lib/geodf_common.sh"

EUROC="$(resolve_euroc_root)" || {
    echo "EuRoC not found. Set EUROC_ROOT=/path/to/euroc" >&2
    exit 1
}

source_ros2_ws "$WS"
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
    for method in $METHODS; do
        MODE="$(geodf_method_to_mode "$method")" || continue
        RUN_CFG="${EUROC_CFG}/euroc_${MODE}_config_run_${SEQ}.yaml"
        for i in $(seq 1 "$N"); do
            out="${OUT_ROOT}/${SEQ}_${method}/trial_${i}"
            if [ "${FORCE:-0}" != "1" ] && [ -f "${out}/eval/metrics.json" ]; then
                echo "[have] ${SEQ}_${method} trial ${i}"
                continue
            fi
            mkdir -p "$out"
            cp "${EUROC_CFG}/euroc_${MODE}_config.yaml" "$RUN_CFG"
            sed -i "s|output_path: \"~/output/\"|output_path: \"${out}/\"|" "$RUN_CFG"
            sed -i "s|pose_graph_save_path: \"~/output/pose_graph/\"|pose_graph_save_path: \"${out}/pose_graph/\"|" "$RUN_CFG"
            echo "=== ${SEQ}_${method} trial ${i}/${N} start=${START}s (mode=${MODE}) ==="
            killall -9 pht_vio_node 2>/dev/null || true
            sleep 1
            run_pht_vio_benchmark "$RUN_CFG" "$out" "$BAG" "$START" 1.0
            python3 "${WS}/scripts/evaluate_trajectory.py" \
                "${out}/vio.csv" "$GT" "${out}/eval" --no-plot \
                --run-name "${SEQ}_${method}_t${i}" || echo "[warn] eval failed ${SEQ}_${method} t${i}"
        done
    done
done
echo "[euroc weighted] done -> ${OUT_ROOT}"
