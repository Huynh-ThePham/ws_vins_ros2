# Paper #2 Proposed Method — GeoDF-Weighted

## Working title

GeoDF-Weighted: Uncertainty-Normalized Inertial Residual Weighting for Dynamic Feature Robustness in Stereo-Inertial VINS

## Core change

Paper #1 removes suspected dynamic features before state estimation. Paper #2
keeps the measurements and reduces their visual residual confidence in the
backend.

Selected implementation:

```text
KLT tracks
  -> active geometry scoring (feature-fit F, IMU F_imu, or derotation)
  -> uncertainty-normalized residual score
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

Default evaluated config:

```yaml
geodf_backend_weight: 1
geodf_backend_min_weight: 0.15
geodf_backend_weight_power: 2.0
geodf_hard_reject: 0
```

Projection factors multiply residuals and Jacobians by `sqrt(w)`, so the Ceres
cost contribution is weighted by `w`.

## Implemented files

- `src/config/viode/viode_stereo_imu_geodf_weighted_config.yaml`
- `src/config/euroc/euroc_stereo_imu_geodf_weighted_config.yaml`
- `scripts/run_geodf_weighted.sh`
- `scripts/run_geodf_weighted_n5.sh`
- `scripts/run_geodf_euroc_weighted.sh`
- `scripts/eval_viode_detection.py --prediction weight`

## Smoke result

Dataset: VIODE `parking_lot/3_high`, one run, SE(3) alignment.

| method | ATE RMSE | RPE RMSE |
|---|---:|---:|
| GeoDF-Weighted smoke | 0.0977 m | 0.0301 m |

Mask-level detection using `weight < 0.999`:

| precision | recall | lift | static FPR |
|---:|---:|---:|---:|
| 76.4% | 26.7% | 5.35x | 1.37% |

This is the selected Paper #2 direction because it improves the previous
parking-lot failure case while preserving all feature tracks for the estimator.

## Next evaluation

Run N=5:

```bash
FORCE=1 bash scripts/run_geodf_weighted.sh "0_none 1_low 2_mid 3_high" "city_day city_night parking_lot" 5
```

or equivalently:

```bash
FORCE=1 bash scripts/run_geodf_weighted_n5.sh 5
```

Run mask evaluation for weighted predictions:

```bash
python3 scripts/eval_viode_detection.py \
  --root results/viode \
  --mask-root results/viode/masks \
  --env parking_lot \
  --levels "3_high" \
  --method weighted \
  --prediction weight \
  --weight-threshold 0.999
```
