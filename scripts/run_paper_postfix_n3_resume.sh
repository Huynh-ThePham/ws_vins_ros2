#!/usr/bin/env bash
# Resume paper_postfix from N=1 gate to N=3 (skips completed t1).
set -eo pipefail
WS="$(cd "$(dirname "$0")/.." && pwd)"
cd "$WS"
exec env SKIP_BUILD=1 SKIP_TRAIN=1 N=3 TRAIN_N=3 YOLO_DEVICE=cuda FORCE=0 \
  METHODS="${METHODS:-baseline adaptive sad_sem sem_geodf}" \
  TAG=paper_postfix TRAIN_TAG=paper_postfix_train \
  OUT_DIR=results/sem_policy_tuning_postfix \
  SELECTED_PARAMS_FILE=results/sem_policy_tuning_postfix/selected_params.yaml \
  VIODE_ROOT="${VIODE_ROOT:-/media/theph/Data1/ws_research_datasets/Datasets/Viode}" \
  EUROC_ROOT="${EUROC_ROOT:-/home/theph/ws_vins_ros2/data/euroc_benchmark}" \
  ./scripts/run_paper_n5_postfix_matrix.sh
