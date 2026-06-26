#!/usr/bin/env bash
# Full Paper #2 benchmark: GeoDF-Inertial N trials on all VIODE conditions.
# Runs sequentially (one pht_vio_node at a time).
#
# Usage: ./scripts/run_geodf_inertial_n5.sh [N]
set -eo pipefail

N="${1:-5}"
WS="$(cd "$(dirname "$0")/.." && pwd)"
export FORCE="${FORCE:-0}"

echo "[inertial-n5] VIODE all conditions N=$N"
bash "${WS}/scripts/run_geodf_inertial.sh" "0_none 1_low 2_mid 3_high" \
    "city_day city_night parking_lot" "$N"

echo "[inertial-n5] summarize"
python3 "${WS}/scripts/summarize_inertial_n5.py"

echo "[inertial-n5] done"
