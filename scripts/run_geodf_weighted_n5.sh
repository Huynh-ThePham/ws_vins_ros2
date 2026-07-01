#!/usr/bin/env bash
# Full GeoDF-Weighted benchmark: N trials of baseline + weighted on all VIODE conditions.
# Wrapper around run_viode_n5.sh (same interface as the adaptive paper pipeline).
#
# Usage: ./scripts/run_geodf_weighted_n5.sh [N]
set -eo pipefail

N="${1:-5}"
WS="$(cd "$(dirname "$0")/.." && pwd)"
exec bash "${WS}/scripts/run_viode_n5.sh" "$N"
