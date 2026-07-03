#!/usr/bin/env bash
# Prepare N-repeat study on EuRoC Machine Hall.
#
# Does NOT run VIO trials — only prerequisites:
#   - optional rebuild (REBUILD=1)
#   - GT extract + ROS1→ROS2 bag conversion (euroc_prepare.sh)
#   - manifest under results/euroc_repeat/
#
# Usage:
#   ./scripts/run_euroc_n3_prepare.sh [N]
# Env: EUROC_ROOT, REBUILD=1, SKIP_PREPARE=1
#      METHODS="baseline alwayson adaptive_fixed adaptive_no_quality adaptive_no_vote adaptive"
set -eo pipefail

N="${1:-3}"
WS="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=scripts/lib/geodf_common.sh
source "${WS}/scripts/lib/geodf_common.sh"

EUROC_SEQS=(MH_01_easy MH_02_easy MH_03_medium MH_04_difficult MH_05_difficult)
read -r -a METHODS <<< "${METHODS:-baseline alwayson adaptive_fixed adaptive_no_quality adaptive_no_vote adaptive}"
OUT_ROOT="${WS}/results/euroc_repeat"
LOG="${WS}/logs/euroc_n3_prepare_$(date +%Y%m%d_%H%M%S).log"
mkdir -p "$OUT_ROOT" "${WS}/logs" "${WS}/data/euroc_ros2"

exec > >(tee -a "$LOG") 2>&1
echo "[euroc-n3-prepare] log: $LOG"
echo "[euroc-n3-prepare] N=$N  total_runs=$(( ${#EUROC_SEQS[@]} * ${#METHODS[@]} * N ))"
echo "[euroc-n3-prepare] METHODS=${METHODS[*]}"

if [ "${REBUILD:-0}" = "1" ]; then
    echo "[euroc-n3-prepare] rebuilding pht_vio_ros (Release)..."
    set +u
    source /opt/ros/humble/setup.bash
    set -u
    cd "$WS"
    colcon build --packages-up-to pht_vio_ros --cmake-args -DCMAKE_BUILD_TYPE=Release
fi

source_ros2_ws "$WS"
if ! ros2 pkg prefix pht_vio_ros >/dev/null 2>&1; then
    echo "ERROR: pht_vio_ros not installed. Run: REBUILD=1 $0 $N" >&2
    exit 1
fi
echo "[euroc-n3-prepare] build: $(ros2 pkg prefix pht_vio_ros)"

EUROC="$(resolve_euroc_root)" || {
    echo "ERROR: EuRoC dataset not found. Set EUROC_ROOT=/path/to/EuRoC" >&2
    exit 1
}
echo "[euroc-n3-prepare] EUROC_ROOT=$EUROC"

if [ "${SKIP_PREPARE:-0}" != "1" ]; then
    bash "${WS}/scripts/euroc_prepare.sh" "${EUROC_SEQS[@]}"
fi

ready_gt=0
ready_bags=0
missing=0

for seq in "${EUROC_SEQS[@]}"; do
    group="$(euroc_group_for_seq "$seq")"
    gt="${EUROC}/${group}/${seq}/${seq}/mav0/state_groundtruth_estimate0/data.csv"
    bag_ros2="$(resolve_euroc_ros2_bag "$seq" "$WS")"
    if [ -f "$gt" ]; then
        ready_gt=$((ready_gt + 1))
    else
        echo "[missing] GT $seq"
        missing=$((missing + 1))
    fi
    if [ -d "$bag_ros2" ] && [ -f "${bag_ros2}/metadata.yaml" ]; then
        ready_bags=$((ready_bags + 1))
    else
        echo "[missing] ROS2 bag $seq"
        missing=$((missing + 1))
    fi
done

python3 "${WS}/scripts/euroc_n3_manifest.py" write \
    --root "$OUT_ROOT" --n "$N" --euroc-root "$EUROC" \
    --log "$LOG"

echo ""
echo "=== EuRoC N=$N preparation summary ==="
echo "  Sequences         : ${#EUROC_SEQS[@]}"
echo "  GT ready          : $ready_gt / ${#EUROC_SEQS[@]}"
echo "  ROS2 bags ready   : $ready_bags / ${#EUROC_SEQS[@]}"
echo "  Output root       : $OUT_ROOT"
echo "  Planned trials    : $(( ${#EUROC_SEQS[@]} * ${#METHODS[@]} * N ))  (seq×method×N)"
echo ""
if [ "$ready_gt" -eq "${#EUROC_SEQS[@]}" ] && [ "$ready_bags" -eq "${#EUROC_SEQS[@]}" ]; then
    echo "READY. Launch benchmark:"
    echo "  cd $WS && bash scripts/run_euroc_n3.sh $N"
else
    echo "NOT READY — fix missing items above, then re-run this script."
    exit 1
fi
