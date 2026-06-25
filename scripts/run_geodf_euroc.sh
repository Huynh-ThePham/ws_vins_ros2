#!/usr/bin/env bash
# Run GeoDF-VINS-Hard or baseline on EuRoC (ROS 2 native).
#
# Usage: ./scripts/run_geodf_euroc.sh [SEQUENCE] [METHOD] [START_S] [--eval]
#
# METHOD (aliases match ws_vins):
#   baseline      -> euroc_stereo_imu_config.yaml
#   geodf_hard    -> euroc_stereo_imu_geodf_config.yaml
#   alwayson      -> euroc_stereo_imu_geodf_dump_config.yaml (always-on + feature dump)
#   geodf_dump    -> same as alwayson
#   adaptive      -> PROPOSED: scene-aware + auto-ρ_on (B), stereo OFF
#   adaptive_fixed -> ablation: fixed ρ_on=0.12 (dataset-tuned oracle)
#   adaptive_v2   -> ablation: auto-ρ_on + stereo cross-check (F)
#   geodf_noguard -> ablation without ratio guard
set -eo pipefail

EVAL=0
ARGS=()
for arg in "$@"; do
    case "$arg" in
        --eval) EVAL=1 ;;
        *) ARGS+=("$arg") ;;
    esac
done

SEQ="${ARGS[0]:-MH_01_easy}"
METHOD="${ARGS[1]:-geodf_hard}"
START="${ARGS[2]:-}"

WS="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=scripts/lib/geodf_common.sh
source "${WS}/scripts/lib/geodf_common.sh"

EUROC="$(resolve_euroc_root)" || {
    echo "EuRoC not found. Set EUROC_ROOT=/path/to/euroc" >&2
    exit 1
}

GROUP="$(euroc_group_for_seq "$SEQ")" || { echo "Unknown seq: $SEQ"; exit 1; }
if [ -z "$START" ]; then
    START="$(euroc_bag_start_s "$SEQ")"
fi

BAG="$(resolve_euroc_ros2_bag "$SEQ" "$WS")"
GT="${EUROC}/${GROUP}/${SEQ}/${SEQ}/mav0/state_groundtruth_estimate0/data.csv"
if [ ! -f "$GT" ] || [ ! -d "$BAG" ] || [ ! -f "${BAG}/metadata.yaml" ]; then
    bash "${WS}/scripts/euroc_prepare.sh" "$SEQ"
fi
[ -d "$BAG" ] || { echo "Missing ros2_bag: $BAG"; exit 1; }
[ -f "$GT" ] || { echo "Missing GT: $GT"; exit 1; }

MODE="$(geodf_method_to_mode "$METHOD")"
TAG="$(start_tag "$START")"
OUT="${WS}/results/geodf/${SEQ}_${METHOD}_s${TAG}"

source_ros2_ws "$WS"
killall -9 pht_vio_node 2>/dev/null || true
sleep 1

EUROC_CFG="$(ros2 pkg prefix pht_vio_ros)/share/pht_vio_ros/config/euroc"
RUN_CFG="${EUROC_CFG}/euroc_${MODE}_config_run_${SEQ}.yaml"
mkdir -p "$OUT"
cp "${EUROC_CFG}/euroc_${MODE}_config.yaml" "$RUN_CFG"
sed -i "s|output_path: \"~/output/\"|output_path: \"${OUT}/\"|" "$RUN_CFG"
sed -i "s|pose_graph_save_path: \"~/output/pose_graph/\"|pose_graph_save_path: \"${OUT}/pose_graph/\"|" "$RUN_CFG"

echo "[geodf] seq=$SEQ method=$METHOD mode=$MODE start=${START}s out=$OUT"
run_pht_vio_benchmark "$RUN_CFG" "$OUT" "$BAG" "$START" 1.0

if [ "$EVAL" = "1" ]; then
    python3 "${WS}/scripts/evaluate_trajectory.py" \
        "${OUT}/vio.csv" "$GT" "${OUT}/eval" \
        --no-plot --run-name "$(basename "$OUT")"
    python3 "${WS}/scripts/geodf_filter_metrics.py" --run-dir "$OUT" --json "${OUT}/geodf_filter_metrics.json" 2>/dev/null || true
fi

echo "[done] $(basename "$OUT") poses=$(wc -l < "${OUT}/vio.csv")${EVAL:+ eval=${OUT}/eval/metrics.json}"
