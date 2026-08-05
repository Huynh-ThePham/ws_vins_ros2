#!/usr/bin/env bash
# Paper matrix AFTER Sem-GeoDF algorithm fix (bidirectional overlap + backend weights).
#
# Recommended gate workflow (avoid burning full repeats on a bad build):
#   1) N=1 full sweep across all scenes/methods
#        N=1 TRAIN_N=1 ./scripts/run_paper_n5_postfix_matrix.sh
#   2) Inspect PAPER_N1_SUMMARY; only continue if claims look sane
#   3) Finish remaining trials to N=3 (skips completed t1 when FORCE=0)
#        N=3 SKIP_TRAIN=1 SKIP_BUILD=1 FORCE=0 ./scripts/run_paper_n5_postfix_matrix.sh
#
# Steps:
#   1) rebuild estimator packages (unless SKIP_BUILD=1)
#   2) re-train city_day sem_geodf stats (TRAIN_N)
#   3) re-select policy thresholds from train only
#   4) full VIODE+EuRoC matrix with selected params (N)
#   5) summarize + sensitivity report
set -eo pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"
cd "$WS"

TS="$(date +%Y%m%d_%H%M%S)"
N="${N:-1}"
TRAIN_N="${TRAIN_N:-$N}"
# Stable tag so N=1 gate + later N=5 resume share the same result tree.
TAG="${TAG:-paper_postfix}"
TRAIN_TAG="${TRAIN_TAG:-${TAG}_train}"
OUT_DIR="${OUT_DIR:-results/sem_policy_tuning_postfix}"
SELECTED_PARAMS_FILE="${SELECTED_PARAMS_FILE:-${OUT_DIR}/selected_params.yaml}"
YOLO_DEVICE="${YOLO_DEVICE:-cuda}"
FORCE="${FORCE:-0}"
SKIP_BUILD="${SKIP_BUILD:-0}"
SKIP_TRAIN="${SKIP_TRAIN:-0}"
SKIP_EUROC="${SKIP_EUROC:-0}"
# Paper default methods; override e.g. METHODS="baseline adaptive sad_sem sequential sem_geodf sem_geodf_mask_gated"
METHODS="${METHODS:-baseline adaptive sad_sem sem_geodf}"
HOLDOUT_METHODS="${HOLDOUT_METHODS:-$METHODS}"

LOG="${WS}/logs/${TAG}_${TS}.log"
mkdir -p "${WS}/logs"

exec > >(tee -a "$LOG") 2>&1
echo "[postfix] start $(date -Is)"
echo "[postfix] log=$LOG"
echo "[postfix] TAG=$TAG TRAIN_TAG=$TRAIN_TAG OUT_DIR=$OUT_DIR N=$N TRAIN_N=$TRAIN_N"
echo "[postfix] SELECTED_PARAMS_FILE=$SELECTED_PARAMS_FILE FORCE=$FORCE YOLO_DEVICE=$YOLO_DEVICE"
echo "[postfix] METHODS=$METHODS"
echo "[postfix] git=$(git rev-parse --short HEAD) branch=$(git branch --show-current)"

source /opt/ros/humble/setup.bash

# Dataset roots (current host layout). Prefer complete GT trees.
export VIODE_ROOT="${VIODE_ROOT:-/media/theph/Data1/ws_research_datasets/Datasets/Viode}"
export EUROC_ROOT="${EUROC_ROOT:-/home/theph/ws_vins_ros2/data/euroc_benchmark}"
if [ ! -d "${VIODE_ROOT}/city_day" ]; then
  echo "[error] VIODE_ROOT missing city_day: $VIODE_ROOT" >&2
  exit 1
fi
if [ ! -d "${EUROC_ROOT}/machine_hall" ]; then
  EUROC_ROOT="/media/theph/Data1/ws_research_datasets/Datasets/EuRoC"
  export EUROC_ROOT
fi
if [ ! -d "${EUROC_ROOT}/machine_hall" ]; then
  echo "[error] EUROC_ROOT missing machine_hall: $EUROC_ROOT" >&2
  exit 1
fi
echo "[postfix] VIODE_ROOT=$VIODE_ROOT"
echo "[postfix] EUROC_ROOT=$EUROC_ROOT"

