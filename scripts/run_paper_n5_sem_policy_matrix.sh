#!/usr/bin/env bash
# Paper-grade N=5 Semantic-GeoDF matrix.
#
# Runs:
#   - VIODE city_day, city_night, parking_lot; levels 0_none..3_high
#   - EuRoC MH_01..MH_05
#   - methods: baseline adaptive sad_sem sequential sem_geodf sem_geodf_mask_gated
#
# The Semantic-GeoDF method receives the train-selected semantic policy params.
# FORCE is intentionally default 0 so the script can resume after interruption.
set -eo pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"
cd "$WS"

TAG="${TAG:-paper_n5_sem_policy}"
N="${N:-5}"
METHODS="${METHODS:-baseline adaptive sad_sem sequential sem_geodf sem_geodf_mask_gated}"
VIODE_LEVELS="${VIODE_LEVELS:-0_none 1_low 2_mid 3_high}"
EUROC_SEQS="${EUROC_SEQS:-MH_01_easy MH_02_easy MH_03_medium MH_04_difficult MH_05_difficult}"
SELECTED_PARAMS_FILE="${SELECTED_PARAMS_FILE:-results/sem_policy_tuning/selected_params.yaml}"
FORCE="${FORCE:-0}"

if [ ! -f "$SELECTED_PARAMS_FILE" ]; then
    echo "[error] selected params not found: $SELECTED_PARAMS_FILE" >&2
    exit 1
fi
if grep -q "status=draft_incomplete_train" "$SELECTED_PARAMS_FILE"; then
    echo "[error] selected params are draft/incomplete: $SELECTED_PARAMS_FILE" >&2
    exit 1
fi

run_viode_env() {
    local env_name="$1"
    echo "============================================================"
    echo "[paper-n5] VIODE ${env_name} N=${N} methods=${METHODS}"
    echo "============================================================"
    PROTOCOL_TAG="$TAG" \
    N="$N" \
    FORCE="$FORCE" \
    FAIR_BAG_RATE=1 \
    SAD_BAG_RATE=1.0 \
    SKIP_EUROC=1 \
    EUROC_SEQS="__none__" \
    VIODE_ENV="$env_name" \
    VIODE_LEVELS="$VIODE_LEVELS" \
    METHODS="$METHODS" \
    SEM_POLICY_PARAMS_FILE="$SELECTED_PARAMS_FILE" \
    ./scripts/run_sem_geodf_ablation.sh full
}

run_euroc() {
    echo "============================================================"
    echo "[paper-n5] EuRoC N=${N} methods=${METHODS}"
    echo "============================================================"
    PROTOCOL_TAG="$TAG" \
    N="$N" \
    FORCE="$FORCE" \
    FAIR_BAG_RATE=1 \
    SAD_BAG_RATE=1.0 \
    VIODE_LEVELS="__none__" \
    EUROC_SEQS="$EUROC_SEQS" \
    METHODS="$METHODS" \
    SEM_POLICY_PARAMS_FILE="$SELECTED_PARAMS_FILE" \
    ./scripts/run_sem_geodf_ablation.sh full
}

run_viode_env city_day
run_viode_env city_night
run_viode_env parking_lot
run_euroc

ROOT="results/sem_geodf_ablation/${TAG}"
python3 scripts/summarize_sem_geodf_ablation.py \
    --root "$ROOT" \
    --out "${ROOT}/PAPER_N5_SUMMARY.md"
python3 scripts/export_ablation_analysis.py \
    --root "$ROOT" \
    --out-csv "${ROOT}/PAPER_N5_ANALYSIS.csv" \
    --out-md "${ROOT}/PAPER_N5_ANALYSIS.md"

echo "[paper-n5] done: ${ROOT}/PAPER_N5_SUMMARY.md"
