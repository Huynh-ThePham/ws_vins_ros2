#!/usr/bin/env bash
# GeoDF-VINS-Hard v2 comparison: baseline vs adaptive(v1) vs adaptive_v2 (B auto-rho + F stereo).
# Single-session run so trajectory deltas are internally consistent despite VINS non-determinism.
#
# Produces, per run dir under results/:
#   eval/metrics.json   (ATE/RPE)
#   geo_df_stats.csv    (per-frame activation; v2 also logs rho_on, outlier_floor, stereo_added)
# and detection_eval.json for the v1 (geodf_dump) and v2 (geodf_dump_v2) feature dumps.
#
# Env overrides: VIODE_ENVS, VIODE_LEVELS, EUROC_SEQS, EUROC_METHODS, SKIP_EUROC=1, SKIP_VIODE=1
set -o pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"
cd "$WS"

export FORCE=1
STAMP="$(date +%Y%m%d_%H%M%S)"
echo "[v2cmp] === start ${STAMP} ==="

VIODE_ENVS="${VIODE_ENVS:-city_day city_night parking_lot}"
VIODE_LEVELS="${VIODE_LEVELS:-0_none 1_low 2_mid 3_high}"
VIODE_TRAJ_METHODS="${VIODE_TRAJ_METHODS:-baseline adaptive adaptive_v2}"
EUROC_SEQS="${EUROC_SEQS:-MH_01_easy MH_02_easy MH_03_medium MH_04_difficult MH_05_difficult}"
EUROC_METHODS="${EUROC_METHODS:-baseline adaptive_v2}"

# ---------------------------------------------------------------- VIODE trajectory
if [ "${SKIP_VIODE:-0}" != "1" ]; then
  for env in $VIODE_ENVS; do
    for m in $VIODE_TRAJ_METHODS; do
      echo "[v2cmp] VIODE traj env=$env method=$m"
      VIODE_ENV="$env" bash scripts/run_geodf_viode.sh "$VIODE_LEVELS" "$m" \
        || echo "[v2cmp][WARN] viode traj $env $m failed"
    done
  done

  # VIODE detection dumps (always-on + stereo cross-check) for v2 detection eval
  for env in $VIODE_ENVS; do
    echo "[v2cmp] VIODE dump_v2 env=$env"
    VIODE_ENV="$env" bash scripts/run_geodf_viode.sh "$VIODE_LEVELS" "geodf_dump_v2" \
      || echo "[v2cmp][WARN] viode dump_v2 $env failed"
  done
fi

# ---------------------------------------------------------------- EuRoC (static no-regression)
if [ "${SKIP_EUROC:-0}" != "1" ]; then
  for seq in $EUROC_SEQS; do
    for m in $EUROC_METHODS; do
      echo "[v2cmp] EuRoC seq=$seq method=$m"
      bash scripts/run_geodf_euroc.sh "$seq" "$m" "" --eval \
        || echo "[v2cmp][WARN] euroc $seq $m failed"
    done
  done
fi

# ---------------------------------------------------------------- Detection eval: v2 dumps (reuse v1 masks)
echo "[v2cmp] detection eval (v2 dumps vs simulator masks)"
for env in $VIODE_ENVS; do
  for l in $VIODE_LEVELS; do
    feats="results/viode/${env}_${l}_geodf_dump_v2/geo_df_features.csv"
    masks="results/viode/${env}_${l}_geodf_dump/masks"
    out="results/viode/${env}_${l}_geodf_dump_v2/detection_eval.json"
    if [ -f "$feats" ] && [ -d "$masks" ]; then
      python3 scripts/eval_viode_detection.py --features "$feats" --mask-dir "$masks" --out "$out" \
        && echo "[v2cmp]   det ok $env $l" \
        || echo "[v2cmp][WARN] det failed $env $l"
    else
      echo "[v2cmp][skip] det $env $l (feats=$([ -f "$feats" ] && echo y || echo n) masks=$([ -d "$masks" ] && echo y || echo n))"
    fi
  done
done

echo "[v2cmp] === done ${STAMP} ==="
