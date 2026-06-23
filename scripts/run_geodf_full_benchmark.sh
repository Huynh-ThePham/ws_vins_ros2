#!/usr/bin/env bash
# Full GeoDF paper benchmark on EuRoC static (ROS 2).
# Usage: ./scripts/run_geodf_full_benchmark.sh [static|ablation|all]
set -eo pipefail

PHASE="${1:-all}"
WS="$(cd "$(dirname "$0")/.." && pwd)"
OUT_ROOT="${WS}/results/geodf_study"
mkdir -p "$OUT_ROOT"

STATIC_SEQS=(MH_01_easy MH_02_easy MH_03_medium MH_04_difficult MH_05_difficult)

run_to_study() {
    local seq="$1" method="$2"
    bash "${WS}/scripts/run_geodf_euroc.sh" "$seq" "$method" "" --eval
    src=$(ls -td "${WS}/results/geodf/${seq}_${method}_s"* 2>/dev/null | head -1)
    dst="${OUT_ROOT}/${seq}_${method}"
    [ -n "$src" ] && rm -rf "$dst" && cp -a "$src" "$dst"
}

phase_static() {
    for seq in "${STATIC_SEQS[@]}"; do
        run_to_study "$seq" baseline
        run_to_study "$seq" geodf_hard
    done
}

phase_ablation() {
    for seq in MH_01_easy MH_03_medium; do
        run_to_study "$seq" baseline
        run_to_study "$seq" geodf_noguard
        run_to_study "$seq" geodf_hard
    done
}

case "$PHASE" in
    static) phase_static ;;
    ablation) phase_ablation ;;
    all) phase_static; phase_ablation ;;
    *) echo "Unknown PHASE=$PHASE"; exit 1 ;;
esac

python3 "${WS}/scripts/summarize_geodf_study.py" --root "$OUT_ROOT"
python3 "${WS}/scripts/summarize_geodf_filter_impact.py" \
    --static-root "${WS}/results/geodf_static_repeat" \
    --viode-root "${WS}/results/viode" 2>/dev/null || true
echo "[geodf] phase=$PHASE -> ${OUT_ROOT}/geodf_summary.md"
