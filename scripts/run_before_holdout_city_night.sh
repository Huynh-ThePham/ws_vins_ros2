#!/usr/bin/env bash
# After the city_day BEFORE matrix finishes, run the hold-out env without
# overwriting the train BEFORE tree. Uses the same SHA binaries and protocol tag
# subdirectory.
set -euo pipefail
WS="$(cd "$(dirname "$0")/.." && pwd)"
PROTOCOL_TAG="${PROTOCOL_TAG:-before-ate-496c908}"
LOG="${WS}/logs/${PROTOCOL_TAG}-holdout-city_night.log"

# Wait until the main ablation for this tag is not running. pgrep patterns must
# exclude this script's own PID and its parents, otherwise the waiter matches its
# own command line and blocks forever.
wait_for_main_matrix() {
  local self=$$ ppid_self=$PPID pid
  while :; do
    local busy=0
    for pid in $(pgrep -f 'run_sem_geodf_ablation\.sh' 2>/dev/null || true); do
      [ "$pid" = "$self" ] && continue
      [ "$pid" = "$ppid_self" ] && continue
      busy=1
    done
    [ "$busy" = "0" ] && return 0
    sleep 60
  done
}
wait_for_main_matrix

echo "[holdout] starting city_night/3_high under ${PROTOCOL_TAG}" | tee -a "$LOG"
env PROTOCOL_TAG="${PROTOCOL_TAG}" \
    PROTOCOL_VERSION=sem-geodf-fair-v2 \
    CLAIM_MATRIX=adaptive_extension \
    N=3 \
    METHODS="baseline union_weight" \
    EUROC_SEQS="__none__" \
    SKIP_EUROC=1 \
    VIODE_ENV=city_night \
    VIODE_LEVELS="3_high" \
    PUBLICATION_MODE=0 \
    FORCE="${FORCE:-0}" \
    bash "${WS}/scripts/run_sem_geodf_ablation.sh" full \
    >>"$LOG" 2>&1

python3 "${WS}/scripts/summarize_before_ate.py" \
  --root "${WS}/results/sem_geodf_ablation/${PROTOCOL_TAG}" \
  --out "${WS}/results/sem_geodf_ablation/${PROTOCOL_TAG}/BEFORE_TABLE.md" \
  >>"$LOG" 2>&1 || true
echo "[holdout] done" | tee -a "$LOG"
