#!/usr/bin/env bash
# Populate data/viode_gt/{env}/{level}/gt_odometry.csv from prior runs or ROS1 bags.
set -eo pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"
GT_ROOT="${WS}/data/viode_gt"
SRC_ROOT="${VIODE_GT_SRC:-/home/theph/ws_vins_ros2/results/viode}"

mkdir -p "$GT_ROOT"

copy_gt() {
    local env="$1" level="$2" src="$3"
    local dst="${GT_ROOT}/${env}/${level}/gt_odometry.csv"
    if [ -f "$dst" ]; then
        return 0
    fi
    if [ -f "$src" ]; then
        mkdir -p "$(dirname "$dst")"
        cp -a "$src" "$dst"
        echo "[gt-cache] $env/$level <- $src"
        return 0
    fi
    return 1
}

for env in city_day city_night parking_lot; do
    for level in 0_none 1_low 2_mid 3_high; do
        key="${env}_${level}"
        found=0
        for src in \
            "${SRC_ROOT}/${key}_adaptive/gt_odometry.csv" \
            "${SRC_ROOT}/${key}_baseline/gt_odometry.csv" \
            "${SRC_ROOT}/${key}_adaptive_v2/gt_odometry.csv" \
            "${SRC_ROOT}/${key}_geodf_dump/gt_odometry.csv"; do
            if copy_gt "$env" "$level" "$src"; then
                found=1
                break
            fi
        done
        if [ "$found" = "0" ]; then
            bag="${VIODE_ROOT:-/media/theph/Data1/Research/Datasets/Viode}/${env}/${level}.bag"
            dst="${GT_ROOT}/${env}/${level}/gt_odometry.csv"
            if [ -f "$bag" ]; then
                mkdir -p "$(dirname "$dst")"
                python3 "${WS}/scripts/viode_dump_gt.py" --bag "$bag" --out "$dst"
                echo "[gt-cache] $env/$level from bag"
            else
                echo "[gt-cache] MISSING $env/$level (no cached GT, no bag)" >&2
            fi
        fi
    done
done

echo "[gt-cache] done -> ${GT_ROOT}"
