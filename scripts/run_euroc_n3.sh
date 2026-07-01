#!/usr/bin/env bash
# EuRoC-only N-repeat benchmark (baseline + adaptive on MH_01..MH_05).
#
# Prerequisites: ./scripts/run_euroc_n3_prepare.sh [N]
#
# Usage: ./scripts/run_euroc_n3.sh [N]
# Env: EUROC_ROOT, FORCE=1 (redo existing trials), SKIP_SUMMARY=1
set -eo pipefail

N="${1:-3}"
WS="$(cd "$(dirname "$0")/.." && pwd)"
export FORCE="${FORCE:-0}"
export EUROC_ROOT="${EUROC_ROOT:-/media/theph/Data1/Research/Datasets/EuRoC}"

# shellcheck source=scripts/lib/geodf_common.sh
source "${WS}/scripts/lib/geodf_common.sh"

EUROC_SEQS=(MH_01_easy MH_02_easy MH_03_medium MH_04_difficult MH_05_difficult)
STAMP="$(date +%Y%m%d_%H%M%S)"
LOG="${WS}/logs/euroc_n3_${STAMP}.log"
OUT="${WS}/results/euroc_repeat"
mkdir -p "${WS}/logs" "$OUT" "${WS}/results/geodf_evaluation"

exec > >(tee -a "$LOG") 2>&1
echo "[euroc-n3] === start N=$N stamp=$STAMP ==="
echo "[euroc-n3] log: $LOG"
echo "[euroc-n3] FORCE=$FORCE  total_runs=$(( 5 * 2 * N ))"

python3 "${WS}/scripts/capture_provenance.py" \
    --study-dir "${WS}/results/euroc_repeat" --study "euroc_n3" \
    --configs "src/config/euroc/*.yaml" || echo "[warn] provenance capture failed"

manifest="${OUT}/manifest.json"
if [ ! -f "$manifest" ]; then
    echo "[euroc-n3] no manifest — running prepare first..."
    bash "${WS}/scripts/run_euroc_n3_prepare.sh" "$N"
fi

source_ros2_ws "$WS"
EUROC="$(resolve_euroc_root)"
EUROC_CFG="$(ros2 pkg prefix pht_vio_ros)/share/pht_vio_ros/config/euroc"

for seq in "${EUROC_SEQS[@]}"; do
    group="$(euroc_group_for_seq "$seq")"
    start="$(euroc_bag_start_s "$seq")"
    bag="$(resolve_euroc_ros2_bag "$seq" "$WS")"
    gt="${EUROC}/${group}/${seq}/${seq}/mav0/state_groundtruth_estimate0/data.csv"
    if [ ! -d "$bag" ] || [ ! -f "$gt" ]; then
        echo "[skip] missing data for $seq"
        continue
    fi

    for method in baseline adaptive; do
        mode="$(geodf_method_to_mode "$method")"
        run_cfg="${EUROC_CFG}/euroc_${mode}_config_n3_${seq}.yaml"
        for i in $(seq 1 "$N"); do
            trial="${OUT}/${seq}_${method}/trial_${i}"
            if [ "$FORCE" != "1" ] && [ -f "${trial}/eval/metrics.json" ]; then
                echo "[have] ${seq} ${method} trial ${i}"
                continue
            fi
            mkdir -p "$trial"
            cp "${EUROC_CFG}/euroc_${mode}_config.yaml" "$run_cfg"
            sed -i "s|output_path: \"~/output/\"|output_path: \"${trial}/\"|" "$run_cfg"
            sed -i "s|pose_graph_save_path: \"~/output/pose_graph/\"|pose_graph_save_path: \"${trial}/pose_graph/\"|" "$run_cfg"
            echo "=== EuRoC ${seq} ${method} trial ${i}/${N} (start=${start}s) ==="
            killall -9 pht_vio_node 2>/dev/null || true
            sleep 1
            run_pht_vio_benchmark "$run_cfg" "$trial" "$bag" "$start" 1.0
            python3 "${WS}/scripts/evaluate_trajectory.py" \
                "${trial}/vio.csv" "$gt" "${trial}/eval" --no-plot \
                --run-name "${seq}_${method}_t${i}" || echo "[warn] eval failed ${seq} ${method} t${i}"
            if [ "$method" != "baseline" ]; then
                python3 "${WS}/scripts/geodf_filter_metrics.py" --run-dir "$trial" 2>/dev/null || true
            fi
        done
    done
done

if [ "${SKIP_SUMMARY:-0}" != "1" ]; then
    echo "[euroc-n3] === summarizing ==="
    python3 "${WS}/scripts/summarize_euroc_repeat.py" \
        --root "$OUT" --n "$N" \
        --out "${WS}/results/geodf_evaluation/EUROC_REPEAT_N3.md"
    python3 "${WS}/scripts/euroc_n3_manifest.py" update --root "$OUT" --n "$N"
fi

echo "[euroc-n3] === done ==="
echo "[euroc-n3] results: $OUT"
echo "[euroc-n3] summary: ${WS}/results/geodf_evaluation/EUROC_REPEAT_N3.md"
