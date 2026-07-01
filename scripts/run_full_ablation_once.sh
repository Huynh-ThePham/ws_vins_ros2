#!/usr/bin/env bash
# One-shot full ablation: EuRoC (if available) + VIODE 3 envs × 4 levels × 6 methods.
# Logs everything for post-hoc analysis (weak methods, gate stats, failures).
#
# Usage:
#   ./scripts/run_full_ablation_once.sh
#
# Env:
#   N=1                  trials (use run_sem_geodf_full_rerun.sh for N=3)
#   FAIR_BAG_RATE=1 SAD_BAG_RATE=1.0   published default: same bag rate for all
#   PROTOCOL_TAG=fair1p0  versioned result subdir under results/sem_geodf_ablation/
#   FORCE=1               re-run existing
#   SKIP_EUROC=1          VIODE only
set -eo pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"
TS="$(date +%Y%m%d_%H%M%S)"
LOG_DIR="${WS}/logs/full_ablation_${TS}"
mkdir -p "$LOG_DIR" "${WS}/data/viode_stub/city_day" \
         "${WS}/data/viode_stub/city_night" "${WS}/data/viode_stub/parking_lot"

export VIODE_ROOT="${WS}/data/viode_stub"
export N="${N:-1}"
export FORCE="${FORCE:-1}"
export FAIR_BAG_RATE="${FAIR_BAG_RATE:-1}"
export SAD_BAG_RATE="${SAD_BAG_RATE:-1.0}"
export SEM_POLICY_VIODE_LEVEL_OVERRIDE="${SEM_POLICY_VIODE_LEVEL_OVERRIDE:-0}"
export ORACLE_ABLATION="${ORACLE_ABLATION:-0}"
export PROTOCOL_TAG="${PROTOCOL_TAG:-fair${SAD_BAG_RATE//./p}}"
export YOLO_DEVICE="${YOLO_DEVICE:-cuda}"

MASTER_LOG="${LOG_DIR}/master.log"
exec > >(tee -a "$MASTER_LOG") 2>&1

echo "=============================================="
echo "[full-once] start $(date -Is)"
echo "[full-once] log_dir=$LOG_DIR"
echo "[full-once] N=$N FORCE=$FORCE FAIR_BAG_RATE=$FAIR_BAG_RATE SAD_BAG_RATE=$SAD_BAG_RATE PROTOCOL_TAG=$PROTOCOL_TAG"
echo "=============================================="

cd "$WS"
bash "${WS}/scripts/setup_viode_gt_cache.sh" | tee "${LOG_DIR}/gt_cache.log"

METHODS="baseline adaptive sad_sem sequential sem_geodf sem_geodf_mask_gated"
VIODE_LEVELS="0_none 1_low 2_mid 3_high"
COMMON=(N=1 FORCE="$FORCE" FAIR_BAG_RATE="$FAIR_BAG_RATE" SAD_BAG_RATE="$SAD_BAG_RATE" METHODS="$METHODS")

if [ "${SKIP_EUROC:-0}" != "1" ]; then
    echo "[full-once] === EuRoC 5×MH ==="
    env "${COMMON[@]}" VIODE_LEVELS=__none__ \
        bash "${WS}/scripts/run_sem_geodf_ablation.sh" full \
        2>&1 | tee "${LOG_DIR}/euroc.log" || true
else
    echo "[full-once] SKIP_EUROC=1"
fi

for env in city_day city_night parking_lot; do
    echo "[full-once] === VIODE env=$env ==="
    env "${COMMON[@]}" VIODE_ENV="$env" VIODE_LEVELS="$VIODE_LEVELS" EUROC_SEQS=__none__ SKIP_EUROC=1 \
        bash "${WS}/scripts/run_sem_geodf_ablation.sh" full \
        2>&1 | tee "${LOG_DIR}/viode_${env}.log" || true
done

echo "[full-once] === final summary ==="
ABLATION_ROOT="${WS}/results/sem_geodf_ablation/${PROTOCOL_TAG}"
python3 "${WS}/scripts/summarize_sem_geodf_ablation.py" \
    --root "$ABLATION_ROOT" \
    --out "${ABLATION_ROOT}/ABLATION_SUMMARY.md"

python3 "${WS}/scripts/export_ablation_analysis.py" \
    --root "$ABLATION_ROOT" \
    --out-csv "${ABLATION_ROOT}/ABLATION_ANALYSIS.csv" \
    --out-md "${ABLATION_ROOT}/ABLATION_ANALYSIS.md"

cp -a "${ABLATION_ROOT}/ABLATION_SUMMARY.md" "${LOG_DIR}/"
cp -a "${ABLATION_ROOT}/ABLATION_ANALYSIS.csv" "${LOG_DIR}/" 2>/dev/null || true
cp -a "${ABLATION_ROOT}/ABLATION_ANALYSIS.md" "${LOG_DIR}/" 2>/dev/null || true

echo "[full-once] done $(date -Is)"
echo "[full-once] master log: $MASTER_LOG"
echo "[full-once] analysis: ${ABLATION_ROOT}/ABLATION_ANALYSIS.csv"
