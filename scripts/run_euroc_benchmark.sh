#!/usr/bin/env bash
# Run VINS-Fusion ROS2 on EuRoC ros2_bag and evaluate with evo (same protocol as ws_vins).
# Usage: ./scripts/run_euroc_benchmark.sh [SEQUENCE] [MODE] [START_S] [LOOP]
#   LOOP: 0 (default, VIO only) | 1 (with loop closure)
set -eo pipefail

SEQ="${1:-MH_01_easy}"
MODE="${2:-stereo_imu}"
START="${3:-}"
LOOP="${4:-0}"

EUROC_ROOT="${EUROC_ROOT:-/media/theph/Data1/ws_research_datasets/raw_datasets/euroc}"
WS="$(cd "$(dirname "$0")/.." && pwd)"

case "$SEQ" in
  MH_*) GROUP=machine_hall ;;
  V1_*) GROUP=vicon_room1 ;;
  V2_*) GROUP=vicon_room2 ;;
  *) echo "Unknown sequence: $SEQ"; exit 1 ;;
esac

if [ -z "$START" ]; then
  case "$SEQ" in
    MH_01_easy) START=40 ;;
    MH_02_easy) START=35 ;;
    MH_03_medium) START=17.5 ;;
    MH_04_difficult|MH_05_difficult) START=15 ;;
    *) START=0 ;;
  esac
fi

BAG="${EUROC_ROOT}/${GROUP}/${SEQ}/ros2_bag"
GT="${EUROC_ROOT}/${GROUP}/${SEQ}/${SEQ}/mav0/state_groundtruth_estimate0/data.csv"
[ -d "$BAG" ] || { echo "Missing bag: $BAG"; exit 1; }
[ -f "$GT" ] || { echo "Missing GT: $GT"; exit 1; }

START_TAG="$START"
if [[ "$START_TAG" == *.* ]]; then START_TAG="${START_TAG//./p}"; fi
OUT="${WS}/results/${SEQ}_${MODE}"
[ "$LOOP" = "1" ] && OUT="${OUT}_loop"
OUT="${OUT}_s${START_TAG}"

EUROC_CFG="$(ros2 pkg prefix pht_vio_ros)/share/pht_vio_ros/config/euroc"
RUN_CFG="${EUROC_CFG}/euroc_${MODE}_config_run_${SEQ}.yaml"
if [ "$LOOP" = "1" ]; then
  RUN_CFG="${EUROC_CFG}/euroc_${MODE}_config_run_${SEQ}_loop.yaml"
fi

source /opt/ros/humble/setup.bash
source "${WS}/install/setup.bash"

killall -9 pht_vio_node pht_loop_closure_node 2>/dev/null || true
sleep 1

mkdir -p "$OUT"
cp "${EUROC_CFG}/euroc_${MODE}_config.yaml" "$RUN_CFG"
sed -i "s|output_path: \"~/output/\"|output_path: \"${OUT}/\"|" "$RUN_CFG"
sed -i "s|pose_graph_save_path: \"~/output/pose_graph/\"|pose_graph_save_path: \"${OUT}/pose_graph/\"|" "$RUN_CFG"

rm -f "${OUT}/vio.csv" "${OUT}/vio_loop.csv" "${OUT}/pht_vio_node.log" "${OUT}/pht_loop_closure_node.log"

echo "[run] seq=$SEQ mode=$MODE loop=$LOOP start=${START}s out=$OUT"

ros2 run pht_vio_ros pht_vio_node "$RUN_CFG" --ros-args -p use_sim_time:=true \
  > "${OUT}/pht_vio_node.log" 2>&1 &
VINS_PID=$!

LOOP_PID=""
if [ "$LOOP" = "1" ]; then
  ros2 run pht_loop_closure_ros pht_loop_closure_node "$RUN_CFG" --ros-args -p use_sim_time:=true \
    > "${OUT}/pht_loop_closure_node.log" 2>&1 &
  LOOP_PID=$!
fi

if [ "$LOOP" = "1" ]; then
  # BRIEF vocabulary load (~58MB) needs extra time before bag play.
  sleep 12
else
  sleep 5
fi
if ! kill -0 "$VINS_PID" 2>/dev/null; then
  echo "pht_vio_node failed to start:" >&2
  tail -30 "${OUT}/pht_vio_node.log" >&2
  exit 1
fi
if [ -n "$LOOP_PID" ] && ! kill -0 "$LOOP_PID" 2>/dev/null; then
  echo "pht_loop_closure_node failed to start:" >&2
  tail -30 "${OUT}/pht_loop_closure_node.log" >&2
  exit 1
fi

ros2 bag play "$BAG" --clock --rate 1.0 --start-offset "$START" --disable-keyboard-controls
sleep 12
kill "$VINS_PID" 2>/dev/null || true
[ -n "$LOOP_PID" ] && kill "$LOOP_PID" 2>/dev/null || true
wait "$VINS_PID" 2>/dev/null || true
[ -n "$LOOP_PID" ] && wait "$LOOP_PID" 2>/dev/null || true

if [ "$LOOP" = "1" ]; then
  TRAJ="${OUT}/vio_loop.csv"
else
  TRAJ="${OUT}/vio.csv"
fi

if [ ! -s "$TRAJ" ]; then
  echo "ERROR: empty trajectory $TRAJ" >&2
  tail -20 "${OUT}/pht_vio_node.log" >&2
  [ -f "${OUT}/pht_loop_closure_node.log" ] && tail -20 "${OUT}/pht_loop_closure_node.log" >&2
  exit 1
fi

python3 "${WS}/scripts/evaluate_trajectory.py" \
  "$TRAJ" "$GT" "${OUT}/eval" \
  --no-plot --run-name "$(basename "$OUT")"

echo "[done] $(basename "$OUT") -> ${OUT}/eval/metrics.json ($(wc -l < "$TRAJ") poses)"
