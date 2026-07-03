#!/usr/bin/env bash
# Quick progress snapshot for the full AECE paper benchmark.
set -eo pipefail
WS="$(cd "$(dirname "$0")/.." && pwd)"
cd "$WS"

METHODS=(baseline alwayson adaptive_fixed adaptive_no_quality adaptive_no_vote adaptive)
ENVS=(city_day city_night parking_lot)
LEVELS=(0_none 1_low 2_mid 3_high)
VIODE_N=5
EUROC_N=3
EUROC_SEQS=(MH_01_easy MH_02_easy MH_03_medium MH_04_difficult MH_05_difficult)

viode_done=0 viode_target=$(( ${#ENVS[@]} * ${#LEVELS[@]} * ${#METHODS[@]} * VIODE_N ))
for env in "${ENVS[@]}"; do
  for level in "${LEVELS[@]}"; do
    for m in "${METHODS[@]}"; do
      for i in $(seq 1 "$VIODE_N"); do
        [ -f "results/viode_repeat/${env}_${level}_${m}/trial_${i}/eval/metrics.json" ] && viode_done=$((viode_done+1))
      done
    done
  done
done

euroc_done=0 euroc_target=$(( ${#EUROC_SEQS[@]} * ${#METHODS[@]} * EUROC_N ))
for seq in "${EUROC_SEQS[@]}"; do
  for m in "${METHODS[@]}"; do
    for i in $(seq 1 "$EUROC_N"); do
      [ -f "results/euroc_repeat/${seq}_${m}/trial_${i}/eval/metrics.json" ] && euroc_done=$((euroc_done+1))
    done
  done
done

det_done=0
for env in "${ENVS[@]}"; do
  for level in "${LEVELS[@]}"; do
    [ -f "results/viode_detection/${env}_${level}_geodf_dump/geo_df_features.csv" ] && det_done=$((det_done+1))
  done
done

LOG="$(ls -t logs/paper_benchmark_resume_*.log logs/paper_benchmark_full_*.log logs/euroc_n3_*.log 2>/dev/null | head -1)"
CUR="—"
if [ -n "$LOG" ]; then
  CUR="$(grep -E '^=== |^\[paper-bench\] === PHASE|^\[resume\]' "$LOG" | tail -1 | sed 's/^=== //;s/^\[paper-bench\] //;s/^\[resume\] //')"
fi

RUNNING=""
pgrep -f 'run_paper_benchmark|run_viode_n5|run_euroc_n3|pht_vio_node' >/dev/null 2>&1 && RUNNING="YES" || RUNNING="NO"

echo "=== Paper benchmark status $(date '+%Y-%m-%d %H:%M:%S') ==="
echo "Running: $RUNNING"
echo "Current: $CUR"
echo ""
pct_viode=$(( viode_done * 1000 / viode_target ))
pct_euroc=$(( euroc_done * 1000 / euroc_target ))
printf "Phase 1 VIODE N=5 : %3d / %3d  (%d.%d%%)\n" "$viode_done" "$viode_target" $((pct_viode/10)) $((pct_viode%10))
printf "Phase 2 EuRoC N=3 : %3d / %3d  (%d.%d%%)\n" "$euroc_done" "$euroc_target" $((pct_euroc/10)) $((pct_euroc%10))
printf "Phase 3 Detection : %2d / 12\n" "$det_done"
echo ""
if [ -f results/geodf_evaluation/PAPER_RESULTS_N5.md ]; then
  echo "Artifacts: results/geodf_evaluation/ (postprocess done)"
else
  echo "Artifacts: pending (postprocess after Phase 1–3)"
fi
[ -n "$LOG" ] && echo "Log: $LOG"
