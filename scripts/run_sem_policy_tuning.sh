#!/usr/bin/env bash
# Offline Semantic policy tuning: city_day train → hold-out report + sensitivity table.
#
# Prerequisites: at least one sem_geodf fusion run per city_day level with
# sem_geodf_stats.csv (from run_sem_geodf_ablation.sh). Hold-out sequences
# (city_night, parking_lot, EuRoC) are reported when present but never used
# for selection.
#
# Usage:
#   ./scripts/run_sem_policy_tuning.sh
#   ./scripts/run_sem_policy_tuning.sh --sweep-overlap
#   ./scripts/run_sem_policy_tuning.sh --allow-incomplete-train   # draft only
set -eo pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"
cd "$WS"

SWEEP_OVERLAP=0
ROOTS=(
    results/sem_geodf_ablation
    results/sem_geodf_ablation/fair1p0
    results/sem_geodf_compare
)
OUT_DIR=results/sem_policy_tuning
EXTRA_ARGS=()

while [ "$#" -gt 0 ]; do
    case "$1" in
        --sweep-overlap)
            SWEEP_OVERLAP=1
            shift
            ;;
        --allow-incomplete-train)
            EXTRA_ARGS+=(--allow-incomplete-train)
            shift
            ;;
        --out-dir)
            OUT_DIR="$2"
            shift 2
            ;;
        --roots)
            ROOTS=()
            shift
            while [ "$#" -gt 0 ] && [[ "$1" != --* ]]; do
                ROOTS+=("$1")
                shift
            done
            ;;
        *)
            echo "[error] unknown argument: $1" >&2
            exit 1
            ;;
    esac
done

echo "[sem_policy_tuning] Step 1/2: select thresholds on city_day train"
TUNE_ARGS=(--roots "${ROOTS[@]}" --out-dir "$OUT_DIR" "${EXTRA_ARGS[@]}")
if [ "$SWEEP_OVERLAP" = "1" ]; then
    TUNE_ARGS+=(--sweep-overlap)
fi
python3 scripts/tune_sem_policy.py "${TUNE_ARGS[@]}"

echo "[sem_policy_tuning] Step 2/2: sensitivity table + hold-out report"
SENS_ARGS=(--roots "${ROOTS[@]}" --out-dir "$OUT_DIR" --overlap-sweep "${EXTRA_ARGS[@]}")
python3 scripts/report_sem_policy_sensitivity.py "${SENS_ARGS[@]}"

echo "[sem_policy_tuning] Done. See ${OUT_DIR}/SENSITIVITY_TABLE.md"
