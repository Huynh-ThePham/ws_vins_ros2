#!/usr/bin/env bash
# Full Semantic–GeoDF ablation rerun with frozen fair protocol (N=3).
#
# Usage:
#   ./scripts/run_sem_geodf_full_rerun.sh
#
# Env:
#   N=3                  trials per cell (default 3)
#   FAIR_BAG_RATE=1      default on
#   SAD_BAG_RATE=1.0     default on
#   SKIP_EUROC=0         set 1 for VIODE-only
#   FORCE=0              set 1 to overwrite existing runs
set -eo pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"
TS="$(date +%Y%m%d_%H%M%S)"
LOG="${WS}/logs/sem_geodf_full_rerun_${TS}.log"
mkdir -p "${WS}/logs"

export N="${N:-3}"
export FORCE="${FORCE:-0}"
export FAIR_BAG_RATE="${FAIR_BAG_RATE:-1}"
export SAD_BAG_RATE="${SAD_BAG_RATE:-1.0}"
export SEM_POLICY_VIODE_LEVEL_OVERRIDE="${SEM_POLICY_VIODE_LEVEL_OVERRIDE:-0}"
export ORACLE_ABLATION="${ORACLE_ABLATION:-0}"
export PROTOCOL_TAG="${PROTOCOL_TAG:-fair${SAD_BAG_RATE//./p}}"
export YOLO_DEVICE="${YOLO_DEVICE:-cuda}"

exec > >(tee -a "$LOG") 2>&1
echo "[full-rerun] start $(date -Is) log=$LOG"
echo "[full-rerun] N=$N FORCE=$FORCE FAIR_BAG_RATE=$FAIR_BAG_RATE SAD_BAG_RATE=$SAD_BAG_RATE PROTOCOL_TAG=$PROTOCOL_TAG"

cd "$WS"
bash "${WS}/scripts/setup_viode_gt_cache.sh" || true

METHODS="baseline adaptive sad_sem sequential sem_geodf sem_geodf_mask_gated"
VIODE_LEVELS="0_none 1_low 2_mid 3_high"
COMMON=(N="$N" FORCE="$FORCE" FAIR_BAG_RATE="$FAIR_BAG_RATE" SAD_BAG_RATE="$SAD_BAG_RATE"
        METHODS="$METHODS" PROTOCOL_TAG="$PROTOCOL_TAG")

if [ "${SKIP_EUROC:-0}" != "1" ]; then
  echo "[full-rerun] === EuRoC 5×MH ==="
  env "${COMMON[@]}" VIODE_LEVELS=__none__ EUROC_SEQS="MH_01_easy MH_02_easy MH_03_medium MH_04_difficult MH_05_difficult" \
    bash "${WS}/scripts/run_sem_geodf_ablation.sh" full || true
fi

for env in city_day city_night parking_lot; do
  echo "[full-rerun] === VIODE env=$env ==="
  env "${COMMON[@]}" VIODE_ENV="$env" VIODE_LEVELS="$VIODE_LEVELS" EUROC_SEQS=__none__ SKIP_EUROC=1 \
    bash "${WS}/scripts/run_sem_geodf_ablation.sh" full || true
done

echo "[full-rerun] done $(date -Is)"
echo "[full-rerun] results: ${WS}/results/sem_geodf_ablation/${PROTOCOL_TAG}/ABLATION_SUMMARY.md"
