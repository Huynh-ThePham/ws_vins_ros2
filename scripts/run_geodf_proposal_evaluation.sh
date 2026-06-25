#!/usr/bin/env bash
# Full proposal evaluation: EuRoC static + VIODE (3 envs) + detection + reports.
#
# Usage: ./scripts/run_geodf_proposal_evaluation.sh
# Env:
#   EUROC_ROOT=/media/theph/Data1/Research/Datasets/EuRoC
#   VIODE_ROOT=/media/theph/Data1/Research/Datasets/Viode
#   FORCE=1   re-run even if metrics.json exists
set -eo pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"
export EUROC_ROOT="${EUROC_ROOT:-/media/theph/Data1/Research/Datasets/EuRoC}"
export VIODE_ROOT="${VIODE_ROOT:-/media/theph/Data1/Research/Datasets/Viode}"
export FORCE="${FORCE:-1}"

LOG="${WS}/logs/proposal_evaluation_$(date +%Y%m%d_%H%M%S).log"
mkdir -p "${WS}/logs" "${WS}/results/geodf_evaluation"
exec > >(tee -a "$LOG") 2>&1

echo "========== GeoDF Proposal Full Evaluation =========="
echo "EUROC_ROOT=$EUROC_ROOT"
echo "VIODE_ROOT=$VIODE_ROOT"
echo "FORCE=$FORCE"
echo "LOG=$LOG"

# shellcheck source=scripts/lib/geodf_common.sh
source "${WS}/scripts/lib/geodf_common.sh"
source_ros2_ws "$WS"

EUROC_SEQS=(MH_01_easy MH_02_easy MH_03_medium MH_04_difficult MH_05_difficult)
EUROC_METHODS=(baseline alwayson adaptive)
VIODE_ENVS=(city_day city_night parking_lot)
VIODE_LEVELS="0_none 1_low 2_mid 3_high"
VIODE_METHODS="baseline geodf_dump adaptive"

echo "========== PHASE 0: Prepare EuRoC (GT + ROS2 bags) =========="
bash "${WS}/scripts/euroc_prepare.sh" "${EUROC_SEQS[@]}"

echo "========== PHASE 1: EuRoC static ablation (5 seq × 3 methods) =========="
for seq in "${EUROC_SEQS[@]}"; do
    for method in "${EUROC_METHODS[@]}"; do
        echo "--- EuRoC $seq / $method ---"
        killall -9 pht_vio_node 2>/dev/null || true
        sleep 1
        bash "${WS}/scripts/run_geodf_euroc.sh" "$seq" "$method" "" --eval || \
            echo "[WARN] failed EuRoC $seq $method" >> "${WS}/results/geodf_evaluation/failures.log"
    done
done
python3 "${WS}/scripts/summarize_euroc_static_ablation.py" \
    --root "${WS}/results/geodf" --seqs "${EUROC_SEQS[*]}"

echo "========== PHASE 2: VIODE dynamic (3 envs × 4 levels × 3 methods) =========="
for env in "${VIODE_ENVS[@]}"; do
    echo "=== VIODE env: $env ==="
    export VIODE_ENV="$env"
    killall -9 pht_vio_node 2>/dev/null || true
    sleep 1
    bash "${WS}/scripts/run_geodf_viode.sh" "$VIODE_LEVELS" "$VIODE_METHODS" || \
        echo "[WARN] failed VIODE $env" >> "${WS}/results/geodf_evaluation/failures.log"
done

echo "========== PHASE 3: VIODE detection eval (GT segmentation) =========="
for env in "${VIODE_ENVS[@]}"; do
    export VIODE_ENV="$env"
    bash "${WS}/scripts/run_viode_detection_eval.sh" "$VIODE_LEVELS" || \
        echo "[WARN] detection eval $env" >> "${WS}/results/geodf_evaluation/failures.log"
done

echo "========== PHASE 4: Summaries & reports =========="
python3 "${WS}/scripts/summarize_geodf_evaluation.py" \
    --ws "$WS" \
    --out "${WS}/results/geodf_evaluation/EVALUATION_REPORT.md" \
    --json "${WS}/results/geodf_evaluation/evaluation_summary.json"

python3 "${WS}/scripts/summarize_geodf_multienv.py" \
    --root "${WS}/results/viode" \
    --out "${WS}/results/geodf_evaluation/MULTIENV_REPORT.md" \
    --json "${WS}/results/geodf_evaluation/multienv_summary.json" 2>/dev/null || true

echo "========== DONE =========="
echo "Reports:"
echo "  ${WS}/results/geodf/euroc_static_ablation.md"
echo "  ${WS}/results/geodf_evaluation/EVALUATION_REPORT.md"
echo "  ${WS}/results/geodf_evaluation/MULTIENV_REPORT.md"
echo "  ${WS}/results/viode/viode_{city_day,city_night,parking_lot}_{adaptive,detection}.md"
echo "LOG=$LOG"
