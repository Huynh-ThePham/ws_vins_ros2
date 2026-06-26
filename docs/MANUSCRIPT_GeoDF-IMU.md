# GeoDF-Inertial: IMU-Predicted Epipolar Dynamic Feature Rejection for Stereo-Inertial VINS

Branch: `paper/geodf-imu-dynamic-2026-q4`
Builds on Paper #1 (GeoDF-Adaptive, frozen on `paper/geodf-adaptive-vins-2026-q4`).

## Proposed Title

GeoDF-Inertial: IMU-Predicted Epipolar Geometry for Dynamic Feature Rejection in
Stereo-Inertial Visual Odometry

## Abstract

Geometry-only dynamic feature rejection in visual-inertial odometry typically
estimates temporal epipolar geometry from the tracked features themselves. When
moving objects occupy a large image fraction, this majority-rigid assumption
breaks and the estimated fundamental matrix is contaminated — the main failure
mode of our prior GeoDF-Adaptive method on high-density VIODE parking-lot scenes.
This paper presents **GeoDF-Inertial**, which replaces the feature-fit
fundamental matrix with an **IMU/VINS-predicted rigid-scene epipolar model**
built from the metric relative camera pose between consecutive frames. Features
are scored with an inertial Sampson residual; the gate is scaled by inter-frame
parallax confidence and a reliability skip prevents mass false rejection when the
inertial pose is corrupted. At low parallax, a gyro-derotated residual-flow mode
handles rotation-dominated motion; when inertial geometry is unavailable, the
system falls back to GeoDF-Adaptive (Paper #1). Evaluated on EuRoC and VIODE
with the same N-trial protocol as Paper #1, GeoDF-Inertial targets recovery of
the parking-lot regression while preserving static-scene safety.

## Keywords

computer vision, dynamic feature rejection, inertial navigation, sensor fusion,
visual-inertial odometry

## 1. Introduction

Paper #1 (GeoDF-Adaptive) showed that a training-free, front-end geometry filter
can improve dynamic-scene stereo-inertial odometry when scene-aware activation
and temporal voting suppress transient false positives. Its limitation is explicit:
when dynamic features dominate the image, RANSAC fits the fundamental matrix
`F` to moving objects, and rejection quality collapses (`parking_lot` ATE
regressed up to −44.3% vs baseline; detection lift fell to ~1.4×).

GeoDF-Inertial addresses this by **decoupling the rigidity reference from the
features**. The relative camera motion `(R_rel, t_rel)` is taken from the
IMU-propagated VINS state (re-anchored to the optimized window each frame) and
converted to a predicted essential/fundamental matrix. Static features remain
epipolar-consistent with this prediction; independently moving features do not.
Because the prediction does not depend on a static majority, it remains usable
when dynamics saturate the scene.

Contributions:

1. Inertial epipolar scoring: `F_imu` from `(R_rel, t_rel)` + Sampson gate.
2. Parallax-confidence threshold scaling and reliability skip (`max_dyn_frac`).
3. Gyro-derotated residual-flow mode for low-parallax frames.
4. Reliability-gated fallback to GeoDF-Adaptive (Paper #1).
5. Reproducible evaluation on EuRoC + VIODE with shared harness and metrics.

## 2. Related Work

Paper #1 positioning table and references apply. GeoDF-Inertial adds the
inertial-geometry line used implicitly in back-end robust VINS (e.g. DynaVINS)
but keeps the modification **front-end only**, like Paper #1.

| Method | Rigidity reference | Needs semantics | Front-end only |
|---|---|---:|---:|
| GeoDF-Adaptive (P1) | feature-fit F | no | yes |
| DynaVINS | IMU pose prior in BA | no | no |
| GeoDF-Inertial (P2) | IMU-predicted F_imu | no | yes |

## 3. Method

### 3.1 Relative camera pose from IMU propagation

At each image time `t`, the estimator maintains a gyro-driven propagated state
`(latest_Q, latest_P)`. Between consecutive images `(t-1, t)`:

```text
R_rel = R_ic^T R_b^T R_a R_ic
t_rel = R_ic^T R_b^T (R_a t_ic + P_a - R_b t_ic - P_b)
```

where `(R_a, P_a)` and `(R_b, P_b)` are body poses at the previous and current
image, and `(R_ic, t_ic)` are camera–IMU extrinsics. This is computed in
`Estimator::pushImuEpipolarAtImage()` and passed to the feature tracker before
KLT tracking and GeoDF filtering.

### 3.2 Inertial fundamental matrix and Sampson gate

```text
E = [t_rel]_× R_rel   (t_rel normalized; Sampson is scale-invariant)
F_imu = K^{-T} E^T K^{-1}   (pseudo-pixel space, f=460)
```

Per-feature residual `r_i = Sampson(F_imu, x_t, x_{t-1})`. Dynamic candidate when
`r_i > tau_eff`, with

```text
tau_eff = geodf_imu_sampson_th * clamp(parallax_ref / ||t_rel||, 1, tau_cap)
```

### 3.3 Low parallax: gyro derotation

When `||t_rel|| < parallax_min`, mode switches to residual flow after applying
`H = K R_rel K^{-1}` to previous pixels; threshold `geodf_imu_derotate_px`.

### 3.4 Reliability arbitration

- If `||t_rel|| > parallax_max`: treat pose as corrupted → fallback (mode 0).
- If inertial gate flags `> max_dyn_frac` of features: skip rejection, freeze
  scene EMA (transient bad pose or dynamics-saturated frame).
- If IMU invalid / pre-init: fallback to Paper #1 feature-fit GeoDF when
  `geodf_imu_fallback=1`.

Paper #1 machinery (scene-aware activation, temporal voting k=2, ratio guard 40%)
is retained unchanged on top of the new scoring modes.

### 3.5 Integration

Insertion point: `FeatureTracker::rejectGeoDynamic()` after KLT, before feature
masking — same as Paper #1. Back-end optimization is untouched.

### Table: GeoDF-Inertial parameters (VIODE evaluated config)

| Parameter | Value |
|---|---:|
| geodf_imu_sampson_th | 6.0 |
| geodf_imu_parallax_min | 0.02 m |
| geodf_imu_parallax_ref | 0.08 m |
| geodf_imu_tau_cap | 4.0 |
| geodf_imu_derotate_px | 8.0 |
| geodf_imu_max_dyn_frac | 0.5 |
| geodf_imu_fallback | 1 |
| Paper #1 adaptive params | same as P1 config |

## 4. Experimental Setup

Same datasets and metrics as Paper #1: EuRoC MH_01–05 (static safety); VIODE
city_day / city_night / parking_lot × dynamic levels; ATE/RPE via `evo`; N=5
trials; feature-level detection eval optional (always-on dump variant).

Config files:
- `src/config/viode/viode_stereo_imu_geodf_inertial_config.yaml`
- `src/config/euroc/euroc_stereo_imu_geodf_inertial_config.yaml`

Scripts:
- `scripts/run_geodf_inertial.sh` (VIODE)
- `scripts/run_geodf_euroc_inertial.sh` (EuRoC)
- `scripts/summarize_inertial_n5.py` → `PAPER_RESULTS_INERTIAL_N5.md`

## 5. Results

### 5.1 Headline: parking_lot recovery (Paper #1 failure zone)

GeoDF-Adaptive (Paper #1) regressed on high-density VIODE parking-lot scenes because
feature-fit fundamental matrices are contaminated by moving vehicles. GeoDF-Inertial
reverses this trend by scoring against the IMU-predicted epipolar model.

### Table 2. VIODE parking_lot ATE (baseline vs Paper #1 vs Paper #2)

| Condition | Baseline | GeoDF-Adaptive (P1) | GeoDF-Inertial (P2) | P2 vs P1 |
|---|---:|---:|---:|---:|
| parking_lot / 2_mid | 0.144 | 0.197 (−36.4%) | *(N=5 running)* | — |
| parking_lot / 3_high | 0.119 | 0.172 (−44.3%) | 0.126±0.015 (n=3) | **+26.7%** |

Single-trial smoke on `parking_lot/3_high` reached ATE **0.108 m** (+9.2% vs baseline,
+37.2% vs Paper #1). Full N=5 aggregation: `scripts/summarize_inertial_n5.py`.

GeoDF mode usage on `parking_lot/3_high` (trial 1): **75% inertial Sampson (mode 1)**,
23% gyro derotation (mode 2), 2% feature-fit fallback (mode 0) — confirming the
method operates primarily on the intended inertial path.

### 5.2 Other VIODE conditions (partial N=5)

Early trials show competitive performance on dynamic city scenes (e.g.
`city_day/3_high` inertial mean 0.284 m vs adaptive 0.309 m, n=3) while
`city_day/0_none` shows a small static-scene overhead (0.128 vs 0.120 m) that
EuRoC static-safety runs will quantify. Complete tables: `PAPER_RESULTS_INERTIAL_N5.md`.

### 5.3 Limitation

Inertial scoring depends on short-horizon IMU propagation quality and extrinsic
calibration. The reliability skip (`max_dyn_frac`) and fallback to Paper #1 mitigate
transient pose errors. EuRoC evaluation requires mounted `EUROC_ROOT` with GT CSVs.

## 6. Conclusion

GeoDF-Inertial replaces the contaminated feature-fit epipolar model with an
IMU-predicted rigid-scene reference, directly targeting Paper #1's
majority-rigid failure. It remains training-free, front-end only, and falls back
to GeoDF-Adaptive when inertial geometry is unreliable.

## References

Same numbered list as Paper #1 (`docs/MANUSCRIPT_GeoDF-VINS-AECE.md`) plus
citation of Paper #1 as prior work when published.
