#!/usr/bin/env bash
# Full pre-claim study: 12 VIODE conditions × 3 configs × N repeats + mask eval.
#
# Configs:
#   baseline      — VINS-Fusion stereo-IMU
#   adaptive_2d   — GeoDF-Adaptive, temporal 2D-F gate only
#   adaptive      — GeoDF-Adaptive, stereo 3D motion consistency (PROPOSED)
#
# Phases:
#   1) VIODE trajectory N-repeat (12 cells × 3 methods)
#   2) Feature-level mask eval (12 cells × 2d + 3d, 1 run/cell)
#   3) Summarize trajectory + detection + claim check
#
# Usage: ./scripts/run_stereo3d_claim_study.sh [N]
# Env: VIODE_ROOT, FORCE=1, SKIP_TRAJECTORY=1, SKIP_MASK=1
set -eo pipefail

N="${1:-5}"
WS="$(cd "$(dirname "$0")/.." && pwd)"
unset METHODS 2>/dev/null || true
export METHODS="baseline adaptive_2d adaptive"
export VIODE_ROOT="${VIODE_ROOT:-/media/theph/Data1/Research/Datasets/Viode}"
export FORCE="${FORCE:-0}"
export EUROC_ROOT="${EUROC_ROOT:-/home/theph/ws_vins_ros2/data/euroc_benchmark}"

STAMP="$(date +%Y%m%d_%H%M%S)"
LOG="${WS}/logs/stereo3d_claim_${STAMP}.log"
mkdir -p "${WS}/logs" "${WS}/results/geodf_evaluation"

exec > >(tee -a "$LOG") 2>&1
echo "[claim-study] === start N=$N stamp=$STAMP ==="
echo "[claim-study] METHODS=$METHODS  FORCE=$FORCE"
echo "[claim-study] log=$LOG"
echo "[claim-study] planned trajectory runs: $(( 3 * 4 * 3 * N ))  (3 env × 4 level × 3 method × N)"
echo "[claim-study] planned mask eval runs: 24  (12 cells × 2d+3d)"

# shellcheck source=scripts/lib/geodf_common.sh
source "${WS}/scripts/lib/geodf_common.sh"
source_ros2_ws "$WS"

resolve_viode_root >/dev/null || {
    echo "ERROR: VIODE dataset not found. Set VIODE_ROOT=/media/theph/Data1/Research/Datasets/Viode" >&2
    exit 1
}

manifest="${WS}/results/viode_repeat/manifest.json"
if [ ! -f "$manifest" ]; then
    echo "[claim-study] preparing VIODE bags/GT..."
    bash "${WS}/scripts/run_viode_n5_prepare.sh" "$N"
fi

python3 "${WS}/scripts/capture_provenance.py" \
    --study-dir "${WS}/results/viode_repeat" --study "stereo3d_claim_n${N}" \
    --configs "src/config/viode/*adaptive*.yaml" || true

if [ "${SKIP_TRAJECTORY:-0}" != "1" ]; then
    echo "[claim-study] === Phase 1/3: VIODE trajectory N=$N (12 cells) ==="
    for env in city_day city_night parking_lot; do
        echo "[claim-study] env=$env"
        VIODE_ENV="$env" bash "${WS}/scripts/run_geodf_repeat.sh" \
            "0_none 1_low 2_mid 3_high" "$METHODS" "$N"
    done
fi

if [ "${SKIP_MASK:-0}" != "1" ]; then
    echo "[claim-study] === Phase 2/3: feature-level mask eval (2D vs 3D) ==="
    METHODS="adaptive_2d adaptive" bash "${WS}/scripts/run_viode_mask_eval.sh"
fi

echo "[claim-study] === Phase 3/3: summarize ==="
python3 "${WS}/scripts/summarize_stereo3d_ablation.py" \
    --viode-root "${WS}/results/viode_repeat" \
    --out "${WS}/results/geodf_evaluation/STEREO3D_CLAIM_N${N}.md" \
    --out-json "${WS}/results/geodf_evaluation/stereo3d_claim_n${N}.json" \
    --full-grid --max-trials "$N" --skip-euroc

echo "[claim-study] === COMPLETE ==="
bash "${WS}/scripts/monitor_paper_benchmark.sh" 2>/dev/null || true
ls -1 "${WS}/results/geodf_evaluation/"STEREO3D_CLAIM* "${WS}/results/geodf_evaluation/"MASK_EVAL* 2>/dev/null | sed 's|^|  |'
