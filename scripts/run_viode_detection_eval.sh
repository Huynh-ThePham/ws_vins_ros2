#!/usr/bin/env bash
# VIODE detection eval: masks + precision/recall vs GeoDF feature dump (ROS 2).
set -eo pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"
VIODE_ENV="${VIODE_ENV:-city_day}"
LEVELS="${1:-0_none 1_low 2_mid 3_high}"
CFG="${WS}/src/config/viode"

# shellcheck source=scripts/lib/geodf_common.sh
source "${WS}/scripts/lib/geodf_common.sh"
VIODE="$(resolve_viode_root)" || exit 1

for level in $LEVELS; do
    bag="${VIODE}/${VIODE_ENV}/${level}.bag"
    run="${VIODE_ENV}_${level}_geodf_dump"
    out="${WS}/results/viode/${run}"
    mask_dir="${out}/masks"
    [ -f "$bag" ] || { echo "[skip] $bag"; continue; }
    [ -f "${out}/geo_df_features.csv" ] || { echo "[skip] no features ${out}/geo_df_features.csv"; continue; }
    echo "=== detection eval $run ==="
    python3 "${WS}/scripts/viode_make_masks.py" \
        --bag "$bag" \
        --out-dir "$mask_dir" \
        --rgb-ids "${CFG}/rgb_ids.txt" \
        --vehicle-ids "${CFG}/vehicle_ids_city_day.txt"
    python3 "${WS}/scripts/eval_viode_detection.py" \
        --features "${out}/geo_df_features.csv" \
        --mask-dir "$mask_dir" \
        --out "${out}/detection_eval.json"
done

MASK_ROOT="${WS}/results/viode/masks"
mkdir -p "$MASK_ROOT"
for level in $LEVELS; do
    src="${WS}/results/viode/${VIODE_ENV}_${level}_geodf_dump/masks"
    if [ -d "$src" ]; then
        ln -sfn "../${VIODE_ENV}_${level}_geodf_dump/masks" "${MASK_ROOT}/${VIODE_ENV}_${level}"
    fi
done

python3 "${WS}/scripts/eval_viode_detection.py" \
    --root "${WS}/results/viode" \
    --mask-root "$MASK_ROOT" \
    --env "$VIODE_ENV" \
    --levels "$LEVELS"

echo "[detection] done -> ${WS}/results/viode/viode_${VIODE_ENV}_detection.md"
