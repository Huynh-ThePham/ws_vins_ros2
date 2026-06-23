#!/usr/bin/env bash
# SAD-VINS on VIODE real-dynamic dataset (ROS 2 native, stereo + IMU).
#
# Usage: ./scripts/run_sad_viode.sh [LEVELS] [METHODS]
#   LEVELS  : "0_none 1_low 2_mid 3_high" (default)
#   METHODS : "baseline sad_sem" (default)
#
# Env: VIODE_ROOT, VIODE_ENV=city_day, YOLO_DEVICE=cuda|cpu|auto, FORCE=1
set -eo pipefail

LEVELS="${1:-0_none 1_low 2_mid 3_high}"
METHODS="${2:-baseline sad_sem}"

WS="$(cd "$(dirname "$0")/.." && pwd)"
VIODE_ENV="${VIODE_ENV:-city_day}"
# shellcheck source=scripts/lib/sad_common.sh
source "${WS}/scripts/lib/sad_common.sh"

VIODE="$(resolve_viode_root)" || {
    echo "VIODE not found. Set VIODE_ROOT=/path/to/viode" >&2
    exit 1
}

source_ros2_ws "$WS"
OUT_ROOT="${WS}/results/sad_viode"
mkdir -p "$OUT_ROOT" "${WS}/logs"

for level in $LEVELS; do
    bag_ros1="${VIODE}/${VIODE_ENV}/${level}.bag"
    bag_ros2="$(resolve_viode_ros2_bag "$bag_ros1" "$WS")"
    if [ ! -f "$bag_ros1" ]; then
        echo "[skip] missing $bag_ros1"
        continue
    fi
    if [ ! -d "$bag_ros2" ] || [ ! -f "$bag_ros2/metadata.yaml" ]; then
        bash "${WS}/scripts/viode_prepare_ros2_bag.sh" "$bag_ros1" "$bag_ros2"
    fi

    for method in $METHODS; do
        mode="$(sad_method_to_mode "$method")"
        run="${VIODE_ENV}_${level}_${method}"
        out="${OUT_ROOT}/${run}"
        if [ "${FORCE:-0}" != "1" ] && [ -f "${out}/eval/metrics.json" ]; then
            echo "[have] $run (FORCE=1 to redo)"
            continue
        fi

        VIODE_CFG="$(ros2 pkg prefix pht_vio_ros)/share/pht_vio_ros/config/viode"
        RUN_CFG="${VIODE_CFG}/viode_${mode}_config_run_${level}.yaml"
        mkdir -p "$out"
        cp "${VIODE_CFG}/viode_${mode}_config.yaml" "$RUN_CFG"
        sed -i "s|output_path: \"~/output/\"|output_path: \"${out}/\"|" "$RUN_CFG"
        sed -i "s|pose_graph_save_path: \"~/output/pose_graph/\"|pose_graph_save_path: \"${out}/pose_graph/\"|" "$RUN_CFG"

        USE_YOLO=0
        [ "$method" != "baseline" ] && USE_YOLO=1

        echo "=== VIODE $run (mode=$mode yolo=$USE_YOLO) ==="
        killall -9 pht_vio_node mask_node 2>/dev/null || true
        sleep 1
        run_sad_vio_benchmark "$RUN_CFG" "$out" "$bag_ros2" 0 1.0 "$USE_YOLO"

        python3 "${WS}/scripts/viode_dump_gt.py" --bag "$bag_ros1" --out "${out}/gt_odometry.csv"
        python3 "${WS}/scripts/evaluate_trajectory.py" \
            "${out}/vio.csv" "${out}/gt_odometry.csv" "${out}/eval" \
            --no-plot --run-name "$run"
        python3 "${WS}/scripts/sem_filter_metrics.py" --run-dir "$out" --json "${out}/sem_filter_metrics.json" 2>/dev/null || true
    done
done

python3 "${WS}/scripts/summarize_sad_viode.py" --root "$OUT_ROOT" --env "$VIODE_ENV" --levels "$LEVELS" || true
echo "[viode] done -> $OUT_ROOT"
