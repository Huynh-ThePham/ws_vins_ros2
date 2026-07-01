#!/usr/bin/env bash
# Full GeoDF-Weighted benchmark: N trials on all VIODE conditions.
#
# Usage: ./scripts/run_geodf_weighted_n5.sh [N]
set -eo pipefail

N="${1:-5}"
WS="$(cd "$(dirname "$0")/.." && pwd)"

echo "[weighted-n5] VIODE all conditions N=$N"
bash "${WS}/scripts/run_geodf_weighted.sh" "0_none 1_low 2_mid 3_high" \
    "city_day city_night parking_lot" "$N"

echo "[weighted-n5] done"
