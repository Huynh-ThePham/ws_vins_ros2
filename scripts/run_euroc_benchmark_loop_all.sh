#!/usr/bin/env bash
# Run loop-closure benchmark on all EuRoC Machine Hall sequences.
set -eo pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"
RESULTS="${WS}/results/euroc_loop_post_refactor_summary.json"
mkdir -p "${WS}/results"

declare -A STARTS=(
  [MH_01_easy]=40
  [MH_02_easy]=35
  [MH_03_medium]=17.5
  [MH_04_difficult]=15
  [MH_05_difficult]=15
)

echo "[" > "$RESULTS"
first=1
for SEQ in MH_01_easy MH_02_easy MH_03_medium MH_04_difficult MH_05_difficult; do
  echo "[batch] $SEQ loop start=${STARTS[$SEQ]}s"
  if ! bash "${WS}/scripts/run_euroc_benchmark.sh" "$SEQ" stereo_imu "${STARTS[$SEQ]}" 1; then
    echo "FAILED: $SEQ" >&2
    exit 1
  fi
  START_TAG="${STARTS[$SEQ]//./p}"
  OUT="${WS}/results/${SEQ}_stereo_imu_loop_s${START_TAG}"
  METRICS="${OUT}/eval/metrics.json"
  if [ ! -f "$METRICS" ]; then
    echo "Missing metrics: $METRICS" >&2
    exit 1
  fi
  [ "$first" -eq 1 ] || echo "," >> "$RESULTS"
  first=0
  python3 - "$SEQ" "$METRICS" "$OUT/vio_loop.csv" <<'PY' >> "$RESULTS"
import json, sys
seq, metrics_path, traj_path = sys.argv[1:4]
with open(metrics_path) as f:
    m = json.load(f)
poses = sum(1 for _ in open(traj_path)) if traj_path else 0
print(json.dumps({"sequence": seq, "ate_m": m.get("ate_rmse_m"), "poses": poses}))
PY
  killall -9 pht_vio_node pht_loop_closure_node 2>/dev/null || true
  sleep 2
done
echo "]" >> "$RESULTS"
echo "[done] summary -> $RESULTS"
