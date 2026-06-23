#!/usr/bin/env bash
# EuRoC static ablation: baseline vs geodf_hard vs adaptive (all MH sequences).
# Usage: ./scripts/run_geodf_study_ros2.sh
set -eo pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"
SEQS="${SEQS:-MH_01_easy MH_02_easy MH_03_medium MH_04_difficult MH_05_difficult}"
METHODS="${METHODS:-baseline geodf_hard adaptive}"

for seq in $SEQS; do
  for method in $METHODS; do
    echo "=== $seq / $method ==="
    bash "${WS}/scripts/run_geodf_euroc.sh" "$seq" "$method"
  done
done

python3 "${WS}/scripts/summarize_geodf_study.py" --root "${WS}/results/geodf" \
  --out "${WS}/results/geodf/geodf_summary_ros2.md"

echo "[study] summary -> ${WS}/results/geodf/geodf_summary_ros2.md"
