#!/usr/bin/env bash
# EuRoC static ablation: baseline vs always-on vs adaptive GeoDF (ROS 2).
set -eo pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"
SEQS="${SEQS:-MH_01_easy MH_02_easy MH_03_medium MH_04_difficult MH_05_difficult}"
METHODS="${METHODS:-baseline alwayson adaptive}"

for seq in $SEQS; do
    for m in $METHODS; do
        echo "==== EuRoC static: $seq / $m ===="
        bash "${WS}/scripts/run_geodf_euroc.sh" "$seq" "$m" "" --eval
    done
done

python3 "${WS}/scripts/summarize_euroc_static_ablation.py" --root "${WS}/results/geodf" --seqs "$SEQS"