if [ "$SKIP_BUILD" != "1" ]; then
  echo "[postfix] === colcon build ==="
  colcon build --packages-select pht_vio pht_vio_ros yolo_dynamic_mask \
    --cmake-args -DCMAKE_BUILD_TYPE=Release
fi
# shellcheck disable=SC1091
source "${WS}/install/setup.bash"
export YOLO_DEVICE

if [ "$SKIP_TRAIN" != "1" ]; then
  echo "[postfix] === train city_day (sem_geodf only, N=${TRAIN_N}) ==="
  TRAIN_N="$TRAIN_N" \
  TRAIN_TAG="$TRAIN_TAG" \
  HOLDOUT_TAG="$TAG" \
  OUT_DIR="$OUT_DIR" \
  FORCE_TRAIN="$FORCE" \
  ./scripts/run_sem_policy_protocol.sh train

  echo "[postfix] === tune thresholds from train only ==="
  TRAIN_TAG="$TRAIN_TAG" \
  HOLDOUT_TAG="$TAG" \
  OUT_DIR="$OUT_DIR" \
  SELECTED_PARAMS_FILE="$SELECTED_PARAMS_FILE" \
  ./scripts/run_sem_policy_protocol.sh tune

  if [ ! -f "$SELECTED_PARAMS_FILE" ]; then
    echo "[error] selected params missing after tune: $SELECTED_PARAMS_FILE" >&2
    exit 1
  fi
  if grep -q "status=draft_incomplete_train" "$SELECTED_PARAMS_FILE"; then
    echo "[error] selected params still draft/incomplete: $SELECTED_PARAMS_FILE" >&2
    exit 1
  fi
  echo "[postfix] selected params:"
  cat "$SELECTED_PARAMS_FILE"
else
  if [ ! -f "$SELECTED_PARAMS_FILE" ]; then
    echo "[error] SKIP_TRAIN=1 but missing $SELECTED_PARAMS_FILE" >&2
    exit 1
  fi
  echo "[postfix] skip train/tune; using $SELECTED_PARAMS_FILE"
fi

echo "[postfix] === paper matrix N=${N} methods=${METHODS} ==="
TAG="$TAG" \
N="$N" \
FORCE="$FORCE" \
METHODS="$METHODS" \
SELECTED_PARAMS_FILE="$SELECTED_PARAMS_FILE" \
YOLO_DEVICE="$YOLO_DEVICE" \
./scripts/run_paper_n5_sem_policy_matrix.sh

if [ "$SKIP_EUROC" = "1" ]; then
  echo "[postfix] note: matrix script always runs EuRoC; SKIP_EUROC only documented here"
fi

echo "[postfix] === sensitivity / hold-out report ==="
TRAIN_TAG="$TRAIN_TAG" \
HOLDOUT_TAG="$TAG" \
OUT_DIR="$OUT_DIR" \
./scripts/run_sem_policy_protocol.sh report || true

ROOT="results/sem_geodf_ablation/${TAG}"
# Friendly alias for the gate (N=1) summary.
if [ "$N" = "1" ]; then
  if [ -f "${ROOT}/PAPER_N5_SUMMARY.md" ]; then
    cp -a "${ROOT}/PAPER_N5_SUMMARY.md" "${ROOT}/PAPER_N1_SUMMARY.md"
  fi
  if [ -f "${ROOT}/PAPER_N5_ANALYSIS.md" ]; then
    cp -a "${ROOT}/PAPER_N5_ANALYSIS.md" "${ROOT}/PAPER_N1_ANALYSIS.md"
  fi
fi

echo "[postfix] done $(date -Is)"
echo "[postfix] summary: ${ROOT}/PAPER_N5_SUMMARY.md"
if [ "$N" = "1" ]; then
  echo "[postfix] gate summary: ${ROOT}/PAPER_N1_SUMMARY.md"
  echo "[postfix] NEXT: inspect gate results; if good, finish to N=3 with:"
  echo "  N=3 SKIP_TRAIN=1 SKIP_BUILD=1 FORCE=0 ./scripts/run_paper_n5_postfix_matrix.sh"
  echo "  (same TAG=${TAG}; FORCE=0 skips completed t1 and only runs t2..t3)"
fi
echo "[postfix] analysis: ${ROOT}/PAPER_N5_ANALYSIS.md"
echo "[postfix] sensitivity: ${OUT_DIR}/SENSITIVITY_TABLE.md"
echo "[postfix] log: $LOG"
