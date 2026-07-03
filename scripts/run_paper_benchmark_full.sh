#!/usr/bin/env bash
# Full AECE paper benchmark: VIODE N=5 + EuRoC N=3 + detection + postprocess.
#
# Prerequisites: run_viode_n5_prepare.sh + run_euroc_n3_prepare.sh (same METHODS).
#
# Usage: ./scripts/run_paper_benchmark_full.sh
# Env: METHODS, VIODE_ROOT, EUROC_ROOT, FORCE=1 (redo all trials)
set -eo pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"
export METHODS="${METHODS:-baseline alwayson adaptive_fixed adaptive_no_quality adaptive_no_vote adaptive}"
export VIODE_ROOT="${VIODE_ROOT:-/media/theph/Data1/Research/Datasets/Viode}"
export EUROC_ROOT="${EUROC_ROOT:-/media/theph/Data1/Research/Datasets/EuRoC}"
export FORCE="${FORCE:-0}"

STAMP="$(date +%Y%m%d_%H%M%S)"
LOG="${WS}/logs/paper_benchmark_full_${STAMP}.log"
mkdir -p "${WS}/logs"

exec > >(tee -a "$LOG") 2>&1
echo "[paper-bench] === start stamp=$STAMP ==="
echo "[paper-bench] log: $LOG"
echo "[paper-bench] METHODS=$METHODS  FORCE=$FORCE"
echo "[paper-bench] VIODE: 360 trials (3 env × 4 level × 6 method × N=5)"
echo "[paper-bench] EuRoC:  90 trials  (5 seq × 6 method × N=3)"

python3 "${WS}/scripts/capture_provenance.py" \
    --study-dir "${WS}/results/geodf_evaluation" --study "paper_full" \
    --configs "src/config/viode/*.yaml" "src/config/euroc/*.yaml" || true

echo ""
echo "[paper-bench] === PHASE 1/4: VIODE N=5 ==="
bash "${WS}/scripts/run_viode_n5.sh" 5

echo ""
echo "[paper-bench] === PHASE 2/4: EuRoC N=3 ==="
bash "${WS}/scripts/run_euroc_n3.sh" 3

echo ""
echo "[paper-bench] === PHASE 3/4: detection dumps (Table 4) ==="
bash "${WS}/scripts/run_viode_detection_prepare.sh"

echo ""
echo "[paper-bench] === PHASE 4/4: postprocess artifacts ==="
bash "${WS}/scripts/postprocess_paper_artifacts.sh"

echo ""
echo "[paper-bench] === COMPLETE ==="
echo "[paper-bench] artifacts: ${WS}/results/geodf_evaluation/"
ls -1 "${WS}/results/geodf_evaluation/"*.md 2>/dev/null | sed 's|^|  |'
