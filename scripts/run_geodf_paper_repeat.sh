#!/usr/bin/env bash
# Paper evaluation: every sequence × method × N trials (mean±std for trajectory).
#
# Datasets: EuRoC MH_01–05 + VIODE (city_day, city_night, parking_lot) × 4 levels.
# Methods (Hướng A): baseline | always-on | adaptive (PROPOSED) | adaptive_fixed.
#
# Usage: ./scripts/run_geodf_paper_repeat.sh [N]
# Env: N=3 (default), FORCE=1, EUROC_ROOT, VIODE_ROOT
set -eo pipefail

N="${1:-3}"
WS="$(cd "$(dirname "$0")/.." && pwd)"
export EUROC_ROOT="${EUROC_ROOT:-/media/theph/Data1/Research/Datasets/EuRoC}"
export VIODE_ROOT="${VIODE_ROOT:-/media/theph/Data1/Research/Datasets/Viode}"
export FORCE="${FORCE:-1}"

EUROC_SEQS=(MH_01_easy MH_02_easy MH_03_medium MH_04_difficult MH_05_difficult)
EUROC_METHODS=(baseline alwayson adaptive adaptive_fixed)
VIODE_ENVS=(city_day city_night parking_lot)
VIODE_LEVELS="0_none 1_low 2_mid 3_high"
VIODE_METHODS=(baseline geodf_dump adaptive adaptive_fixed)

OUT="${WS}/results/paper_repeat"
LOG="${WS}/logs/paper_repeat_$(date +%Y%m%d_%H%M%S).log"
mkdir -p "$OUT" "${WS}/logs" "${WS}/results/geodf_evaluation"

# shellcheck source=scripts/lib/geodf_common.sh
source "${WS}/scripts/lib/geodf_common.sh"
source_ros2_ws "$WS"

exec > >(tee -a "$LOG") 2>&1
echo "[paper] === start N=$N ==="
echo "[paper] OUT=$OUT LOG=$LOG"

echo "[paper] === PHASE 0: EuRoC prepare ==="
bash "${WS}/scripts/euroc_prepare.sh" "${EUROC_SEQS[@]}"

echo "[paper] === PHASE 1: EuRoC × ${#EUROC_METHODS[@]} methods × N=$N ==="
EUROC="$(resolve_euroc_root)"
EUROC_CFG="$(ros2 pkg prefix pht_vio_ros)/share/pht_vio_ros/config/euroc"

for seq in "${EUROC_SEQS[@]}"; do
    GROUP="$(euroc_group_for_seq "$seq")"
    START="$(euroc_bag_start_s "$seq")"
    BAG="$(resolve_euroc_ros2_bag "$seq" "$WS")"
    GT="${EUROC}/${GROUP}/${seq}/${seq}/mav0/state_groundtruth_estimate0/data.csv"
    [ -d "$BAG" ] && [ -f "$GT" ] || { echo "[skip] EuRoC missing $seq"; continue; }

    for method in "${EUROC_METHODS[@]}"; do
        mode="$(geodf_method_to_mode "$method")"
        RUN_CFG="${EUROC_CFG}/euroc_${mode}_config_paper_${seq}.yaml"
        for i in $(seq 1 "$N"); do
            trial="${OUT}/euroc/${seq}_${method}/trial_${i}"
            if [ "$FORCE" != "1" ] && [ -f "${trial}/eval/metrics.json" ]; then
                echo "[have] euroc ${seq} ${method} t${i}"
                continue
            fi
            mkdir -p "$trial"
            cp "${EUROC_CFG}/euroc_${mode}_config.yaml" "$RUN_CFG"
            sed -i "s|output_path: \"~/output/\"|output_path: \"${trial}/\"|" "$RUN_CFG"
            sed -i "s|pose_graph_save_path: \"~/output/pose_graph/\"|pose_graph_save_path: \"${trial}/pose_graph/\"|" "$RUN_CFG"
            echo "=== EuRoC ${seq} ${method} trial ${i}/${N} ==="
            killall -9 pht_vio_node 2>/dev/null || true
            sleep 1
            run_pht_vio_benchmark "$RUN_CFG" "$trial" "$BAG" "$START" 1.0
            python3 "${WS}/scripts/evaluate_trajectory.py" \
                "${trial}/vio.csv" "$GT" "${trial}/eval" --no-plot \
                --run-name "${seq}_${method}_t${i}" || echo "[warn] eval failed ${seq} ${method} t${i}"
            python3 "${WS}/scripts/geodf_filter_metrics.py" \
                --run-dir "$trial" --json "${trial}/geodf_filter_metrics.json" 2>/dev/null || true
        done
    done
done

echo "[paper] === PHASE 2: VIODE × 3 env × 4 levels × ${#VIODE_METHODS[@]} methods × N=$N ==="
VIODE="$(resolve_viode_root)"
VIODE_CFG="$(ros2 pkg prefix pht_vio_ros)/share/pht_vio_ros/config/viode"

