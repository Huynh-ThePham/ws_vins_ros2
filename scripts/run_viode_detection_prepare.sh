#!/usr/bin/env bash
# One-time geodf_dump runs (12 env×level) for feature-level detection eval (Table 4 / Fig. 3).
#
# Prerequisites: run_viode_n5_prepare.sh
# Usage: ./scripts/run_viode_detection_prepare.sh
# Env: VIODE_ROOT, FORCE=1
set -eo pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"
export VIODE_ROOT="${VIODE_ROOT:-/media/theph/Data1/Research/Datasets/Viode}"
export FORCE="${FORCE:-0}"
OUT="${WS}/results/viode_detection"
CFG="${WS}/src/config/viode"
VIODE_ENVS=(city_day city_night parking_lot)
LEVELS="0_none 1_low 2_mid 3_high"

# shellcheck source=scripts/lib/geodf_common.sh
source "${WS}/scripts/lib/geodf_common.sh"
VIODE="$(resolve_viode_root)" || exit 1
source_ros2_ws "$WS"
VIODE_CFG="$(ros2 pkg prefix pht_vio_ros)/share/pht_vio_ros/config/viode"
mkdir -p "$OUT" "${WS}/results/geodf_evaluation"

for env in "${VIODE_ENVS[@]}"; do
    for level in $LEVELS; do
        bag_ros1="${VIODE}/${env}/${level}.bag"
        bag_ros2="$(resolve_viode_ros2_bag "$bag_ros1" "$WS")"
        run="${env}_${level}_geodf_dump"
        out="${OUT}/${run}"
        [ -f "$bag_ros1" ] || { echo "[skip] $bag_ros1"; continue; }

        if [ "$FORCE" != "1" ] && [ -f "${out}/geo_df_features.csv" ]; then
            echo "[have] features $run"
        else
            mode="$(geodf_method_to_mode geodf_dump)"
            run_cfg="${VIODE_CFG}/viode_${mode}_config_det_${level}.yaml"
            mkdir -p "$out"
            cp "${VIODE_CFG}/viode_${mode}_config.yaml" "$run_cfg"
            sed -i "s|output_path: \"~/output/\"|output_path: \"${out}/\"|" "$run_cfg"
            sed -i "s|pose_graph_save_path: \"~/output/pose_graph/\"|pose_graph_save_path: \"${out}/pose_graph/\"|" "$run_cfg"
            echo "=== detection dump $run ==="
            killall -9 pht_vio_node 2>/dev/null || true
            sleep 1
            run_pht_vio_benchmark "$run_cfg" "$out" "$bag_ros2" 0 1.0
        fi

        if [ -f "${out}/geo_df_features.csv" ]; then
            mask_dir="${out}/masks"
            # Prefer a per-environment id list if provided; VIODE shares a fixed
            # segmentation palette across environments, so city_day is the default.
            ids="${CFG}/vehicle_ids_${env}.txt"
            [ -f "$ids" ] || ids="${CFG}/vehicle_ids_city_day.txt"
            [ -d "$mask_dir" ] && [ "$FORCE" != "1" ] || python3 "${WS}/scripts/viode_make_masks.py" \
                --bag "$bag_ros1" --out-dir "$mask_dir" \
                --rgb-ids "${CFG}/rgb_ids.txt" \
                --vehicle-ids "$ids"
            python3 "${WS}/scripts/eval_viode_detection.py" \
                --features "${out}/geo_df_features.csv" \
                --mask-dir "$mask_dir" \
                --out "${out}/detection_eval.json"
        fi
    done
done

python3 "${WS}/scripts/eval_viode_detection.py" \
    --root "$OUT" \
    --mask-root "$OUT" \
    --env city_day \
    --levels "$LEVELS" \
    --out-md "${WS}/results/geodf_evaluation/DETECTION_EVAL_VIODE.md" 2>/dev/null || \
python3 << 'PY'
import glob, json, os
from pathlib import Path
root = Path("results/viode_detection")
out = Path("results/geodf_evaluation/DETECTION_EVAL_VIODE.md")
rows = []
for p in sorted(root.glob("*_geodf_dump/detection_eval.json")):
    d = json.loads(p.read_text())
    env, level = p.parent.name.replace("_geodf_dump", "").rsplit("_", 1)
    rows.append((env, level, d))
lines = ["# VIODE detection eval (geodf_dump, 1 run/cell)", "",
         "| env | level | precision | recall | lift | static-FPR |",
         "|---|---|---:|---:|---:|---:|"]
for env, level, d in rows:
    lift = d.get("precision_lift", "n/a")
    lift_s = f"{lift:.2f}x" if isinstance(lift, (int, float)) else str(lift)
    lines.append(
        f"| {env} | {level} | {d.get('precision', 'n/a')} | {d.get('recall', 'n/a')} | "
        f"{lift_s} | {d.get('static_fpr', 'n/a')} |"
    )
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text("\n".join(lines) + "\n")
print(f"[ok] wrote {out} ({len(rows)} cells)")
PY

echo "[detection-prepare] done -> $OUT"
