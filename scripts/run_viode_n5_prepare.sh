#!/usr/bin/env bash
# Prepare N-repeat study on full VIODE.
#
# Does NOT run VIO trials — only prerequisites:
#   - optional rebuild (REBUILD=1)
#   - ROS1→ROS2 bag conversion (12 bags)
#   - ground-truth CSV per (env, level)
#   - manifest under results/viode_repeat/
#
# Usage:
#   ./scripts/run_viode_n5_prepare.sh [N]
# Env: VIODE_ROOT, REBUILD=1, SKIP_CONVERT=1, SKIP_GT=1
#      METHODS="baseline alwayson adaptive_fixed adaptive_no_quality adaptive_no_vote adaptive"
set -eo pipefail

N="${1:-5}"
WS="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=scripts/lib/geodf_common.sh
source "${WS}/scripts/lib/geodf_common.sh"

VIODE_ENVS=(city_day city_night parking_lot)
ALL_LEVELS=(0_none 1_low 2_mid 3_high)
read -r -a METHODS <<< "${METHODS:-baseline alwayson adaptive_fixed adaptive_no_quality adaptive_no_vote adaptive}"
OUT_ROOT="${WS}/results/viode_repeat"
LOG="${WS}/logs/viode_n5_prepare_$(date +%Y%m%d_%H%M%S).log"
mkdir -p "$OUT_ROOT" "${WS}/logs" "${WS}/data/viode_ros2"

exec > >(tee -a "$LOG") 2>&1
echo "[viode-n5-prepare] log: $LOG"
echo "[viode-n5-prepare] N=$N  trials_per_cell=$((N))  total_runs=$(( ${#VIODE_ENVS[@]} * ${#ALL_LEVELS[@]} * ${#METHODS[@]} * N ))"
echo "[viode-n5-prepare] METHODS=${METHODS[*]}"

if [ "${REBUILD:-0}" = "1" ]; then
    echo "[viode-n5-prepare] rebuilding pht_vio_ros (Release)..."
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
echo "[viode-n5-prepare] build: $(ros2 pkg prefix pht_vio_ros)"

VIODE="$(resolve_viode_root)" || {
    echo "ERROR: VIODE dataset not found. Set VIODE_ROOT=/path/to/viode" >&2
    exit 1
}
echo "[viode-n5-prepare] VIODE_ROOT=$VIODE"

ready_bags=0
ready_gt=0
missing_ros1=0

for env in "${VIODE_ENVS[@]}"; do
    export VIODE_ENV="$env"
    for level in "${ALL_LEVELS[@]}"; do
        bag_ros1="${VIODE}/${env}/${level}.bag"
        bag_ros2="$(resolve_viode_ros2_bag "$bag_ros1" "$WS")"
        gt="${OUT_ROOT}/${env}_${level}_gt.csv"

        if [ ! -f "$bag_ros1" ]; then
            echo "[missing] ROS1 bag: $bag_ros1"
            missing_ros1=$((missing_ros1 + 1))
            continue
        fi

        if [ "${SKIP_GT:-0}" != "1" ]; then
            if [ ! -f "$gt" ]; then
                echo "[gt] $env/$level -> $gt"
                python3 "${WS}/scripts/viode_dump_gt.py" --bag "$bag_ros1" --out "$gt"
            else
                echo "[have] GT $env/$level"
            fi
        fi
        [ -f "$gt" ] && ready_gt=$((ready_gt + 1))

        if [ "${SKIP_CONVERT:-0}" != "1" ]; then
            if [ ! -d "$bag_ros2" ] || [ ! -f "$bag_ros2/metadata.yaml" ]; then
                echo "[convert] $env/$level ($(du -h "$bag_ros1" | cut -f1))"
                bash "${WS}/scripts/viode_prepare_ros2_bag.sh" "$bag_ros1" "$bag_ros2"
            else
                echo "[have] ROS2 $env/$level"
            fi
        fi
        if [ -d "$bag_ros2" ] && [ -f "$bag_ros2/metadata.yaml" ]; then
            ready_bags=$((ready_bags + 1))
        fi
    done
done

python3 "${WS}/scripts/viode_n5_manifest.py" write \
    --root "$OUT_ROOT" --n "$N" --viode-root "$VIODE" \
    --log "$LOG"

echo ""
echo "=== VIODE N=$N preparation summary ==="
echo "  ROS1 bags missing : $missing_ros1"
echo "  ROS2 bags ready   : $ready_bags / 12"
echo "  GT CSV ready      : $ready_gt / 12"
echo "  Output root       : $OUT_ROOT"
echo "  Planned trials    : $(( ${#VIODE_ENVS[@]} * ${#ALL_LEVELS[@]} * ${#METHODS[@]} * N ))  (env×level×method×N)"
echo ""
if [ "$missing_ros1" -eq 0 ] && [ "$ready_bags" -eq 12 ] && [ "$ready_gt" -eq 12 ]; then
    echo "READY. Launch benchmark:"
    echo "  cd $WS && bash scripts/run_viode_n5.sh $N"
else
    echo "NOT READY — fix missing items above, then re-run this script."
    exit 1
fi
