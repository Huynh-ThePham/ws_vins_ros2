#!/usr/bin/env bash
# Prepare UrbanNav for ROS 2 VIO: convert cam+imu topics + dump GT.
#
# Usage:
#   ./scripts/urbannav_prepare.sh [medium|deep|harsh|all] [--max-sec N] [--force]
#
# Env:
#   URBANNAV_ROOT   dataset root (default: resolve via scripts/lib)
#   WS              workspace root
set -eo pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=scripts/lib/geodf_common.sh
source "${WS}/scripts/lib/geodf_common.sh"
# shellcheck source=scripts/lib/sad_common.sh
source "${WS}/scripts/lib/sad_common.sh"

SEQS=()
MAX_SEC=""
FORCE=0
while [ $# -gt 0 ]; do
    case "$1" in
        medium|deep|harsh|all) SEQS+=("$1"); shift ;;
        --max-sec) MAX_SEC="${2:?}"; shift 2 ;;
        --force) FORCE=1; shift ;;
        -h|--help)
            sed -n '2,12p' "$0"
            exit 0
            ;;
        *)
            echo "Unknown arg: $1" >&2
            exit 2
            ;;
    esac
done
if [ ${#SEQS[@]} -eq 0 ]; then
    SEQS=(medium)
fi
if [ "${SEQS[0]}" = "all" ]; then
    SEQS=(medium deep harsh)
fi

ROOT="$(resolve_urbannav_root)" || {
    echo "[fatal] UrbanNav root not found. Set URBANNAV_ROOT=/media/theph/Data1/Research/dataset/UrbanNav" >&2
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

for alias in "${SEQS[@]}"; do
    seq="$(urbannav_seq_dir "$alias")" || {
        echo "[fatal] unknown sequence alias: $alias" >&2
        exit 1
    }
    bag_ros1="$(urbannav_ros1_bag "$alias" "$ROOT")"
    if [ ! -f "$bag_ros1" ]; then
        echo "[fatal] missing ROS1 bag: $bag_ros1" >&2
        exit 1
    fi

    gt_out="$(resolve_urbannav_gt "$alias" "$WS")"
    mkdir -p "$(dirname "$gt_out")"
    python3 "${WS}/scripts/urbannav_dump_gt.py" \
        --root "$ROOT" --seq "$alias" --out "$gt_out"

    bag_ros2="$(resolve_urbannav_ros2_bag "$alias" "$WS")"
    if [ -n "$MAX_SEC" ]; then
        bag_ros2="${bag_ros2}_s${MAX_SEC}"
    fi

    if [ "$FORCE" != "1" ] && [ -d "$bag_ros2" ] && [ -f "${bag_ros2}/metadata.yaml" ]; then
        echo "[have] ROS2 bag $alias -> $bag_ros2"
        continue
    fi

    echo "[urbannav] convert $alias: $bag_ros1 -> $bag_ros2"
    mkdir -p "$(dirname "$bag_ros2")"
    rm -rf "$bag_ros2"

    convert_args=(--src "$bag_ros1" --dst "$bag_ros2")
    if [ -n "$MAX_SEC" ]; then
        convert_args+=(--max-sec "$MAX_SEC")
    fi
    python3 "${WS}/scripts/urbannav_convert_bag.py" "${convert_args[@]}"
    patch_ros2_metadata "$bag_ros2"
    echo "[ok] ROS2 bag ready: $bag_ros2"
done