for env in "${VIODE_ENVS[@]}"; do
    for level in $VIODE_LEVELS; do
        bag_ros1="${VIODE}/${env}/${level}.bag"
        bag_ros2="$(resolve_viode_ros2_bag "$bag_ros1" "$WS")"
        [ -f "$bag_ros1" ] || { echo "[skip] missing $bag_ros1"; continue; }
        if [ ! -d "$bag_ros2" ] || [ ! -f "$bag_ros2/metadata.yaml" ]; then
            bash "${WS}/scripts/viode_prepare_ros2_bag.sh" "$bag_ros1" "$bag_ros2"
        fi
        gt="${OUT}/viode/${env}_${level}_gt.csv"
        [ -f "$gt" ] || python3 "${WS}/scripts/viode_dump_gt.py" --bag "$bag_ros1" --out "$gt"

        for method in "${VIODE_METHODS[@]}"; do
            mode="$(geodf_method_to_mode "$method")"
            RUN_CFG="${VIODE_CFG}/viode_${mode}_config_paper_${level}.yaml"
            for i in $(seq 1 "$N"); do
                trial="${OUT}/viode/${env}_${level}_${method}/trial_${i}"
                if [ "$FORCE" != "1" ] && [ -f "${trial}/eval/metrics.json" ]; then
                    echo "[have] viode ${env} ${level} ${method} t${i}"
                    continue
                fi
                mkdir -p "$trial"
                cp "${VIODE_CFG}/viode_${mode}_config.yaml" "$RUN_CFG"
                sed -i "s|output_path: \"~/output/\"|output_path: \"${trial}/\"|" "$RUN_CFG"
                sed -i "s|pose_graph_save_path: \"~/output/pose_graph/\"|pose_graph_save_path: \"${trial}/pose_graph/\"|" "$RUN_CFG"
                echo "=== VIODE ${env}_${level}_${method} trial ${i}/${N} ==="
                killall -9 pht_vio_node 2>/dev/null || true
                sleep 1
                run_pht_vio_benchmark "$RUN_CFG" "$trial" "$bag_ros2" 0 1.0
                python3 "${WS}/scripts/evaluate_trajectory.py" \
                    "${trial}/vio.csv" "$gt" "${trial}/eval" --no-plot \
                    --run-name "${env}_${level}_${method}_t${i}" || echo "[warn] eval failed"
                python3 "${WS}/scripts/geodf_filter_metrics.py" \
                    --run-dir "$trial" --json "${trial}/geodf_filter_metrics.json" 2>/dev/null || true
            done
        done
    done
done

echo "[paper] === PHASE 3: Detection eval (always-on dump, 1 run/env/level) ==="
for env in "${VIODE_ENVS[@]}"; do
    export VIODE_ENV="$env"
    for level in $VIODE_LEVELS; do
        dump="${OUT}/viode/${env}_${level}_geodf_dump/trial_1"
        if [ ! -f "${dump}/geo_df_features.csv" ]; then
            echo "[paper] detection dump run ${env} ${level}"
            mode="$(geodf_method_to_mode geodf_dump)"
            RUN_CFG="${VIODE_CFG}/viode_${mode}_config_det_${level}.yaml"
            bag_ros1="${VIODE}/${env}/${level}.bag"
            bag_ros2="$(resolve_viode_ros2_bag "$bag_ros1" "$WS")"
            mkdir -p "$dump"
            cp "${VIODE_CFG}/viode_${mode}_config.yaml" "$RUN_CFG"
            sed -i "s|output_path: \"~/output/\"|output_path: \"${dump}/\"|" "$RUN_CFG"
            sed -i "s|pose_graph_save_path: \"~/output/pose_graph/\"|pose_graph_save_path: \"${dump}/pose_graph/\"|" "$RUN_CFG"
            sed -i 's/geodf_adaptive: 1/geodf_adaptive: 0/' "$RUN_CFG" 2>/dev/null || true
            killall -9 pht_vio_node 2>/dev/null || true
            sleep 1
            run_pht_vio_benchmark "$RUN_CFG" "$dump" "$bag_ros2" 0 1.0
        fi
        masks="${dump}/masks"
        bag_ros1="${VIODE}/${env}/${level}.bag"
        feats="${dump}/geo_df_features.csv"
        if [ -f "$feats" ] && [ -f "$bag_ros1" ]; then
            [ -d "$masks" ] || python3 "${WS}/scripts/viode_make_masks.py" \
                --bag "$bag_ros1" --out-dir "$masks" \
                --rgb-ids "${WS}/src/config/viode/rgb_ids.txt" \
                --vehicle-ids "${WS}/src/config/viode/vehicle_ids_city_day.txt" 2>/dev/null || true
            python3 "${WS}/scripts/eval_viode_detection.py" \
                --features "$feats" --mask-dir "$masks" \
                --out "${dump}/detection_eval.json" 2>/dev/null || true
        fi
    done
done

echo "[paper] === PHASE 4: Summarize ==="
python3 "${WS}/scripts/summarize_paper_repeat.py" --root "$OUT" --trials "$N"

echo "[paper] === done ==="
