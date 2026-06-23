#!/usr/bin/env bash
# Quick GeoDF study on MH_01 (ROS 2).
set -eo pipefail
WS="$(cd "$(dirname "$0")/.." && pwd)"

bash "${WS}/scripts/run_geodf_euroc.sh" MH_01_easy baseline 40 --eval
bash "${WS}/scripts/run_geodf_euroc.sh" MH_01_easy geodf_hard 40 --eval
bash "${WS}/scripts/run_geodf_euroc.sh" MH_01_easy geodf_noguard 40 --eval

STUDY="${WS}/results/geodf_study"
mkdir -p "$STUDY"
for tag in baseline geodf_hard geodf_noguard; do
    src="${WS}/results/geodf/MH_01_easy_${tag}_s40"
    dst="${STUDY}/MH_01_easy_${tag}"
    [ -d "$src" ] && ln -sfn "$src" "$dst"
done

python3 "${WS}/scripts/summarize_geodf_study.py" --root "$STUDY"
echo "[geodf_study] -> ${STUDY}/geodf_summary.md"
