# Proposed Method — GeoDF-Weighted

## Working title

GeoDF-Weighted: Uncertainty-Normalized Inertial Residual Weighting for Dynamic Feature Robustness in Stereo-Inertial VINS

## Core change

GeoDF-Adaptive removes suspected dynamic features before state estimation.
GeoDF-Weighted keeps the measurements and reduces their visual residual
confidence in the backend.

Selected implementation:

```text
KLT tracks
  -> active geometry scoring (feature-fit F, IMU F_imu, or derotation)
  -> uncertainty-normalized residual score
  -> temporal dynamic-belief update per track
  -> per-feature backend weight w in [w_min, 1]
  -> projection residuals scaled by sqrt(w)
  -> VINS backend optimizes all features
```

No feature is hard-deleted when `geodf_hard_reject: 0` and
`geodf_backend_weight: 1`.

## Weight rule

For each scored feature on an active frame:

```text
r = residual / tau_eff
s = max(0, r - 1)
w = max(w_min, 1 / (1 + s^p))
```

The upgraded weighted branch adds a track-level temporal reliability memory:

```text
p_dyn = 1 - 1 / (1 + s^p)
b_t = alpha * p_dyn + (1 - alpha) * b_{t-1}
alpha = attack if p_dyn > b_{t-1}, else recovery
w = max(w_min, 1 - b_t)
```

This makes the backend robust to one-frame KLT/epipolar spikes while still
down-weighting persistently dynamic tracks. It is a stronger paper contribution
than frame-wise weighting because it explicitly models track reliability over
time, matching the sliding-window estimator's temporal structure.

Default evaluated config:

```yaml
geodf_backend_weight: 1
geodf_backend_min_weight: 0.15
geodf_backend_weight_power: 2.0
geodf_backend_temporal: 1
geodf_backend_temporal_attack: 0.45
geodf_backend_temporal_recovery: 0.12
geodf_backend_temporal_prior: 0.0
geodf_hard_reject: 0
```

Projection factors multiply residuals and Jacobians by `sqrt(w)`, so the Ceres
cost contribution is weighted by `w`.

## Implemented files

- `src/config/viode/viode_stereo_imu_geodf_weighted_config.yaml`
- `src/config/euroc/euroc_stereo_imu_geodf_weighted_config.yaml`
- `scripts/run_viode_n5_prepare.sh` / `scripts/run_viode_n5.sh` — VIODE N-repeat orchestrator
- `scripts/run_euroc_n3_prepare.sh` / `scripts/run_euroc_n3.sh` — EuRoC static-safety orchestrator
- `scripts/run_geodf_weighted.sh` / `scripts/run_geodf_weighted_n5.sh` — low-level VIODE runner (+ shortcut)
- `scripts/run_geodf_euroc_weighted.sh` — low-level EuRoC runner
- `scripts/postprocess_paper_artifacts.sh` — regenerate all tables/figures
- `scripts/summarize_n5_final.py`, `scripts/stats_tests.py`, `scripts/verify_paper_data.py`
- `scripts/run_viode_detection_prepare.sh` + `scripts/eval_viode_detection.py --prediction weight`

## Smoke result

Dataset: VIODE `parking_lot/3_high`, one run, SE(3) alignment.

| method | ATE RMSE | RPE RMSE |
|---|---:|---:|
| GeoDF-Weighted smoke | 0.0977 m | 0.0301 m |

Mask-level detection using `weight < 0.999`:

| precision | recall | lift | static FPR |
|---:|---:|---:|---:|
| 76.4% | 26.7% | 5.35x | 1.37% |

This direction improves the parking-lot failure case seen with hard rejection
while preserving all feature tracks for the estimator.

## Full N-repeat evaluation (paper numbers)

Same workflow as GeoDF-Adaptive paper #1: prepare → run → postprocess.

```bash
source scripts/setup_ws.bash
export VIODE_ROOT=/path/to/viode
export EUROC_ROOT=/path/to/EuRoC

bash scripts/run_viode_n5_prepare.sh 5
bash scripts/run_euroc_n3_prepare.sh 3
FORCE=1 bash scripts/run_viode_n5.sh 5
FORCE=1 bash scripts/run_euroc_n3.sh 3
bash scripts/run_viode_detection_prepare.sh   # optional: Table detection
bash scripts/postprocess_paper_artifacts.sh     # PAPER_RESULTS_N5, STATS_TESTS, figures
```

Artifacts land in `results/geodf_evaluation/` (see `results/geodf_evaluation/README.md`).

Detection eval (weight < 0.999 marks dynamic candidate):

```bash
bash scripts/run_viode_detection_prepare.sh
# or single-cell:
python3 scripts/eval_viode_detection.py \
  --features results/viode_detection/parking_lot_3_high_weighted/geo_df_features.csv \
  --mask-dir results/viode_detection/parking_lot_3_high_weighted/masks \
  --prediction weight --weight-threshold 0.999
```

**Branch:** `paper/geodf-weighted-vins-2026` · **Worktree:** `../ws_vins_ros2_paper2_weight` · **Baseline:** `baseline/ros2-stereo-vi-slam-euroc-v1` · See [docs/BRANCHING.md](BRANCHING.md).
