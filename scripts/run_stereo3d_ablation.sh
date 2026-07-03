#!/usr/bin/env bash
# Short 3-way ablation: baseline vs old 2D-F adaptive vs new stereo-3D adaptive.
#
# Target cells (minimal claim set):
#   VIODE  city_day/0_none  — static pass-through
#   VIODE  city_day/3_high — dynamic stress
#   EuRoC  MH_01..MH_05    — static safety
#
# Reuses existing baseline/adaptive trials when present (FORCE=0).
# Only adaptive_2d is new; typically ~2×VIODE + 5×EuRoC cells × N trials.
#
# Usage:
#   ./scripts/run_stereo3d_ablation.sh [N]
# Env:
#   EUROC_ROOT=/home/theph/ws_vins_ros2/data/euroc_benchmark
#   FORCE=1   redo all trials for the 3 methods on target cells
#   RUN_ALL=1 also run baseline+adaptive (default: all 3, skip existing)
set -eo pipefail

N="${1:-5}"
VIODE_N="$N"
EUROC_N=$(( N > 3 ? 3 : N ))
WS="$(cd "$(dirname "$0")/.." && pwd)"
export METHODS="${METHODS:-baseline adaptive_2d adaptive}"
export EUROC_ROOT="${EUROC_ROOT:-/home/theph/ws_vins_ros2/data/euroc_benchmark}"
export VIODE_ROOT="${VIODE_ROOT:-/media/theph/Data1/Research/Datasets/Viode}"
export FORCE="${FORCE:-0}"

STAMP="$(date +%Y%m%d_%H%M%S)"
LOG="${WS}/logs/stereo3d_ablation_${STAMP}.log"
OUT_EVAL="${WS}/results/geodf_evaluation"
mkdir -p "${WS}/logs" "$OUT_EVAL"

exec > >(tee -a "$LOG") 2>&1
echo "[stereo3d-ablation] === start N=$N stamp=$STAMP ==="
echo "[stereo3d-ablation] METHODS=$METHODS"
echo "[stereo3d-ablation] log=$LOG"

# shellcheck source=scripts/lib/geodf_common.sh
source "${WS}/scripts/lib/geodf_common.sh"
source_ros2_ws "$WS"

if ! resolve_euroc_root >/dev/null 2>&1; then
    echo "ERROR: EuRoC not found. Set EUROC_ROOT=/home/theph/ws_vins_ros2/data/euroc_benchmark" >&2
    exit 1
fi

echo "[stereo3d-ablation] === VIODE: city_day 0_none + 3_high ==="
# baseline + adaptive (3D) already exist from N=5 main study — only run 2D ablation if missing.
VIODE_ENV=city_day bash "${WS}/scripts/run_geodf_repeat.sh" "0_none 3_high" "adaptive_2d" "$VIODE_N"

echo "[stereo3d-ablation] === EuRoC: MH_01..MH_05 (all 3 configs) ==="
export METHODS
bash "${WS}/scripts/run_euroc_n3.sh" "$EUROC_N"

echo "[stereo3d-ablation] === summarize ==="
python3 "${WS}/scripts/summarize_stereo3d_ablation.py" \
    --out "${OUT_EVAL}/STEREO3D_ABLATION.md" \
    --out-json "${OUT_EVAL}/stereo3d_ablation.json"

echo "[stereo3d-ablation] === done ==="
cat "${OUT_EVAL}/STEREO3D_ABLATION.md"
