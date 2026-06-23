#!/usr/bin/env bash
# Compare GeoDF EuRoC results between ws_vins (ROS1) and ws_vins_ros2 (ROS2).
#
# Usage:
#   ./scripts/compare_geodf_repos.sh [SEQUENCE] [METHOD]
#
# Runs geodf on both repos (if needed) and writes a side-by-side summary.
set -eo pipefail

SEQ="${1:-MH_01_easy}"
METHOD="${2:-geodf_hard}"

WS_ROS2="$(cd "$(dirname "$0")/.." && pwd)"
WS_ROS1="${WS_VINS:-/home/theph/ws_vins}"
export EUROC_ROOT="${EUROC_ROOT:-/media/theph/Data1/ws_research_datasets/raw_datasets/euroc}"

case "$SEQ" in
  MH_01_easy) START=40 ;;
  MH_02_easy) START=35 ;;
  MH_03_medium) START=17.5 ;;
  MH_04_difficult|MH_05_difficult) START=15 ;;
  *) START=0 ;;
esac
START_TAG="$START"
if [[ "$START_TAG" == *.* ]]; then START_TAG="${START_TAG//./p}"; fi

OUT_ROS2="${WS_ROS2}/results/geodf/${SEQ}_${METHOD}_s${START_TAG}"
OUT_ROS1="${WS_ROS1}/results/geodf/${SEQ}_${METHOD}_s${START_TAG}"

echo "=== ROS2: run ${METHOD} on ${SEQ} ==="
if [ ! -f "${OUT_ROS2}/eval/metrics.json" ]; then
  bash "${WS_ROS2}/scripts/run_geodf_euroc.sh" "$SEQ" "$METHOD" "$START"
else
  echo "[skip] existing ${OUT_ROS2}/eval/metrics.json"
fi

echo "=== ROS1: run ${METHOD} on ${SEQ} ==="
if [ ! -f "${OUT_ROS1}/eval/metrics.json" ]; then
  if [ -x "${WS_ROS1}/scripts/run_geodf_euroc.sh" ]; then
    EUROC_ROOT="$EUROC_ROOT" bash "${WS_ROS1}/scripts/run_geodf_euroc.sh" "$SEQ" "$METHOD" 1.0 "$START" --eval
  else
    echo "Missing ${WS_ROS1}/scripts/run_geodf_euroc.sh" >&2
    exit 1
  fi
else
  echo "[skip] existing ${OUT_ROS1}/eval/metrics.json"
fi

python3 "${WS_ROS2}/scripts/compare_geodf_repos.py" \
  --ros1-root "${WS_ROS1}/results/geodf" \
  --ros2-root "${WS_ROS2}/results/geodf" \
  --seq "$SEQ" --method "$METHOD" --start "$START" \
  --out "${WS_ROS2}/results/geodf/compare_${SEQ}_${METHOD}.md"

cat "${WS_ROS2}/results/geodf/compare_${SEQ}_${METHOD}.md"
