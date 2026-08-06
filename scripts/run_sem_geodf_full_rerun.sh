#!/usr/bin/env bash
# Full Semantic–GeoDF fixed-backbone rerun with fail-closed publication gate (P0.5).
#
# Flow:
#   run all cells
#   -> validate expected matrix
#   -> validate provenance
#   -> validate failures and coverage
#   -> export analysis
#   -> generate paper assets
#   -> compile paper
#
# Usage:
#   ./scripts/run_sem_geodf_full_rerun.sh
set -euo pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"
TS="$(date +%Y%m%d_%H%M%S)"
LOG="${WS}/logs/sem_geodf_full_rerun_${TS}.log"
mkdir -p "${WS}/logs"

export N="${N:-3}"
export FORCE="${FORCE:-0}"
export FAIR_BAG_RATE="${FAIR_BAG_RATE:-1}"
export SAD_BAG_RATE="${SAD_BAG_RATE:-1.0}"
export SEM_POLICY_VIODE_LEVEL_OVERRIDE=0
export ORACLE_ABLATION=0
export PUBLICATION_MODE=1
export ALLOW_MISSING_DATA=0
export PROTOCOL_VERSION="${PROTOCOL_VERSION:-sem-geodf-fair-v2}"
export PROTOCOL_TAG="${PROTOCOL_TAG:-${PROTOCOL_VERSION}}"
export CLAIM_MATRIX=fixed_backbone_main
export YOLO_DEVICE="${YOLO_DEVICE:-cuda}"
export METHODS="${METHODS:-baseline geodf semantic union_noweight union_weight}"

EXPECTED_MATRIX="${EXPECTED_MATRIX:-${WS}/experiments/sem_geodf_expected_matrix.json}"
ABLATION_ROOT="${WS}/results/sem_geodf_ablation/${PROTOCOL_TAG}"

exec > >(tee -a "$LOG") 2>&1
echo "[full-rerun] start $(date -Is) log=$LOG"
echo "[full-rerun] N=$N FORCE=$FORCE PROTOCOL_TAG=$PROTOCOL_TAG PUBLICATION_MODE=1"
echo "[full-rerun] METHODS=$METHODS expected=$EXPECTED_MATRIX"

cd "$WS"
bash "${WS}/scripts/setup_viode_gt_cache.sh"

COMMON=(N="$N" FORCE="$FORCE" FAIR_BAG_RATE="$FAIR_BAG_RATE" SAD_BAG_RATE="$SAD_BAG_RATE"
        METHODS="$METHODS" PROTOCOL_TAG="$PROTOCOL_TAG" PROTOCOL_VERSION="$PROTOCOL_VERSION"
        PUBLICATION_MODE=1 CLAIM_MATRIX=fixed_backbone_main ALLOW_MISSING_DATA=0)

if [ "${SKIP_EUROC:-0}" != "1" ]; then
  echo "[full-rerun] === EuRoC 5×MH ==="
  env "${COMMON[@]}" VIODE_LEVELS=__none__ SKIP_VIODE=1 \
      EUROC_SEQS="MH_01_easy MH_02_easy MH_03_medium MH_04_difficult MH_05_difficult" \
    bash "${WS}/scripts/run_sem_geodf_ablation.sh" full
fi

if [ "${SKIP_VIODE:-0}" != "1" ]; then
  for env_name in city_day city_night parking_lot; do
    echo "[full-rerun] === VIODE env=$env_name ==="
    env "${COMMON[@]}" VIODE_ENV="$env_name" VIODE_LEVELS="0_none 1_low 2_mid 3_high" \
        EUROC_SEQS=__none__ SKIP_EUROC=1 \
      bash "${WS}/scripts/run_sem_geodf_ablation.sh" full
  done
fi

echo "[full-rerun] === validate expected matrix ==="
python3 "${WS}/scripts/validate_experiment_matrix.py" \
  --root "$ABLATION_ROOT" \
  --expected "$EXPECTED_MATRIX" \
  --report "${ABLATION_ROOT}/validation.json" \
  --write-receipt "${ABLATION_ROOT}/validation_receipt.json"

echo "[full-rerun] === audit resolved configs in tree ==="
python3 "${WS}/scripts/audit_method_config_diff.py" --resolved "$ABLATION_ROOT"

echo "[full-rerun] === export analysis ==="
python3 "${WS}/scripts/summarize_sem_geodf_ablation.py" \
  --root "$ABLATION_ROOT" \
  --out "${ABLATION_ROOT}/ABLATION_SUMMARY.md"
python3 "${WS}/scripts/export_ablation_analysis.py" \
  --root "$ABLATION_ROOT" \
  --out-csv "${ABLATION_ROOT}/ABLATION_ANALYSIS.csv" \
  --out-md "${ABLATION_ROOT}/ABLATION_ANALYSIS.md" \
  --paired

echo "[full-rerun] === generate paper assets (gated) ==="
python3 "${WS}/scripts/make_sem_geodf_paper_assets.py" \
  --root "$ABLATION_ROOT" \
  --out "${WS}/paper/sem_geodf" \
  --require-receipt "${ABLATION_ROOT}/validation_receipt.json"

echo "[full-rerun] === compile paper ==="
(
  cd "${WS}/paper/sem_geodf/en"
  latexmk -pdf -interaction=nonstopmode -halt-on-error main.tex
)
(
  cd "${WS}/paper/sem_geodf/vi"
  latexmk -pdf -interaction=nonstopmode -halt-on-error main.tex
)

echo "[done] $(date -Is)"
echo "[done] results: ${ABLATION_ROOT}"
echo "[done] validation: ${ABLATION_ROOT}/validation.json"
echo "[done] receipt: ${ABLATION_ROOT}/validation_receipt.json"
