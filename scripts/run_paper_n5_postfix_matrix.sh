#!/usr/bin/env bash
# Paper-grade N=5 matrix AFTER Sem-GeoDF algorithm fix (bidirectional overlap +
# backend weights). Does NOT reuse pre-fix ATE tables.
#
# Steps:
#   1) rebuild estimator packages
#   2) re-train city_day sem_geodf stats (N=5)
#   3) re-select policy thresholds from train only
#   4) full VIODE+EuRoC N=5 matrix with selected params
#   5) summarize + sensitivity report
#
# Usage:
#   ./scripts/run_paper_n5_postfix_matrix.sh
#   SKIP_BUILD=1 ./scripts/run_paper_n5_postfix_matrix.sh
#   SKIP_TRAIN=1 SELECTED_PARAMS_FILE=... ./scripts/run_paper_n5_postfix_matrix.sh
set -eo pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"
cd "$WS"

TS="$(date +%Y%m%d_%H%M%S)"
LOG="${WS}/logs/paper_n5_postfix_${TS}.log"
mkdir -p "${WS}/logs"

TAG="${TAG:-paper_n5_postfix}"
TRAIN_TAG="${TRAIN_TAG:-paper_n5_postfix_train}"
OUT_DIR="${OUT_DIR:-results/sem_policy_tuning_postfix}"
SELECTED_PARAMS_FILE="${SELECTED_PARAMS_FILE:-${OUT_DIR}/selected_params.yaml}"
N="${N:-5}"
TRAIN_N="${TRAIN_N:-$N}"
YOLO_DEVICE="${YOLO_DEVICE:-cuda}"
FORCE="${FORCE:-0}"
SKIP_BUILD="${SKIP_BUILD:-0}"
SKIP_TRAIN="${SKIP_TRAIN:-0}"
SKIP_EUROC="${SKIP_EUROC:-0}"

exec > >(tee -a "$LOG") 2>&1
echo "[postfix-n5] start $(date -Is)"
echo "[postfix-n5] log=$LOG"
echo "[postfix-n5] TAG=$TAG TRAIN_TAG=$TRAIN_TAG OUT_DIR=$OUT_DIR N=$N TRAIN_N=$TRAIN_N"
echo "[postfix-n5] SELECTED_PARAMS_FILE=$SELECTED_PARAMS_FILE FORCE=$FORCE YOLO_DEVICE=$YOLO_DEVICE"
echo "[postfix-n5] git=$(git rev-parse --short HEAD) branch=$(git branch --show-current)"

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
echo "[postfix-n5] VIODE_ROOT=$VIODE_ROOT"
echo "[postfix-n5] EUROC_ROOT=$EUROC_ROOT"

if [ "$SKIP_BUILD" != "1" ]; then
  echo "[postfix-n5] === colcon build ==="
  colcon build --packages-select pht_vio pht_vio_ros yolo_dynamic_mask \
    --cmake-args -DCMAKE_BUILD_TYPE=Release
fi
# shellcheck disable=SC1091
source "${WS}/install/setup.bash"
export YOLO_DEVICE

if [ "$SKIP_TRAIN" != "1" ]; then
  echo "[postfix-n5] === train city_day (sem_geodf only) ==="
  TRAIN_N="$TRAIN_N" \
  TRAIN_TAG="$TRAIN_TAG" \
  HOLDOUT_TAG="$TAG" \
  OUT_DIR="$OUT_DIR" \
  FORCE_TRAIN="$FORCE" \
  ./scripts/run_sem_policy_protocol.sh train

  echo "[postfix-n5] === tune thresholds from train only ==="
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
  echo "[postfix-n5] selected params:"
  cat "$SELECTED_PARAMS_FILE"
else
  if [ ! -f "$SELECTED_PARAMS_FILE" ]; then
    echo "[error] SKIP_TRAIN=1 but missing $SELECTED_PARAMS_FILE" >&2
    exit 1
  fi
  echo "[postfix-n5] skip train/tune; using $SELECTED_PARAMS_FILE"
fi

echo "[postfix-n5] === full paper N=${N} matrix ==="
TAG="$TAG" \
N="$N" \
FORCE="$FORCE" \
SELECTED_PARAMS_FILE="$SELECTED_PARAMS_FILE" \
YOLO_DEVICE="$YOLO_DEVICE" \
./scripts/run_paper_n5_sem_policy_matrix.sh

# Optionally skip EuRoC inside matrix by editing env; matrix always runs EuRoC.
# If SKIP_EUROC=1 was requested, re-summarize VIODE-only is still fine.
if [ "$SKIP_EUROC" = "1" ]; then
  echo "[postfix-n5] note: matrix script always runs EuRoC; SKIP_EUROC only documented here"
fi

echo "[postfix-n5] === sensitivity / hold-out report ==="
TRAIN_TAG="$TRAIN_TAG" \
HOLDOUT_TAG="$TAG" \
OUT_DIR="$OUT_DIR" \
./scripts/run_sem_policy_protocol.sh report || true

ROOT="results/sem_geodf_ablation/${TAG}"
echo "[postfix-n5] done $(date -Is)"
echo "[postfix-n5] summary: ${ROOT}/PAPER_N5_SUMMARY.md"
echo "[postfix-n5] analysis: ${ROOT}/PAPER_N5_ANALYSIS.md"
echo "[postfix-n5] sensitivity: ${OUT_DIR}/SENSITIVITY_TABLE.md"
echo "[postfix-n5] log: $LOG"
