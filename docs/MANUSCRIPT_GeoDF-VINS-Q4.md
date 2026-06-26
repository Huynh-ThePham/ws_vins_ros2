# GeoDF-Adaptive VINS: Manuscript Skeleton for Scopus Q4

## Working Title

GeoDF-Adaptive: Geometry-Only Dynamic Feature Rejection for Robust
Stereo-Inertial Visual Odometry

## Abstract Draft

Dynamic objects violate the static-scene assumption used by visual-inertial
odometry front-ends. This paper presents GeoDF-Adaptive, a lightweight
geometry-only dynamic feature rejection module for stereo-inertial VINS. The
method uses temporal epipolar consistency, Sampson residual gating, scene-aware
activation, auto-calibrated activation thresholding, and track-level temporal
voting. Experiments on the original EuRoC and VIODE datasets show that the
proposed method preserves static-scene accuracy on EuRoC and improves trajectory
accuracy in several moderate-dynamic VIODE scenes. Detection evaluation against
VIODE ground-truth segmentation masks confirms that rejected features are
substantially more likely to lie on moving vehicles than random feature samples.
The method is not universal: high dynamic-density parking-lot scenes remain a
failure case because the fundamental matrix estimate can be contaminated by
large moving regions. These results support GeoDF-Adaptive as a reproducible,
low-overhead geometry baseline for dynamic-scene VINS.

## Contributions

1. A geometry-only dynamic feature rejection front-end for stereo-inertial VINS
   based on temporal epipolar consistency and Sampson residual dual gating.
2. A scene-aware activation mechanism with auto-calibrated `rho_on`, reducing
   unnecessary rejection on static or high-noise scenes.
3. Track-level temporal voting to suppress transient two-frame false positives.
4. A reproducible evaluation on original EuRoC and VIODE data, including
   trajectory metrics and feature-level detection metrics against VIODE dynamic
   segmentation masks.
5. An explicit limitation analysis for high dynamic-density scenes.

## Method Summary

The front-end first tracks features temporally in the left camera. For tracked
features with sufficient age, it estimates a temporal fundamental matrix using
RANSAC and scores each feature with a Sampson residual. A feature becomes a
dynamic candidate only when it is both a RANSAC outlier and exceeds the Sampson
threshold. The scene-aware gate arms the rejection module only when the smoothed
outlier signal exceeds an auto-calibrated threshold derived from the per-scene
outlier floor. Temporal voting then requires a feature to be flagged dynamic on
multiple consecutive frames before hard deletion.

## Experimental Setup

- Static safety dataset: EuRoC MH_01_easy to MH_05_difficult.
- Dynamic dataset: VIODE `city_day`, `city_night`, and `parking_lot`, each at
  `0_none`, `1_low`, `2_mid`, and `3_high` dynamic levels.
- Trajectory metrics: ATE RMSE and RPE RMSE.
- Detection metrics: precision, recall, precision lift, static FPR, and
  RANSAC dynamic/static separation against VIODE segmentation masks.
- Main trajectory table: `results/geodf_evaluation/PAPER_RESULTS_N5.md`.
- Main detection table: `results/geodf_evaluation/DETECTION_EVAL_VIODE.md`.

## Results To Report

- VIODE trajectory verdict: 7 wins, 5 losses, 0 neutral under a +/-3% band.
- Best dynamic gains: `city_day/3_high` +24.5% ATE improvement and
  `city_night/0_none` +41.3%.
- EuRoC static safety: all five sequences improve by +2.0% to +6.2%.
- Feature-level detection: `city_day` dynamic precision lift is 8.33x to 31.72x,
  with static FPR below 1%.
- Limitation: `parking_lot/2_mid` and `parking_lot/3_high` degrade by -36.4% and
  -44.3%, while detection lift drops to approximately 1.4x.

## Figure Plan

- Figure 1: GeoDF-Adaptive pipeline diagram.
- Figure 2: VIODE ATE improvement bar chart
  (`results/geodf_evaluation/figures/viode_ate_delta_n5.svg`).
- Figure 3: Detection lift against VIODE GT masks
  (`results/geodf_evaluation/figures/viode_detection_lift.svg`).
- Figure 4: Limitation plot contrasting `city_day` and `parking_lot` dynamic
  density and detection lift.

## Discussion And Limitations

GeoDF-Adaptive should be claimed as conditionally effective, not universally
superior. It works best when moving objects produce clear epipolar inconsistency
but do not dominate the scene geometry. The `parking_lot` results show that when
large moving regions contaminate the fundamental matrix estimate, hard rejection
can remove useful static structure and degrade trajectory accuracy. Future work
should prioritize scene-adaptive hard/soft rejection, IMU-compensated epipolar
scoring, and more robust fundamental matrix estimation under high dynamic
density.

## Submission Framing

Target a Scopus Q4 applied robotics, autonomous systems, or applied computer
vision venue. Avoid claims of universal state-of-the-art trajectory improvement.
Emphasize reproducibility, geometry-only design, static safety, feature-level
detection validation, and honest failure analysis.
