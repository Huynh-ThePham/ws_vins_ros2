#!/usr/bin/env bash
# VIODE-only N-repeat benchmark (baseline + adaptive on all 12 conditions).
#
# Prerequisites: ./scripts/run_viode_n5_prepare.sh [N]
#
# Usage: ./scripts/run_viode_n5.sh [N]
# Env: VIODE_ROOT, FORCE=1 (redo existing trials), SKIP_SUMMARY=1
set -eo pipefail

N="${1:-5}"
WS="$(cd "$(dirname "$0")/.." && pwd)"
export FORCE="${FORCE:-0}"
export VIODE_ROOT="${VIODE_ROOT:-/media/theph/Data1/Research/Datasets/Viode}"

# shellcheck source=scripts/lib/geodf_common.sh
source "${WS}/scripts/lib/geodf_common.sh"

VIODE_ENVS=(city_day city_night parking_lot)
ALL_LEVELS="0_none 1_low 2_mid 3_high"
STAMP="$(date +%Y%m%d_%H%M%S)"
LOG="${WS}/logs/viode_n5_${STAMP}.log"
mkdir -p "${WS}/logs" "${WS}/results/viode_repeat" "${WS}/results/geodf_evaluation"

exec > >(tee -a "$LOG") 2>&1
echo "[viode-n5] === start N=$N stamp=$STAMP ==="
echo "[viode-n5] log: $LOG"
echo "[viode-n5] FORCE=$FORCE  total_runs=$(( 3 * 4 * 2 * N ))"

python3 "${WS}/scripts/capture_provenance.py" \
    --study-dir "${WS}/results/viode_repeat" --study "viode_n5" \
    --configs "src/config/viode/*.yaml" || echo "[warn] provenance capture failed"

# Quick readiness check
manifest="${WS}/results/viode_repeat/manifest.json"
if [ ! -f "$manifest" ]; then
    echo "[viode-n5] no manifest — running prepare first..."
    bash "${WS}/scripts/run_viode_n5_prepare.sh" "$N"
fi

for env in "${VIODE_ENVS[@]}"; do
    echo "[viode-n5] === env=$env ==="
    VIODE_ENV="$env" bash "${WS}/scripts/run_geodf_repeat.sh" "$ALL_LEVELS" "baseline adaptive" "$N"
done

if [ "${SKIP_SUMMARY:-0}" != "1" ]; then
    echo "[viode-n5] === summarizing ==="
    python3 "${WS}/scripts/summarize_n5_final.py" \
        --viode-only \
        --out "${WS}/results/geodf_evaluation/PAPER_RESULTS_N5.md"
    python3 "${WS}/scripts/viode_n5_manifest.py" update --root "${WS}/results/viode_repeat" --n "$N"
fi

echo "[viode-n5] === done ==="
echo "[viode-n5] results: ${WS}/results/viode_repeat"
echo "[viode-n5] summary: ${WS}/results/geodf_evaluation/PAPER_RESULTS_N5.md"
