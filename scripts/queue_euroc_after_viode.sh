#!/usr/bin/env bash
# Wait for VIODE/full ablation batch, then run EuRoC 5×MH × 6 methods (N=1).
#
# Usage:
#   WAIT_PID=12345 ./scripts/queue_euroc_after_viode.sh
#   ./scripts/queue_euroc_after_viode.sh          # wait for any run_full_ablation_once.sh
set -eo pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"
TS="$(date +%Y%m%d_%H%M%S)"
LOG="${WS}/logs/euroc_queued_${TS}.log"
exec > >(tee -a "$LOG") 2>&1

wait_for_batch() {
    local pid="${WAIT_PID:-}"
    if [ -n "$pid" ]; then
        echo "[queue-euroc] waiting for PID $pid ..."
        while kill -0 "$pid" 2>/dev/null; do
            sleep 30
        done
        echo "[queue-euroc] PID $pid finished"
        return 0
    fi
    echo "[queue-euroc] waiting for run_full_ablation_once.sh / run_sem_geodf_ablation.sh ..."
    while pgrep -f "run_full_ablation_once.sh|run_sem_geodf_ablation.sh" >/dev/null 2>&1; do
        sleep 30
    done
    echo "[queue-euroc] no ablation processes running"
}

echo "=============================================="
echo "[queue-euroc] start $(date -Is)"
echo "[queue-euroc] log=$LOG"
echo "=============================================="

wait_for_batch

export EUROC_ROOT="${EUROC_ROOT:-/media/theph/Data1/Research/Datasets/EuRoC}"
if [ ! -d "${EUROC_ROOT}/machine_hall" ]; then
    echo "[queue-euroc] ERROR: EuRoC not mounted at $EUROC_ROOT" >&2
    exit 1
fi

export N=1
export FORCE="${FORCE:-1}"
export FAIR_BAG_RATE="${FAIR_BAG_RATE:-1}"
export SAD_BAG_RATE="${SAD_BAG_RATE:-1.0}"
export YOLO_DEVICE="${YOLO_DEVICE:-cuda}"
export VIODE_LEVELS=__none__
export SKIP_EUROC=0
METHODS="${METHODS:-baseline adaptive sad_sem sequential sem_geodf sem_geodf_mask_gated}"
export METHODS

echo "[queue-euroc] EuRoC root OK: $EUROC_ROOT"
echo "[queue-euroc] === EuRoC 5×MH rerun ==="

cd "$WS"
env N="$N" FORCE="$FORCE" FAIR_BAG_RATE="$FAIR_BAG_RATE" SAD_BAG_RATE="$SAD_BAG_RATE" \
    EUROC_ROOT="$EUROC_ROOT" METHODS="$METHODS" VIODE_LEVELS=__none__ \
    bash "${WS}/scripts/run_sem_geodf_ablation.sh" full

echo "[queue-euroc] === refresh summary ==="
python3 "${WS}/scripts/summarize_sem_geodf_ablation.py" \
    --root "${WS}/results/sem_geodf_ablation" \
    --out "${WS}/results/sem_geodf_ablation/ABLATION_SUMMARY.md"

python3 "${WS}/scripts/export_ablation_analysis.py" \
    --root "${WS}/results/sem_geodf_ablation" \
    --out-csv "${WS}/results/sem_geodf_ablation/ABLATION_ANALYSIS.csv" \
    --out-md "${WS}/results/sem_geodf_ablation/ABLATION_ANALYSIS.md"

echo "[queue-euroc] done $(date -Is)"
echo "[queue-euroc] log: $LOG"
