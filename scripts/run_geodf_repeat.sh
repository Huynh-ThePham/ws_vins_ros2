#!/usr/bin/env bash
# Repeatability study: run each (level, method) N times into separate trial dirs
# so trajectory accuracy can be reported as mean +/- std (VINS-Fusion is non-deterministic).
#
# Usage: ./scripts/run_geodf_repeat.sh [LEVELS] [METHODS] [N]
# Env: VIODE_ENV=city_day, FORCE=1 to redo existing trials.
set -eo pipefail

LEVELS="${1:-2_mid 3_high}"
METHODS="${2:-baseline adaptive adaptive_v2}"
N="${3:-5}"

WS="$(cd "$(dirname "$0")/.." && pwd)"
VIODE_ENV="${VIODE_ENV:-city_day}"
# shellcheck source=scripts/lib/geodf_common.sh
source "${WS}/scripts/lib/geodf_common.sh"

VIODE="$(resolve_viode_root)" || { echo "VIODE not found"; exit 1; }
source_ros2_ws "$WS"
OUT_ROOT="${WS}/results/viode_repeat"
mkdir -p "$OUT_ROOT" "${WS}/logs"
VIODE_CFG="$(ros2 pkg prefix pht_vio_ros)/share/pht_vio_ros/config/viode"

for level in $LEVELS; do
    bag_ros1="${VIODE}/${VIODE_ENV}/${level}.bag"
    bag_ros2="$(resolve_viode_ros2_bag "$bag_ros1" "$WS")"
    [ -f "$bag_ros1" ] || { echo "[skip] missing $bag_ros1"; continue; }
    if [ ! -d "$bag_ros2" ] || [ ! -f "$bag_ros2/metadata.yaml" ]; then
        bash "${WS}/scripts/viode_prepare_ros2_bag.sh" "$bag_ros1" "$bag_ros2"
    fi
    gt="${OUT_ROOT}/${VIODE_ENV}_${level}_gt.csv"
    [ -f "$gt" ] || python3 "${WS}/scripts/viode_dump_gt.py" --bag "$bag_ros1" --out "$gt"

    for method in $METHODS; do
        mode="$(geodf_method_to_mode "$method")"
        RUN_CFG="${VIODE_CFG}/viode_${mode}_config_repeat_${level}.yaml"
        for i in $(seq 1 "$N"); do
            out="${OUT_ROOT}/${VIODE_ENV}_${level}_${method}/trial_${i}"
            if [ "${FORCE:-0}" != "1" ] && [ -f "${out}/eval/metrics.json" ]; then
                echo "[have] ${VIODE_ENV}_${level}_${method} trial ${i}"
                continue
            fi
            mkdir -p "$out"
            cp "${VIODE_CFG}/viode_${mode}_config.yaml" "$RUN_CFG"
            sed -i "s|output_path: \"~/output/\"|output_path: \"${out}/\"|" "$RUN_CFG"
            sed -i "s|pose_graph_save_path: \"~/output/pose_graph/\"|pose_graph_save_path: \"${out}/pose_graph/\"|" "$RUN_CFG"
            echo "=== ${VIODE_ENV}_${level}_${method} trial ${i}/${N} (mode=$mode) ==="
            killall -9 pht_vio_node 2>/dev/null || true
            sleep 1
            run_pht_vio_benchmark "$RUN_CFG" "$out" "$bag_ros2" 0 1.0 || {
                echo "[warn] benchmark failed ${VIODE_ENV}_${level}_${method} trial ${i}"
                rm -rf "${out}/eval"
                continue
            }
            python3 "${WS}/scripts/evaluate_trajectory.py" \
                "${out}/vio.csv" "$gt" "${out}/eval" --no-plot \
                --run-name "${VIODE_ENV}_${level}_${method}_t${i}" || echo "[warn] eval failed"
            if [ "$method" != "baseline" ]; then
                python3 "${WS}/scripts/geodf_filter_metrics.py" --run-dir "$out" 2>/dev/null || true
            fi
        done
    done
done

python3 "${WS}/scripts/summarize_geodf_repeat.py" --root "$OUT_ROOT" \
    --env "$VIODE_ENV" --levels "$LEVELS" --methods "$METHODS"
echo "[repeat] done -> $OUT_ROOT"
