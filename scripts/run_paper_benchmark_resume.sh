#!/usr/bin/env bash
# Resume incomplete paper benchmark (EuRoC N=3 + detection + postprocess).
# VIODE N=5 is assumed complete — skipped unless RERUN_VIODE=1.
#
# Usage: ./scripts/run_paper_benchmark_resume.sh
# Env: METHODS, EUROC_ROOT, FORCE=1 (redo all EuRoC trials)
set -eo pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"
export METHODS="${METHODS:-baseline alwayson adaptive_fixed adaptive_no_quality adaptive_no_vote adaptive}"
export EUROC_ROOT="${EUROC_ROOT:-/home/theph/ws_vins_ros2/data/euroc_benchmark}"
export VIODE_ROOT="${VIODE_ROOT:-/media/theph/Data1/Research/Datasets/Viode}"
export FORCE="${FORCE:-0}"

STAMP="$(date +%Y%m%d_%H%M%S)"
LOG="${WS}/logs/paper_benchmark_resume_${STAMP}.log"
mkdir -p "${WS}/logs"

exec > >(tee -a "$LOG") 2>&1
echo "[resume] === start stamp=$STAMP ==="
echo "[resume] log: $LOG"
echo "[resume] METHODS=$METHODS  FORCE=$FORCE"

if [ "${RERUN_VIODE:-0}" = "1" ]; then
    echo "[resume] === VIODE N=5 (full rerun) ==="
    bash "${WS}/scripts/run_viode_n5.sh" 5
else
    viode_done=$(find "${WS}/results/viode_repeat" -name metrics.json 2>/dev/null | wc -l)
    echo "[resume] skip VIODE ($viode_done trials present)"
fi

echo "[resume] === EuRoC N=3 ==="
bash "${WS}/scripts/run_euroc_n3.sh" 3

echo "[resume] === detection dumps ==="
if resolve_viode_root >/dev/null 2>&1; then
    bash "${WS}/scripts/run_viode_detection_prepare.sh"
else
    echo "[resume][WARN] VIODE dataset not mounted — skip detection (Table 4). Mount /media/theph/Data1 and re-run."
fi

echo "[resume] === postprocess ==="
bash "${WS}/scripts/postprocess_paper_artifacts.sh"

echo "[resume] === COMPLETE ==="
bash "${WS}/scripts/monitor_paper_benchmark.sh"
