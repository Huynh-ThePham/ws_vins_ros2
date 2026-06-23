#!/usr/bin/env bash
# Master GeoDF benchmark: EuRoC static + VIODE dynamic (ROS 2).
# Usage: ./scripts/run_geodf_benchmark_all.sh [euroc|viode|all]
set -eo pipefail

PHASE="${1:-all}"
WS="$(cd "$(dirname "$0")/.." && pwd)"

case "$PHASE" in
    euroc)
        bash "${WS}/scripts/run_euroc_static_ablation.sh"
        bash "${WS}/scripts/run_geodf_full_benchmark.sh" static
        ;;
    viode)
        bash "${WS}/scripts/run_geodf_viode.sh"
        ;;
    all)
        bash "${WS}/scripts/run_geodf_full_benchmark.sh" all
        bash "${WS}/scripts/run_euroc_static_ablation.sh"
        bash "${WS}/scripts/run_geodf_viode.sh" "0_none 1_low 2_mid 3_high" "baseline geodf_dump adaptive"
        ;;
    *)
        echo "Usage: $0 [euroc|viode|all]"; exit 1
        ;;
esac

echo "[benchmark] complete. Summaries:"
echo "  ${WS}/results/geodf_study/geodf_summary.md"
echo "  ${WS}/results/geodf/euroc_static_ablation.md"
echo "  ${WS}/results/viode/viode_city_day_adaptive.md"
