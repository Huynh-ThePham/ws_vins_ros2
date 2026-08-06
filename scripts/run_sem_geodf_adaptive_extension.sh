#!/usr/bin/env bash
# Adaptive-extension protocol: full_adaptive ONLY (plan P0.2).
#
# Never merge these cells into the fixed-backbone main claim matrix.
# Results live under a separate protocol tag so the expected-matrix validator
# for the main paper cannot accidentally consume them.
#
# Usage:
#   ./scripts/run_sem_geodf_adaptive_extension.sh [quick|full]
set -euo pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"

SCOPE="${1:-quick}"
export PUBLICATION_MODE="${PUBLICATION_MODE:-1}"
export METHODS="full_adaptive"
export PROTOCOL_TAG="${PROTOCOL_TAG:-sem-geodf-adaptive-v2}"
export CLAIM_MATRIX="adaptive_extension"
export FAIR_BAG_RATE="${FAIR_BAG_RATE:-1}"
export SAD_BAG_RATE="${SAD_BAG_RATE:-1.0}"
export FORCE="${FORCE:-0}"

echo "[adaptive-extension] full_adaptive only; tag=$PROTOCOL_TAG claim=$CLAIM_MATRIX"
exec bash "${WS}/scripts/run_sem_geodf_ablation.sh" "$SCOPE"
