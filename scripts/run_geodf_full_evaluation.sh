#!/usr/bin/env bash
# Full GeoDF-Adaptive evaluation: EuRoC (5 seq) + VIODE (4 levels), all metrics.
# Runs SEQUENTIALLY — do not parallelize with other VIO nodes.
set -eo pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"
export EUROC_ROOT="${EUROC_ROOT:-/media/theph/Data1/ws_research_datasets/raw_datasets/euroc}"
export VIODE_ROOT="${VIODE_ROOT:-/media/theph/Data1/ws_research_datasets/viode}"
export FORCE="${FORCE:-0}"

source "${WS}/scripts/setup_ws.bash"
# shellcheck source=scripts/lib/geodf_common.sh
source "${WS}/scripts/lib/geodf_common.sh"

OUT="${WS}/results/geodf_evaluation"
mkdir -p "$OUT" "${WS}/logs"

EUROC_SEQS=(MH_01_easy MH_02_easy MH_03_medium MH_04_difficult MH_05_difficult)
EUROC_METHODS=(baseline alwayson adaptive)
VIODE_LEVELS=(0_none 1_low 2_mid 3_high)
VIODE_METHODS=(baseline geodf_dump adaptive)

run_euroc() {
    killall -9 pht_vio_node 2>/dev/null || true
    sleep 2
    bash "${WS}/scripts/run_geodf_euroc.sh" "$@" || {
        echo "[WARN] EuRoC run failed: $*" | tee -a "${OUT}/failures.log"
        return 0
    }
}

run_viode() {
    killall -9 pht_vio_node 2>/dev/null || true
    sleep 2
    bash "${WS}/scripts/run_geodf_viode.sh" "$@" || {
        echo "[WARN] VIODE run failed: $*" | tee -a "${OUT}/failures.log"
        return 0
    }
}

echo "========== PHASE 1: EuRoC static ablation (5 seq × 3 methods) =========="
for seq in "${EUROC_SEQS[@]}"; do
    tag="$(start_tag "$(euroc_bag_start_s "$seq")")"
    for method in "${EUROC_METHODS[@]}"; do
        echo "[euroc] $seq / $method"
        run_euroc "$seq" "$method" "" --eval
        out_dir="${WS}/results/geodf/${seq}_${method}_s${tag}"
        [ -d "$out_dir" ] && python3 "${WS}/scripts/geodf_filter_metrics.py" --run-dir "$out_dir" 2>/dev/null || true
    done
done

echo "========== PHASE 2: VIODE dynamic (4 levels × 3 methods) =========="
for level in "${VIODE_LEVELS[@]}"; do
    for method in "${VIODE_METHODS[@]}"; do
        echo "[viode] $level / $method"
        run_viode "$level" "$method"
        python3 "${WS}/scripts/geodf_filter_metrics.py" --run-dir \
            "${WS}/results/viode/city_day_${level}_${method}" 2>/dev/null || true
    done
done

echo "========== PHASE 3: Summaries =========="
python3 "${WS}/scripts/summarize_euroc_static_ablation.py" \
    --root "${WS}/results/geodf" \
    --seqs "${EUROC_SEQS[*]}"
python3 "${WS}/scripts/summarize_viode_adaptive.py" \
    --root "${WS}/results/viode" --env city_day \
    --levels "${VIODE_LEVELS[*]}"
python3 "${WS}/scripts/summarize_geodf_filter_impact.py" \
    --static-root "${WS}/results/geodf" \
    --viode-root "${WS}/results/viode" \
    --out "${OUT}/filter_impact_summary.md" 2>/dev/null || \
python3 "${WS}/scripts/summarize_geodf_filter_impact.py" \
    --viode-root "${WS}/results/viode" \
    --out "${OUT}/filter_impact_summary.md" 2>/dev/null || true

python3 "${WS}/scripts/summarize_geodf_evaluation.py" \
    --ws "$WS" --out "${OUT}/EVALUATION_REPORT.md" \
    --json "${OUT}/evaluation_summary.json"

echo "[done] Full evaluation -> ${OUT}/EVALUATION_REPORT.md"
