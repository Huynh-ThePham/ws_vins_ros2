#!/usr/bin/env bash
# Run the GeoDF-Weighted method together with the baseline it is compared
# against, on VIODE conditions.
#
# Self-contained: prepares the ROS 2 bag + cached ground-truth CSV from the raw
# VIODE .bag on first use (raw .bag may be unmounted; already-prepared bags/GT
# are reused). Runs N trials per method into separate trial dirs so trajectory
# accuracy can be reported as mean +/- std (VINS-Fusion is non-deterministic).
#
# Usage: ./scripts/run_geodf_weighted.sh "[LEVELS]" "[ENVS]" [N]
#   LEVELS default: "0_none 1_low 2_mid 3_high"
#   ENVS   default: "city_day city_night parking_lot"
#   N      default: 5
# Env:
#   METHODS      space-separated methods (default "baseline weighted")
#   VIODE_ROOT   raw VIODE dataset root (needed only to prepare missing bags/GT)
#   FORCE=1      redo existing trials
#   SKIP_SUMMARY=1  skip the per-env mean/std summary
set -eo pipefail

LEVELS="${1:-0_none 1_low 2_mid 3_high}"
ENVS="${2:-city_day city_night parking_lot}"
N="${3:-5}"
METHODS="${METHODS:-baseline weighted}"

WS="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=scripts/lib/geodf_common.sh
source "${WS}/scripts/lib/geodf_common.sh"
source_ros2_ws "$WS"

VIODE="$(resolve_viode_root 2>/dev/null || true)"
OUT_ROOT="${WS}/results/viode_repeat"
CFG_DIR="$(ros2 pkg prefix pht_vio_ros)/share/pht_vio_ros/config/viode"
mkdir -p "$OUT_ROOT" "${WS}/logs"

for env in $ENVS; do
    for level in $LEVELS; do
        bag="${WS}/data/viode_ros2/${env}/${level}/ros2_bag"
        gt="${OUT_ROOT}/${env}_${level}_gt.csv"
        raw="${VIODE:+${VIODE}/${env}/${level}.bag}"

        # Prepare the ROS 2 bag + GT once, only if missing and the raw bag is mounted.
        if { [ ! -d "$bag" ] || [ ! -f "$bag/metadata.yaml" ]; } && [ -n "$raw" ] && [ -f "$raw" ]; then
            bash "${WS}/scripts/viode_prepare_ros2_bag.sh" "$raw" "$bag"
        fi
        if [ ! -f "$gt" ] && [ -n "$raw" ] && [ -f "$raw" ]; then
            python3 "${WS}/scripts/viode_dump_gt.py" --bag "$raw" --out "$gt"
        fi
        [ -d "$bag" ] || { echo "[skip] no ros2 bag ${env}/${level} (set VIODE_ROOT to prepare)"; continue; }
        [ -f "$gt" ]  || { echo "[skip] no gt ${env}/${level} (set VIODE_ROOT to prepare)"; continue; }

        for method in $METHODS; do
            mode="$(geodf_method_to_mode "$method")" || continue
            RUN_CFG="${CFG_DIR}/viode_${mode}_config_run_${env}_${level}.yaml"
            for i in $(seq 1 "$N"); do
                out="${OUT_ROOT}/${env}_${level}_${method}/trial_${i}"
                if [ "${FORCE:-0}" != "1" ] && [ -f "${out}/eval/metrics.json" ]; then
                    echo "[have] ${env}_${level}_${method} trial ${i}"
                    continue
                fi
                mkdir -p "$out"
                cp "${CFG_DIR}/viode_${mode}_config.yaml" "$RUN_CFG"
                sed -i "s|output_path: \"~/output/\"|output_path: \"${out}/\"|" "$RUN_CFG"
                sed -i "s|pose_graph_save_path: \"~/output/pose_graph/\"|pose_graph_save_path: \"${out}/pose_graph/\"|" "$RUN_CFG"
                echo "=== ${env}_${level}_${method} trial ${i}/${N} (mode=${mode}) ==="
                killall -9 pht_vio_node 2>/dev/null || true
                sleep 1
                run_pht_vio_benchmark "$RUN_CFG" "$out" "$bag" 0 1.0
                python3 "${WS}/scripts/evaluate_trajectory.py" \
                    "${out}/vio.csv" "$gt" "${out}/eval" --no-plot \
                    --run-name "${env}_${level}_${method}_t${i}" || echo "[warn] eval failed ${env}_${level}_${method} t${i}"
            done
        done
    done
done

if [ "${SKIP_SUMMARY:-0}" != "1" ]; then
    for env in $ENVS; do
        python3 "${WS}/scripts/summarize_geodf_repeat.py" --root "$OUT_ROOT" \
            --env "$env" --levels "$LEVELS" --methods "$METHODS" || echo "[warn] summary ${env}"
    done
fi
echo "[weighted] done -> ${OUT_ROOT}"
