#!/usr/bin/env bash
# Run SAD-VINS or baseline on EuRoC (ROS 2 native, stereo + IMU).
#
# Usage: ./scripts/run_sad_euroc.sh [SEQUENCE] [METHOD] [START_S] [--eval]
#
# METHOD:
#   baseline  -> euroc_stereo_imu_config.yaml (no semantic filter)
#   sad_sem   -> euroc_stereo_imu_sem_config.yaml (+ YOLO mask node)
#   sgta      -> euroc_stereo_imu_sgta_config.yaml (+ YOLO + geometric/temporal gate)
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
METHOD="${ARGS[1]:-sad_sem}"
START="${ARGS[2]:-}"

WS="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=scripts/lib/sad_common.sh
source "${WS}/scripts/lib/sad_common.sh"

EUROC="$(resolve_euroc_root)" || {
    echo "EuRoC not found. Set EUROC_ROOT=/path/to/euroc" >&2
    exit 1
}

GROUP="$(euroc_group_for_seq "$SEQ")" || { echo "Unknown seq: $SEQ"; exit 1; }
if [ -z "$START" ]; then
    START="$(euroc_bag_start_s "$SEQ")"
fi

BAG="${EUROC}/${GROUP}/${SEQ}/ros2_bag"
GT="${EUROC}/${GROUP}/${SEQ}/${SEQ}/mav0/state_groundtruth_estimate0/data.csv"
[ -d "$BAG" ] || { echo "Missing ros2_bag: $BAG"; exit 1; }
[ -f "$GT" ] || { echo "Missing GT: $GT"; exit 1; }

MODE="$(sad_method_to_mode "$METHOD")"
TAG="$(start_tag "$START")"
OUT="${WS}/results/sad/${SEQ}_${METHOD}_s${TAG}"

source_ros2_ws "$WS"
killall -9 pht_vio_node mask_node 2>/dev/null || true
sleep 1

EUROC_CFG="$(ros2 pkg prefix pht_vio_ros)/share/pht_vio_ros/config/euroc"
RUN_CFG="${EUROC_CFG}/euroc_${MODE}_config_run_${SEQ}.yaml"
mkdir -p "$OUT"
cp "${EUROC_CFG}/euroc_${MODE}_config.yaml" "$RUN_CFG"
sed -i "s|output_path: \"~/output/\"|output_path: \"${OUT}/\"|" "$RUN_CFG"
sed -i "s|pose_graph_save_path: \"~/output/pose_graph/\"|pose_graph_save_path: \"${OUT}/pose_graph/\"|" "$RUN_CFG"
apply_sgta_method_overrides "$METHOD" "$RUN_CFG"

USE_YOLO="$(sad_method_uses_yolo "$METHOD")"

echo "[sad] seq=$SEQ method=$METHOD mode=$MODE yolo=$USE_YOLO start=${START}s out=$OUT"
run_sad_vio_benchmark "$RUN_CFG" "$OUT" "$BAG" "$START" 1.0 "$USE_YOLO"

if [ "$EVAL" = "1" ]; then
    python3 "${WS}/scripts/evaluate_trajectory.py" \
        "${OUT}/vio.csv" "$GT" "${OUT}/eval" \
        --no-plot --run-name "$(basename "$OUT")"
    python3 "${WS}/scripts/sem_filter_metrics.py" --run-dir "$OUT" --json "${OUT}/sem_filter_metrics.json" 2>/dev/null || true
fi

echo "[done] $(basename "$OUT") poses=$(wc -l < "${OUT}/vio.csv")${EVAL:+ eval=${OUT}/eval/metrics.json}"
