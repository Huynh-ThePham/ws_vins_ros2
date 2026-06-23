#!/usr/bin/env bash
# Static repeatability: baseline vs GeoDF-Hard on MH_01..MH_05 (ROS 2).
# Usage: ./scripts/run_geodf_static_repeat.sh [REPEATS]
set -eo pipefail

REPEATS="${1:-2}"
WS="$(cd "$(dirname "$0")/.." && pwd)"
OUT_ROOT="${WS}/results/geodf_static_repeat"
mkdir -p "$OUT_ROOT"

STATIC_SEQS=(MH_01_easy MH_02_easy MH_03_medium MH_04_difficult MH_05_difficult)

for seq in "${STATIC_SEQS[@]}"; do
    for method in baseline geodf_hard; do
        for r in $(seq 1 "$REPEATS"); do
            name="${seq}_${method}_run${r}"
            out="${OUT_ROOT}/${name}"
            if [ "${FORCE:-0}" != "1" ] && [ -f "${out}/eval/metrics.json" ]; then
                echo "[skip] $name"
                continue
            fi
            echo "=== $name ==="
            bash "${WS}/scripts/run_geodf_euroc.sh" "$seq" "$method" "" --eval
            src="${WS}/results/geodf/${seq}_${method}_s"*
            latest=$(ls -td ${src} 2>/dev/null | head -1)
            if [ -n "$latest" ] && [ -d "$latest" ]; then
                rm -rf "$out"
                cp -a "$latest" "$out"
            fi
        done
    done
done

python3 "${WS}/scripts/summarize_geodf_static_repeat.py" --root "$OUT_ROOT" --repeats "$REPEATS"
python3 "${WS}/scripts/summarize_geodf_filter_impact.py" --static-root "$OUT_ROOT"
echo "[static_repeat] -> ${OUT_ROOT}/static_repeat_summary.md"
