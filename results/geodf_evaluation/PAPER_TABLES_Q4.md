# GeoDF-VINS Q4 Paper Tables and Figures

This file is the paper-facing artifact index. Use `PAPER_RESULTS_N5.md` as the
primary trajectory source and `DETECTION_EVAL_VIODE.md` as the primary dynamic
feature detection source.

## Main Claim

GeoDF-Adaptive is a geometry-only front-end dynamic feature rejection module for
stereo-inertial VINS. It improves trajectory accuracy in moderate-dynamic scenes,
preserves static-scene safety on EuRoC, and exposes a clear limitation under
high dynamic density.

## Table 1: VIODE Trajectory Summary

Source: `results/geodf_evaluation/PAPER_RESULTS_N5.md`

- Trials: N=5 for VIODE baseline/adaptive.
- Methods: baseline VINS-Fusion vs PROPOSED GeoDF-Adaptive.
- Metrics: ATE RMSE and RPE RMSE in metres.
- Verdict: 7 wins, 5 losses, 0 neutral under a +/-3% band.
- Strong positive cases: `city_day/3_high` +24.5%, `city_night/0_none` +41.3%.
- Limitation cases: `parking_lot/2_mid` -36.4%, `parking_lot/3_high` -44.3%.

## Table 2: EuRoC Static Safety

Source: `results/geodf_evaluation/PAPER_RESULTS_N5.md`

- Sequences: MH_01_easy through MH_05_difficult.
- PROPOSED improves all five static sequences by +2.0% to +6.2%.
- This supports the safety claim that scene-aware gating does not degrade static
  stereo-inertial odometry in the tested EuRoC sequences.

## Table 3: Detection Against VIODE GT Masks

Source: `results/geodf_evaluation/DETECTION_EVAL_VIODE.md`

- Ground truth: VIODE segmentation IDs `vehicle_dynamic_0..10`.
- Timestamp match: nearest mask within 30 ms, 100% matched.
- `city_day` lift: 31.72x, 12.14x, 8.33x for low/mid/high dynamic levels.
- Static false-positive rate on `city_day`: 0.59% to 0.71%.
- `parking_lot` lift drops to 1.42x to 1.48x, explaining the trajectory failure
  under high dynamic density.

## Figures

- `figures/viode_ate_delta_n5.svg`: VIODE ATE improvement bar chart.
- `figures/viode_detection_lift.svg`: VIODE dynamic-feature detection lift chart.

## Do Not Use As Main Paper Artifacts

The hybrid, soft-weight, and YOLO experiments are exploratory and are excluded
from this artifact set. They should only appear as future work if discussed at
all.
