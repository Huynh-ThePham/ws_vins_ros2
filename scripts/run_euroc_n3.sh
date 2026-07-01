#!/usr/bin/env bash
# EuRoC-only N-repeat benchmark (baseline + weighted on MH_01..MH_05).
#
# Prerequisites: ./scripts/run_euroc_n3_prepare.sh [N]
#
# Usage: ./scripts/run_euroc_n3.sh [N]
# Env: EUROC_ROOT, FORCE=1 (redo existing trials), SKIP_SUMMARY=1
set -eo pipefail

N="${1:-3}"
WS="$(cd "$(dirname "$0")/.." && pwd)"
export FORCE="${FORCE:-0}"
export METHODS="${METHODS:-baseline weighted}"
export EUROC_ROOT="${EUROC_ROOT:-/media/theph/Data1/Research/Datasets/EuRoC}"

STAMP="$(date +%Y%m%d_%H%M%S)"
LOG="${WS}/logs/euroc_n3_${STAMP}.log"
OUT="${WS}/results/euroc_repeat"
mkdir -p "${WS}/logs" "$OUT" "${WS}/results/geodf_evaluation"

exec > >(tee -a "$LOG") 2>&1
echo "[euroc-n3] === start N=$N stamp=$STAMP ==="
echo "[euroc-n3] log: $LOG"
echo "[euroc-n3] FORCE=$FORCE  METHODS=$METHODS  total_runs=$(( 5 * 2 * N ))"

python3 "${WS}/scripts/capture_provenance.py" \
    --study-dir "${WS}/results/euroc_repeat" --study "euroc_n3_weighted" \
    --configs "src/config/euroc/*.yaml" || echo "[warn] provenance capture failed"

manifest="${OUT}/manifest.json"
if [ ! -f "$manifest" ]; then
    echo "[euroc-n3] no manifest — running prepare first..."
    bash "${WS}/scripts/run_euroc_n3_prepare.sh" "$N"
fi

bash "${WS}/scripts/run_geodf_euroc_weighted.sh" "$N"

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
