#!/usr/bin/env bash
# Run offline VIO on EuRoC mav0 and evaluate ATE (compare with ROS bag baseline).
set -eo pipefail

SEQ="${1:-MH_04_difficult}"
START="${2:-15}"
WS="$(cd "$(dirname "$0")/.." && pwd)"
EUROC_ROOT="${EUROC_ROOT:-/media/theph/Data1/ws_research_datasets/raw_datasets/euroc}"

case "$SEQ" in
  MH_*) GROUP=machine_hall ;;
  *) echo "Unknown sequence: $SEQ"; exit 1 ;;
esac

MAV="${EUROC_ROOT}/${GROUP}/${SEQ}/${SEQ}/mav0"
GT="${EUROC_ROOT}/${GROUP}/${SEQ}/${SEQ}/mav0/state_groundtruth_estimate0/data.csv"
[ -d "$MAV" ] || { echo "Missing mav0: $MAV"; exit 1; }

source "${WS}/scripts/setup_ws.bash"
CFG="$(ros2 pkg prefix pht_vio_ros)/share/pht_vio_ros/config/euroc/euroc_stereo_imu_config.yaml"
OUT="${WS}/results/${SEQ}_offline_s${START//./p}"
mkdir -p "$OUT"

RUN_CFG="${OUT}/config.yaml"
cp "$CFG" "$RUN_CFG"
cp "$(ros2 pkg prefix pht_vio_ros)/share/pht_vio_ros/config/euroc/"cam*.yaml "$OUT/"
sed -i "s|output_path: \"~/output/\"|output_path: \"${OUT}/\"|" "$RUN_CFG"

echo "[offline] seq=$SEQ start=${START}s mav0=$MAV"
ros2 run pht_vio pht_vio_offline "$RUN_CFG" "$MAV" "$START"

python3 "${WS}/scripts/evaluate_trajectory.py" \
  "${OUT}/vio.csv" "$GT" "${OUT}/eval" \
  --no-plot --run-name "$(basename "$OUT")"

echo "[done] $(basename "$OUT") -> ${OUT}/eval/metrics.json"
