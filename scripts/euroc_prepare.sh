#!/usr/bin/env bash
# Extract EuRoC GT (mav0) from zip + convert ROS1 .bag -> ROS2 bag for Humble.
#
# Usage: ./scripts/euroc_prepare.sh [SEQUENCE ...]
#   default: MH_01_easy MH_02_easy MH_03_medium MH_04_difficult MH_05_difficult
# Env: EUROC_ROOT, WS (optional)
set -eo pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"
SEQS=("$@")
if [ ${#SEQS[@]} -eq 0 ]; then
    SEQS=(MH_01_easy MH_02_easy MH_03_medium MH_04_difficult MH_05_difficult)
fi

# shellcheck source=scripts/lib/geodf_common.sh
source "${WS}/scripts/lib/geodf_common.sh"

EUROC="$(resolve_euroc_root)" || {
    echo "EuRoC not found. Set EUROC_ROOT=/path/to/EuRoC" >&2
    exit 1
}

patch_ros2_metadata() {
    local dst="$1"
    python3 << PY
from pathlib import Path
meta = Path("${dst}") / "metadata.yaml"
if not meta.is_file():
    raise SystemExit(f"missing {meta}")
text = meta.read_text()
text = text.replace("offered_qos_profiles: []", 'offered_qos_profiles: ""')
lines, skip = [], False
for line in text.splitlines():
    if line.strip().startswith("type_description_hash:"):
        skip = True
        continue
    if skip:
        if line.startswith("  - message_count:") or line.startswith("  version:"):
            skip = False
        else:
            continue
    lines.append(line)
meta.write_text("\n".join(lines) + "\n")
print(f"[ok] patched {meta}")
PY
}

for seq in "${SEQS[@]}"; do
    group="$(euroc_group_for_seq "$seq")" || continue
    seq_dir="${EUROC}/${group}/${seq}"
    gt_csv="${seq_dir}/${seq}/mav0/state_groundtruth_estimate0/data.csv"
    ros1_bag="${seq_dir}/${seq}.bag"
    ros2_bag="$(resolve_euroc_ros2_bag "$seq" "$WS")"

    if [ ! -f "$gt_csv" ]; then
        zip="${seq_dir}/${seq}.zip"
        if [ ! -f "$zip" ]; then
            echo "[skip] missing GT zip: $zip" >&2
        else
            echo "[euroc] extract GT $seq"
            mkdir -p "${seq_dir}/${seq}"
            unzip -q -o "$zip" -d "${seq_dir}/${seq}"
        fi
    else
        echo "[have] GT $seq"
    fi

    if [ -d "$ros2_bag" ] && [ -f "${ros2_bag}/metadata.yaml" ]; then
        echo "[have] ROS2 bag $seq"
        continue
    fi
    if [ ! -f "$ros1_bag" ]; then
        echo "[skip] missing ROS1 bag: $ros1_bag" >&2
        continue
    fi

    echo "[euroc] convert $ros1_bag -> $ros2_bag"
    mkdir -p "$(dirname "$ros2_bag")"
    rm -rf "$ros2_bag"
    python3 -m rosbags.convert \
        --src "$ros1_bag" \
        --dst "$ros2_bag" \
        --src-typestore ros1_noetic \
        --dst-typestore ros2_humble \
        --include-topic /cam0/image_raw /cam1/image_raw /imu0
    patch_ros2_metadata "$ros2_bag"
done

echo "[euroc] prepare done"
