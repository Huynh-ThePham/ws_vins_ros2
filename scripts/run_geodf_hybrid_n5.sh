#!/usr/bin/env bash
# Full Paper #2 benchmark: GeoDF-Hybrid N trials on all VIODE conditions.
#
# Usage: ./scripts/run_geodf_hybrid_n5.sh [N]
set -eo pipefail

N="${1:-5}"
WS="$(cd "$(dirname "$0")/.." && pwd)"
export FORCE="${FORCE:-0}"

echo "[hybrid-n5] VIODE all conditions N=$N"
bash "${WS}/scripts/run_geodf_hybrid.sh" "0_none 1_low 2_mid 3_high" \
    "city_day city_night parking_lot" "$N"

echo "[hybrid-n5] summarize"
python3 "${WS}/scripts/summarize_hybrid_n5.py"

echo "[hybrid-n5] done"
