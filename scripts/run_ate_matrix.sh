#!/usr/bin/env bash
# Frozen BEFORE/AFTER protocol for the ATE reduction study.
#
# One tag = one binary. Train scenes and the hold-out scene are run with the same
# seed, rate, methods and trial count, then summarized together. Nothing here
# reads ground truth, sequence difficulty or VIODE dynamic level; those are only
# used by the offline evaluator.
#
#   PROTOCOL_TAG=after-p0-<sha> METHODS="baseline union_weight" bash scripts/run_ate_matrix.sh
set -eo pipefail
WS="$(cd "$(dirname "$0")/.." && pwd)"
set -u

PROTOCOL_TAG="${PROTOCOL_TAG:?PROTOCOL_TAG is required}"
METHODS="${METHODS:-baseline union_weight}"
N="${N:-3}"
ADAPTATION_MODE="${ADAPTATION_MODE:-off}"
TRAIN_EUROC="${TRAIN_EUROC:-MH_03_medium MH_04_difficult MH_05_difficult}"
TRAIN_VIODE_ENV="${TRAIN_VIODE_ENV:-city_day}"
TRAIN_VIODE_LEVELS="${TRAIN_VIODE_LEVELS:-2_mid 3_high}"
HOLDOUT_VIODE_ENV="${HOLDOUT_VIODE_ENV:-city_night}"
HOLDOUT_VIODE_LEVELS="${HOLDOUT_VIODE_LEVELS:-3_high}"
SKIP_HOLDOUT="${SKIP_HOLDOUT:-0}"

ROOT="${WS}/results/sem_geodf_ablation/${PROTOCOL_TAG}"
LOG="${WS}/logs/${PROTOCOL_TAG}.log"
mkdir -p "${WS}/logs" "$ROOT"

SHA="$(git -C "$WS" rev-parse HEAD)"
if [ -n "$(git -C "$WS" status --porcelain -- src)" ]; then
    echo "[fatal] src/ is dirty; commit before running a protocol tag" >&2
    exit 2
fi

cat >"${ROOT}/RUN_META.json" <<EOF
{
  "protocol_tag": "${PROTOCOL_TAG}",
  "git_sha": "${SHA}",
  "created_utc": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "methods": "${METHODS}",
  "trials_per_cell": ${N},
  "train_euroc": "${TRAIN_EUROC}",
  "train_viode": "${TRAIN_VIODE_ENV} ${TRAIN_VIODE_LEVELS}",
  "holdout_viode": "${HOLDOUT_VIODE_ENV} ${HOLDOUT_VIODE_LEVELS}",
  "bag_rate": "1.0",
  "adaptation_mode": "${ADAPTATION_MODE}"
}
EOF

run_stage() {
    local stage="$1" euroc="$2" env="$3" levels="$4" skip_euroc="$5"
    echo "[matrix] stage=${stage} euroc='${euroc}' viode=${env}/${levels} adaptation=${ADAPTATION_MODE}" | tee -a "$LOG"
    env PROTOCOL_TAG="$PROTOCOL_TAG" \
        PROTOCOL_VERSION=sem-geodf-fair-v2 \
        CLAIM_MATRIX=adaptive_extension \
        ADAPTATION_MODE="$ADAPTATION_MODE" \
        N="$N" \
        METHODS="$METHODS" \
        EUROC_SEQS="$euroc" \
        SKIP_EUROC="$skip_euroc" \
        VIODE_ENV="$env" \
        VIODE_LEVELS="$levels" \
        PUBLICATION_MODE=0 \
        FORCE="${FORCE:-0}" \
        bash "${WS}/scripts/run_sem_geodf_ablation.sh" full >>"$LOG" 2>&1
}

run_stage train "$TRAIN_EUROC" "$TRAIN_VIODE_ENV" "$TRAIN_VIODE_LEVELS" 0
if [ "$SKIP_HOLDOUT" != "1" ]; then
    run_stage holdout "__none__" "$HOLDOUT_VIODE_ENV" "$HOLDOUT_VIODE_LEVELS" 1
fi

python3 "${WS}/scripts/summarize_before_ate.py" \
    --root "$ROOT" --out "${ROOT}/ATE_TABLE.md" >>"$LOG" 2>&1
echo "DONE ${SHA} $(date -Is)" >"${ROOT}/MATRIX_COMPLETE.flag"
echo "[matrix] done -> ${ROOT}/ATE_TABLE.md" | tee -a "$LOG"
