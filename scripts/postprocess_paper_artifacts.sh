#!/usr/bin/env bash
# Regenerate ALL paper artifacts after VIODE N=5 + EuRoC N=3 (+ optional detection).
#
# Produces, under results/geodf_evaluation/:
#   PAPER_RESULTS_N5.md + .json       (Table 2: VIODE ATE/RPE mean±std)
#   EUROC_REPEAT_N3.md  + .json       (Table 3: EuRoC static safety)
#   STATS_TESTS.md      + .json       (significance, effect size, CI, variance)
#   RUNTIME_TABLE.md    + .json       (Table 5: per-frame cost, armed %)
#   DETECTION_EVAL_VIODE.md           (Table 4: detection lift/FPR, if dumps exist)
#   verify_report.json                (trial integrity)
#   figures/viode_ate_delta_n5_gray.* (Fig 2, data-driven)
#   figures/viode_detection_lift_gray.* (Fig 3, data-driven)
#   figures/viode_trajectories_gray.* (qualitative overlays)
#
# Usage: ./scripts/postprocess_paper_artifacts.sh
set -eo pipefail

WS="$(cd "$(dirname "$0")/.." && pwd)"
cd "$WS"
mkdir -p results/geodf_evaluation/figures

echo "[post] 0/9 integrity check"
python3 scripts/verify_paper_data.py \
    --out-json results/geodf_evaluation/verify_report.json || \
    echo "[post][WARN] verifier reported problems — inspect verify_report.json"

echo "[post] 1/9 per-adaptive-trial filter metrics"
find results/viode_repeat results/euroc_repeat -path '*_adaptive/trial_*' -type d 2>/dev/null | while read -r d; do
    [ -f "$d/geo_df_stats.csv" ] && python3 scripts/geodf_filter_metrics.py --run-dir "$d" >/dev/null 2>&1 || true
done

echo "[post] 2/9 VIODE N=5 summary (Table 2)"
python3 scripts/summarize_n5_final.py --viode-only \
    --out results/geodf_evaluation/PAPER_RESULTS_N5.md \
    --out-json results/geodf_evaluation/paper_results_n5.json >/dev/null || echo "[WARN] viode summary"

echo "[post] 3/9 EuRoC N=3 summary (Table 3)"
python3 scripts/summarize_euroc_repeat.py --n 3 \
    --out results/geodf_evaluation/EUROC_REPEAT_N3.md \
    --out-json results/geodf_evaluation/euroc_repeat_n3.json >/dev/null || echo "[WARN] euroc summary"

echo "[post] 4/9 statistical significance (STATS_TESTS)"
python3 scripts/stats_tests.py >/dev/null || echo "[WARN] stats tests"

echo "[post] 5/9 runtime table (Table 5) + resource table"
python3 scripts/summarize_runtime_from_repeat.py --root results/viode_repeat >/dev/null || echo "[WARN] runtime"
python3 scripts/summarize_resources.py >/dev/null || echo "[WARN] resource summary"

echo "[post] 6/9 detection eval (Table 4, if dumps exist)"
if ls results/viode_detection/*_adaptive_dump/detection_eval.json results/viode_detection/*_geodf_dump/detection_eval.json >/dev/null 2>&1; then
    python3 scripts/summarize_detection_table.py
else
    echo "[post] no detection dumps — run scripts/run_viode_detection_prepare.sh for Table 4/Fig 3"
fi

echo "[post] 7/9 result figures (Fig 2/3, data-driven)"
python3 scripts/make_result_figures.py 2>/dev/null || echo "[WARN] result figures"

echo "[post] 8/9 trajectory overlays (qualitative)"
python3 scripts/make_trajectory_figures.py --root results/viode_repeat 2>/dev/null || echo "[WARN] trajectory figures"

echo "[post] 9/9 manifest progress"
python3 scripts/viode_n5_manifest.py update --root results/viode_repeat --n 5 >/dev/null 2>&1 || true
python3 scripts/euroc_n3_manifest.py update --root results/euroc_repeat --n 3 >/dev/null 2>&1 || true

echo "[post] done — artifacts in results/geodf_evaluation/"
ls -1 results/geodf_evaluation/*.md results/geodf_evaluation/*.json 2>/dev/null | sed 's|^|  |'
