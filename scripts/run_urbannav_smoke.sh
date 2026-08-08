#!/usr/bin/env bash
# UrbanNav stereo-IMU smoke: prepare short ROS2 bag, run baseline VIO, optional ATE.
#
# Usage:
#   ./scripts/run_urbannav_smoke.sh [medium|deep|harsh]
#
# Env:
#   MAX_SEC=60          bag trim length (default 60)
#   BAG_RATE=1.0
#   EVAL=1              run evaluate_trajectory.py when GT is present
#   METHOD=baseline     baseline|geodf|semantic|union_noweight|union_weight
#   FORCE=1             re-convert bag
set -eo pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=scripts/lib/geodf_common.sh
source "${WS}/scripts/lib/geodf_common.sh"
# shellcheck source=scripts/lib/sad_common.sh
source "${WS}/scripts/lib/sad_common.sh"

ALIAS="${1:-medium}"
MAX_SEC="${MAX_SEC:-60}"
BAG_RATE="${BAG_RATE:-1.0}"
METHOD="${METHOD:-baseline}"
EVAL="${EVAL:-1}"

canon="$(urbannav_alias_canonical "$ALIAS")" || {
    echo "Usage: $0 [medium|deep|harsh]" >&2
    exit 2
}

ROOT="$(resolve_urbannav_root)" || {
    echo "[fatal] UrbanNav not found. Expected data/UrbanNav -> /media/theph/Data1/Research/dataset/UrbanNav" >&2
    exit 1
}
echo "[urbannav] root=$ROOT seq=$canon max_sec=$MAX_SEC method=$METHOD"

FORCE_FLAG=()
[ "${FORCE:-0}" = "1" ] && FORCE_FLAG=(--force)
bash "${WS}/scripts/urbannav_prepare.sh" "$canon" --max-sec "$MAX_SEC" "${FORCE_FLAG[@]}"

BAG="$(resolve_urbannav_ros2_bag "$canon" "$WS")_s${MAX_SEC}"
GT="$(resolve_urbannav_gt "$canon" "$WS")"
if [ ! -f "${BAG}/metadata.yaml" ]; then
    echo "[fatal] missing prepared bag: $BAG" >&2
    exit 1
fi

OUT="${WS}/results/urbannav/${canon}_${METHOD}_s${MAX_SEC}"
mkdir -p "$OUT"

CFG_SRC="${WS}/src/config/urbannav/urbannav_stereo_imu_config.yaml"
if [ "$METHOD" != "baseline" ]; then
    OVERLAY="${WS}/src/config/paper/overlays/${METHOD}.yaml"
    if [ ! -f "$OVERLAY" ]; then
        echo "[fatal] unknown method overlay: $METHOD ($OVERLAY)" >&2
        exit 1
    fi
    python3 "${WS}/scripts/generate_paper_config.py" \
        --base "${WS}/src/config/paper/urbannav_common.yaml" \
        --overlay "$OVERLAY" \
        --out "${OUT}/resolved_config.yaml" \
        --set "output_path=\"${OUT}/\"" \
        --set "pose_graph_save_path=\"${OUT}/pose_graph/\""
    # Camera calib files must sit next to the resolved config.
    cp -a "${WS}/src/config/urbannav/cam0_pinhole.yaml" \
          "${WS}/src/config/urbannav/cam1_pinhole.yaml" "$OUT/"
    RUN_CFG="${OUT}/resolved_config.yaml"
else
    # Writable copy so output_path points at this run.
    python3 - "$CFG_SRC" "${OUT}/run_config.yaml" "$OUT" <<'PY'
import re, sys
src, dst, out = sys.argv[1], sys.argv[2], sys.argv[3]
text = open(src).read()
text = re.sub(r'(?m)^output_path:.*$', f'output_path: "{out}/"', text)
text = re.sub(r'(?m)^pose_graph_save_path:.*$',
              f'pose_graph_save_path: "{out}/pose_graph/"', text)
open(dst, "w").write(text)
PY
    cp -a "${WS}/src/config/urbannav/cam0_pinhole.yaml" \
          "${WS}/src/config/urbannav/cam1_pinhole.yaml" "$OUT/"
    RUN_CFG="${OUT}/run_config.yaml"
fi

source_ros2_ws "$WS"
USE_YOLO=0
case "$METHOD" in
    semantic|union_noweight|union_weight|full_adaptive|adaptive_arbitration) USE_YOLO=1 ;;
esac

echo "=== UrbanNav $canon $METHOD bag=$BAG out=$OUT ==="
if [ "$USE_YOLO" = "1" ]; then
    run_sad_vio_benchmark "$RUN_CFG" "$OUT" "$BAG" 0 "$BAG_RATE" 1 || {
        echo "[warn] benchmark returned non-zero" >&2
    }
else
    run_pht_vio_benchmark "$RUN_CFG" "$OUT" "$BAG" 0 "$BAG_RATE" || {
        echo "[warn] benchmark returned non-zero" >&2
    }
fi

if [ "$EVAL" = "1" ] && [ -f "$GT" ] && [ -f "${OUT}/vio.csv" ]; then
    python3 "${WS}/scripts/evaluate_trajectory.py" \
        "${OUT}/vio.csv" "$GT" "${OUT}/eval" --run-name "$(basename "$OUT")" || true
fi

echo "[done] $OUT"
