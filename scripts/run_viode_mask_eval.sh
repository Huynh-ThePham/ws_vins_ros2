#!/usr/bin/env bash
# Feature-level mask eval for adaptive_2d vs adaptive (stereo 3D) on all 12 VIODE cells.
#
# One geodf_dump run per (env, level, method) with geodf_dump_features: 1.
# Compares rejected features against VIODE vehicle segmentation masks.
#
# Prerequisites: run_viode_n5_prepare.sh (ROS2 bags + GT)
# Usage: ./scripts/run_viode_mask_eval.sh
# Env: VIODE_ROOT, FORCE=1
set -eo pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"
export VIODE_ROOT="${VIODE_ROOT:-/media/theph/Data1/Research/Datasets/Viode}"
export FORCE="${FORCE:-0}"
OUT="${WS}/results/viode_detection"
CFG="${WS}/src/config/viode"
VIODE_ENVS=(city_day city_night parking_lot)
LEVELS="0_none 1_low 2_mid 3_high"
METHODS="${METHODS:-adaptive_2d adaptive}"

# shellcheck source=scripts/lib/geodf_common.sh
source "${WS}/scripts/lib/geodf_common.sh"
VIODE="$(resolve_viode_root)" || { echo "ERROR: VIODE not found (set VIODE_ROOT)" >&2; exit 1; }
source_ros2_ws "$WS"
VIODE_CFG="$(ros2 pkg prefix pht_vio_ros)/share/pht_vio_ros/config/viode"
mkdir -p "$OUT" "${WS}/results/geodf_evaluation"

for env in "${VIODE_ENVS[@]}"; do
    for level in $LEVELS; do
        bag_ros1="${VIODE}/${env}/${level}.bag"
        bag_ros2="$(resolve_viode_ros2_bag "$bag_ros1" "$WS")"
        [ -f "$bag_ros1" ] || { echo "[skip] $bag_ros1"; continue; }

        for method in $METHODS; do
            run="${env}_${level}_${method}_dump"
            out="${OUT}/${run}"
            if [ "$FORCE" != "1" ] && [ -f "${out}/detection_eval.json" ]; then
                echo "[have] mask eval $run"
                continue
            fi

            mode="$(geodf_method_to_mode "$method")"
            run_cfg="${VIODE_CFG}/viode_${mode}_config_mask_${env}_${level}.yaml"
            mkdir -p "$out"
            cp "${VIODE_CFG}/viode_${mode}_config.yaml" "$run_cfg"
            sed -i "s|output_path: \"~/output/\"|output_path: \"${out}/\"|" "$run_cfg"
            sed -i "s|pose_graph_save_path: \"~/output/pose_graph/\"|pose_graph_save_path: \"${out}/pose_graph/\"|" "$run_cfg"
            sed -i 's|geodf_dump_features: 0|geodf_dump_features: 1|' "$run_cfg"

            echo "=== mask eval dump $run (method=$method) ==="
            killall -9 pht_vio_node 2>/dev/null || true
            sleep 1
            run_pht_vio_benchmark "$run_cfg" "$out" "$bag_ros2" 0 1.0

            if [ ! -f "${out}/geo_df_features.csv" ]; then
                echo "[warn] no features ${out}/geo_df_features.csv"
                continue
            fi

            mask_dir="${out}/masks"
            ids="${CFG}/vehicle_ids_${env}.txt"
            [ -f "$ids" ] || ids="${CFG}/vehicle_ids_city_day.txt"
            if [ ! -d "$mask_dir" ] || [ "$FORCE" = "1" ]; then
                python3 "${WS}/scripts/viode_make_masks.py" \
                    --bag "$bag_ros1" --out-dir "$mask_dir" \
                    --rgb-ids "${CFG}/rgb_ids.txt" \
                    --vehicle-ids "$ids"
            fi
            python3 "${WS}/scripts/eval_viode_detection.py" \
                --features "${out}/geo_df_features.csv" \
                --mask-dir "$mask_dir" \
                --out "${out}/detection_eval.json"
        done
    done
done

python3 "${WS}/scripts/summarize_mask_eval.py" \
    --root "$OUT" \
    --out "${WS}/results/geodf_evaluation/MASK_EVAL_2D_vs_3D.md"

echo "[mask-eval] done -> ${WS}/results/geodf_evaluation/MASK_EVAL_2D_vs_3D.md"
