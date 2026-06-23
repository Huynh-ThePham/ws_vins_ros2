#!/usr/bin/env bash
# Convert a VIODE ROS1 bag to ROS2 bag (cam+imu topics) for ros2 bag play on Humble.
#
# Usage: ./scripts/viode_prepare_ros2_bag.sh ROS1_BAG [ROS2_BAG_DIR]
set -eo pipefail

SRC="${1:?ROS1 .bag path required}"
DST="${2:-$(dirname "$SRC")/ros2_bag}"

if [ -d "$DST" ] && [ -f "$DST/metadata.yaml" ]; then
    echo "[skip] existing ROS2 bag: $DST"
    exit 0
fi

if [ -d "$DST" ]; then
    rm -rf "$DST"
fi

echo "[viode] convert $SRC -> $DST"
python3 -m rosbags.convert \
    --src "$SRC" \
    --dst "$DST" \
    --src-typestore ros1_noetic \
    --dst-typestore ros2_humble \
    --include-topic /cam0/image_raw /cam1/image_raw /imu0

# Humble ros2 bag play expects offered_qos_profiles: "" (not []) and no type_description_hash.
python3 << PY
from pathlib import Path
meta = Path("${DST}") / "metadata.yaml"
if meta.is_file():
    text = meta.read_text()
    text = text.replace("offered_qos_profiles: []", 'offered_qos_profiles: ""')
    lines = []
    skip = False
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
    print("[ok] patched metadata.yaml for ROS2 Humble")
PY

echo "[ok] ROS2 bag ready: $DST"
