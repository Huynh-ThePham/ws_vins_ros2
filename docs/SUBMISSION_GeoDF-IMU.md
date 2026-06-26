# Paper #2 — GeoDF-Inertial Submission Notes

Branch: `paper/geodf-imu-dynamic-2026-q4`
Prior work: Paper #1 frozen on `paper/geodf-adaptive-vins-2026-q4` (commit `c646740`).

## Status

| Item | State |
|---|---|
| Core implementation (`rejectGeoDynamic` IMU modes) | done |
| Estimator plumbing (`pushImuEpipolarAtImage`) | done |
| VIODE + EuRoC inertial configs | done |
| Benchmark scripts | done |
| N=5 VIODE evaluation | running (`scripts/run_geodf_inertial_n5.sh`) |
| Manuscript draft | `docs/MANUSCRIPT_GeoDF-IMU.md` |
| Proposal | `docs/PROPOSAL_GeoDF-IMU.md` |

## Headline result (smoke, parking_lot/3_high trial 1)

| Method | ATE RMSE (m) | vs baseline |
|---|---:|---:|
| Baseline | 0.119 | — |
| GeoDF-Adaptive (P1) | 0.172 | **−44%** (regression) |
| GeoDF-Inertial (P2) | 0.108 | **+9%** |

Paper #1's parking_lot failure is reversed by inertial epipolar scoring.

## Reproduce

```bash
source /opt/ros/humble/setup.bash
cd ws_vins_ros2 && colcon build --packages-select pht_vio pht_vio_ros

# Single condition
bash scripts/run_geodf_inertial.sh "3_high" "parking_lot" 5

# Full VIODE grid N=5
bash scripts/run_geodf_inertial_n5.sh 5

# Summarize (needs baseline/adaptive from Paper #1 runs)
python3 scripts/summarize_inertial_n5.py
```

EuRoC static safety (requires `EUROC_ROOT` with GT CSVs):

```bash
export EUROC_ROOT=/path/to/EuRoC
bash scripts/run_geodf_euroc_inertial.sh 5
```

## Config

- VIODE: `src/config/viode/viode_stereo_imu_geodf_inertial_config.yaml`
- EuRoC: `src/config/euroc/euroc_stereo_imu_geodf_inertial_config.yaml`

Key IMU parameters: `geodf_imu_enable`, `geodf_imu_sampson_th`, `geodf_imu_parallax_min/ref`,
`geodf_imu_fallback`, `geodf_imu_derotate`, `geodf_imu_max_dyn_frac`.

## Claims for Paper #2

**Make:** IMU-predicted epipolar geometry fixes high dynamic-density failure of
feature-fit GeoDF; training-free front-end; fallback to Paper #1 when IMU unreliable.

**Avoid:** SOTA dynamic-VIO; perfect calibration claims; universal improvement.

## Relationship to Paper #1

- Paper #1 = geometry-only, submittable standalone.
- Paper #2 = adds inertial rigidity reference; cites P1 as baseline/fallback.
- Publish incrementally on separate timelines.
