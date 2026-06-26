#!/usr/bin/env bash
# Final paper numbers at N=5 (consistent single build), reusing any existing trials.
#
# Rationale: VINS-Fusion ATE is run-to-run variable on HIGH-DYNAMIC scenes (baseline
# std up to ~13%), so N=3 under-sampled the comparison. N=5 + mean±std gives the true
# picture and exposes the filter's variance-reduction (determinism) property.
#
# VIODE: baseline + adaptive(PROPOSED, hard+vote) on all 12 conditions.
# EuRoC: baseline + adaptive (static safety).
#
# Usage: ./scripts/run_geodf_n5_final.sh [N]   (default 5; EuRoC uses min(N,3))
set -eo pipefail

N="${1:-5}"
EUROC_N=$(( N < 3 ? N : 3 ))
WS="$(cd "$(dirname "$0")/.." && pwd)"
export EUROC_ROOT="${EUROC_ROOT:-/media/theph/Data1/Research/Datasets/EuRoC}"
export VIODE_ROOT="${VIODE_ROOT:-/media/theph/Data1/Research/Datasets/Viode}"
export FORCE="${FORCE:-0}"   # 0 = reuse existing trials (fill up to N)

# shellcheck source=scripts/lib/geodf_common.sh
source "${WS}/scripts/lib/geodf_common.sh"
source_ros2_ws "$WS"

VIODE_ENVS=(city_day city_night parking_lot)
ALL_LEVELS="0_none 1_low 2_mid 3_high"

echo "[n5] === VIODE: baseline+adaptive (all levels) N=$N ==="
for env in "${VIODE_ENVS[@]}"; do
    VIODE_ENV="$env" bash "${WS}/scripts/run_geodf_repeat.sh" "$ALL_LEVELS" "baseline adaptive" "$N"
done

echo "[n5] === EuRoC: baseline+adaptive (static safety) N=$EUROC_N ==="
EUROC_SEQS=(MH_01_easy MH_02_easy MH_03_medium MH_04_difficult MH_05_difficult)
EUROC="$(resolve_euroc_root)"
EUROC_CFG="$(ros2 pkg prefix pht_vio_ros)/share/pht_vio_ros/config/euroc"
OUT="${WS}/results/euroc_repeat"; mkdir -p "$OUT"
bash "${WS}/scripts/euroc_prepare.sh" "${EUROC_SEQS[@]}" || true
for seq in "${EUROC_SEQS[@]}"; do
    GROUP="$(euroc_group_for_seq "$seq")"
    START="$(euroc_bag_start_s "$seq")"
    BAG="$(resolve_euroc_ros2_bag "$seq" "$WS")"
    GT="${EUROC}/${GROUP}/${seq}/${seq}/mav0/state_groundtruth_estimate0/data.csv"
    [ -d "$BAG" ] && [ -f "$GT" ] || { echo "[skip] EuRoC missing $seq"; continue; }
    for method in baseline adaptive; do
        mode="$(geodf_method_to_mode "$method")"
        RUN_CFG="${EUROC_CFG}/euroc_${mode}_config_n5_${seq}.yaml"
        for i in $(seq 1 "$EUROC_N"); do
            trial="${OUT}/${seq}_${method}/trial_${i}"
            if [ "$FORCE" != "1" ] && [ -f "${trial}/eval/metrics.json" ]; then
                echo "[have] euroc ${seq} ${method} t${i}"; continue
            fi
            mkdir -p "$trial"
            cp "${EUROC_CFG}/euroc_${mode}_config.yaml" "$RUN_CFG"
            sed -i "s|output_path: \"~/output/\"|output_path: \"${trial}/\"|" "$RUN_CFG"
            sed -i "s|pose_graph_save_path: \"~/output/pose_graph/\"|pose_graph_save_path: \"${trial}/pose_graph/\"|" "$RUN_CFG"
            echo "=== EuRoC ${seq} ${method} trial ${i}/${EUROC_N} ==="
            killall -9 pht_vio_node 2>/dev/null || true
            sleep 1
            run_pht_vio_benchmark "$RUN_CFG" "$trial" "$BAG" "$START" 1.0
            python3 "${WS}/scripts/evaluate_trajectory.py" \
                "${trial}/vio.csv" "$GT" "${trial}/eval" --no-plot \
                --run-name "${seq}_${method}_t${i}" || echo "[warn] eval failed ${seq} ${method} t${i}"
        done
    done
done

echo "[n5] === done ==="
