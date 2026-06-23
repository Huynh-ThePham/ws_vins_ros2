#!/usr/bin/env bash
# Full SAD-VINS evaluation: EuRoC (5 seq) + VIODE (4 levels), baseline vs sad_sem.
# Runs SEQUENTIALLY — requires GPU for YOLO (set YOLO_DEVICE=cpu to fallback).
set -eo pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"
export EUROC_ROOT="${EUROC_ROOT:-/media/theph/Data1/ws_research_datasets/raw_datasets/euroc}"
export VIODE_ROOT="${VIODE_ROOT:-/media/theph/Data1/ws_research_datasets/viode}"
export YOLO_DEVICE="${YOLO_DEVICE:-cuda}"
export FORCE="${FORCE:-0}"
export SAD_BAG_RATE="${SAD_BAG_RATE:-0.5}"

source "${WS}/scripts/setup_ws.bash"
# shellcheck source=scripts/lib/sad_common.sh
source "${WS}/scripts/lib/sad_common.sh"

OUT="${WS}/results/sad_evaluation"
mkdir -p "$OUT" "${WS}/logs"

EUROC_SEQS=(MH_01_easy MH_02_easy MH_03_medium MH_04_difficult MH_05_difficult)
EUROC_METHODS=(baseline sad_sem)
VIODE_LEVELS=(0_none 1_low 2_mid 3_high)

echo "========== PHASE 1: EuRoC (5 seq × baseline + sad_sem) =========="
for seq in "${EUROC_SEQS[@]}"; do
    for method in "${EUROC_METHODS[@]}"; do
        echo "[euroc] $seq / $method"
        killall -9 pht_vio_node mask_node 2>/dev/null || true
        sleep 2
        bash "${WS}/scripts/run_sad_euroc.sh" "$seq" "$method" "" --eval || {
            echo "[WARN] EuRoC failed: $seq $method" | tee -a "${OUT}/failures.log"
        }
    done
done

echo "========== PHASE 2: VIODE (4 levels × baseline + sad_sem) =========="
killall -9 pht_vio_node mask_node 2>/dev/null || true
sleep 2
bash "${WS}/scripts/run_sad_viode.sh" "${VIODE_LEVELS[*]}" "baseline sad_sem" || {
    echo "[WARN] VIODE batch failed" | tee -a "${OUT}/failures.log"
}

echo "========== PHASE 3: Summaries =========="
python3 "${WS}/scripts/summarize_sad_viode.py" \
    --root "${WS}/results/sad_viode" --env city_day \
    --levels "${VIODE_LEVELS[*]}" \
    --out "${OUT}/viode_summary.md"

python3 "${WS}/scripts/summarize_sad_evaluation.py" \
    --ws "$WS" \
    --out "${OUT}/EVALUATION_REPORT.md" \
    --json "${OUT}/evaluation_summary.json"

echo "[done] Report -> ${OUT}/EVALUATION_REPORT.md"
