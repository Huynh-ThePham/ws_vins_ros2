#!/usr/bin/env bash
# Reproducible Semantic policy protocol:
#   1) tune thresholds only on VIODE city_day (train)
#   2) report final ablations on city_night + parking_lot + EuRoC (hold-out)
#
# Usage:
#   ./scripts/run_sem_policy_protocol.sh train    # city_day sem_geodf stats
#   ./scripts/run_sem_policy_protocol.sh tune     # select params from train only
#   ./scripts/run_sem_policy_protocol.sh holdout  # final report runs with selected params
#   ./scripts/run_sem_policy_protocol.sh report   # regenerate reports from existing runs
#   ./scripts/run_sem_policy_protocol.sh all
#
# Env:
#   TRAIN_N=1
#   HOLDOUT_N=3
#   TRAIN_TAG=sem_policy_train_city_day
#   HOLDOUT_TAG=sem_policy_holdout_fair1p0
#   OUT_DIR=results/sem_policy_tuning
#   HOLDOUT_METHODS="baseline adaptive sad_sem sequential sem_geodf sem_geodf_mask_gated"
#   ALLOW_INCOMPLETE_TRAIN=0  # set 1 only for draft/debug
set -eo pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"
cd "$WS"

MODE="${1:-all}"
TRAIN_N="${TRAIN_N:-1}"
HOLDOUT_N="${HOLDOUT_N:-3}"
TRAIN_TAG="${TRAIN_TAG:-sem_policy_train_city_day}"
HOLDOUT_TAG="${HOLDOUT_TAG:-sem_policy_holdout_fair1p0}"
OUT_DIR="${OUT_DIR:-results/sem_policy_tuning}"
HOLDOUT_METHODS="${HOLDOUT_METHODS:-baseline adaptive sad_sem sequential sem_geodf sem_geodf_mask_gated}"
VIODE_LEVELS_ALL="${VIODE_LEVELS_ALL:-0_none 1_low 2_mid 3_high}"
EUROC_SEQS_ALL="${EUROC_SEQS_ALL:-MH_01_easy MH_02_easy MH_03_medium MH_04_difficult MH_05_difficult}"
SELECTED_PARAMS_FILE="${SELECTED_PARAMS_FILE:-${OUT_DIR}/selected_params.yaml}"

run_train() {
    echo "[policy_protocol] train: city_day sem_geodf only (no hold-out)"
    PROTOCOL_TAG="$TRAIN_TAG" \
    N="$TRAIN_N" \
    METHODS="sem_geodf" \
    EUROC_SEQS="__none__" \
    VIODE_ENV="city_day" \
    VIODE_LEVELS="$VIODE_LEVELS_ALL" \
    FAIR_BAG_RATE=1 \
    SAD_BAG_RATE=1.0 \
    FORCE="${FORCE_TRAIN:-0}" \
    ./scripts/run_sem_geodf_ablation.sh full
}

run_tune() {
    echo "[policy_protocol] tune: select params from ${TRAIN_TAG}/city_day only"
    local extra=()
    if [ "${ALLOW_INCOMPLETE_TRAIN:-0}" = "1" ]; then
        extra+=(--allow-incomplete-train)
    fi
    ./scripts/run_sem_policy_tuning.sh \
        --roots "results/sem_geodf_ablation/${TRAIN_TAG}" \
        "results/sem_geodf_ablation/${HOLDOUT_TAG}" \
        --out-dir "$OUT_DIR" \
        "${extra[@]}"
}

run_holdout_viode_env() {
    local env_name="$1"
    echo "[policy_protocol] holdout: VIODE ${env_name}"
    PROTOCOL_TAG="$HOLDOUT_TAG" \
    N="$HOLDOUT_N" \
    METHODS="$HOLDOUT_METHODS" \
    EUROC_SEQS="__none__" \
    VIODE_ENV="$env_name" \
    VIODE_LEVELS="$VIODE_LEVELS_ALL" \
    FAIR_BAG_RATE=1 \
    SAD_BAG_RATE=1.0 \
    SEM_POLICY_PARAMS_FILE="$SELECTED_PARAMS_FILE" \
    FORCE="${FORCE_HOLDOUT:-0}" \
    ./scripts/run_sem_geodf_ablation.sh full
}

run_holdout_euroc() {
    echo "[policy_protocol] holdout: EuRoC generalization"
    PROTOCOL_TAG="$HOLDOUT_TAG" \
    N="$HOLDOUT_N" \
    METHODS="$HOLDOUT_METHODS" \
    EUROC_SEQS="$EUROC_SEQS_ALL" \
    VIODE_LEVELS="__none__" \
    FAIR_BAG_RATE=1 \
    SAD_BAG_RATE=1.0 \
    SEM_POLICY_PARAMS_FILE="$SELECTED_PARAMS_FILE" \
    FORCE="${FORCE_HOLDOUT:-0}" \
    ./scripts/run_sem_geodf_ablation.sh full
}

run_holdout() {
    if [ ! -f "$SELECTED_PARAMS_FILE" ]; then
        echo "[error] selected params not found: $SELECTED_PARAMS_FILE" >&2
        echo "        Run ./scripts/run_sem_policy_protocol.sh tune first." >&2
        exit 1
    fi
    if python3 - "$SELECTED_PARAMS_FILE" <<'PY'
from pathlib import Path
import sys

text = Path(sys.argv[1]).read_text()
raise SystemExit(0 if "status=draft_incomplete_train" in text else 1)
PY
    then
        if [ "${ALLOW_INCOMPLETE_TRAIN:-0}" != "1" ]; then
            echo "[error] selected params are marked draft_incomplete_train: $SELECTED_PARAMS_FILE" >&2
            echo "        Complete city_day train first, or set ALLOW_INCOMPLETE_TRAIN=1 for debug only." >&2
            exit 1
        fi
    fi
    run_holdout_viode_env city_night
    run_holdout_viode_env parking_lot
    run_holdout_euroc
}

run_report() {
    echo "[policy_protocol] report: hold-out ATE + sensitivity"
    local root="results/sem_geodf_ablation/${HOLDOUT_TAG}"
    mkdir -p "$root" "$OUT_DIR"
    if [ -d "$root" ]; then
        python3 scripts/summarize_sem_geodf_ablation.py \
            --root "$root" \
            --out "${root}/HOLDOUT_SUMMARY.md"
        python3 scripts/export_ablation_analysis.py \
            --root "$root" \
            --out-csv "${root}/HOLDOUT_ANALYSIS.csv" \
            --out-md "${root}/HOLDOUT_ANALYSIS.md"
    fi

    local extra=()
    if [ "${ALLOW_INCOMPLETE_TRAIN:-0}" = "1" ]; then
        extra+=(--allow-incomplete-train)
    fi
    python3 scripts/report_sem_policy_sensitivity.py \
        --roots "results/sem_geodf_ablation/${TRAIN_TAG}" "$root" \
        --out-dir "$OUT_DIR" \
        --overlap-sweep \
        "${extra[@]}"
}

case "$MODE" in
    train)
        run_train
        ;;
    tune)
        run_tune
        ;;
    holdout)
        run_holdout
        ;;
    report)
        run_report
        ;;
    all)
        run_train
        run_tune
        run_holdout
        run_report
        ;;
    *)
        echo "[error] unknown mode: $MODE" >&2
        exit 1
        ;;
esac
