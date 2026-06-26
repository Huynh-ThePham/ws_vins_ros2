# Proposal — Paper #2: IMU-Aided Geometric Dynamic-Feature Rejection

Branch: `paper/geodf-imu-dynamic-2026-q4`
Builds directly on Paper #1 (GeoDF-Adaptive, frozen at AECE submission state).

Working names (pick one later): **GeoDF-Inertial**, **iGeoDF**, **InertialGeoDF**.

---

## 1. Motivation (the exact gap left by Paper #1)

Paper #1 detects dynamic features from **temporal epipolar geometry estimated
from the features themselves** (`cv::findFundamentalMat` over tracked points,
then a Sampson + RANSAC dual gate). Its measured failure mode is explicit in the
manuscript and in the data:

- **Majority-rigid assumption breaks.** When moving objects occupy a large
  image fraction, RANSAC fits the fundamental matrix `F` partly to the dynamic
  set, so the rigidity reference is contaminated. This is exactly the
  `parking_lot/2_mid` (-36.4%) and `parking_lot/3_high` (-44.3%) regression and
  the detection-lift collapse to ~1.4x at 10-14% dynamic base rate.
- **Two-view degeneracy.** Under low parallax or near-pure rotation, the
  two-frame `F` is ill-conditioned, producing transient false positives that
  Paper #1 can only suppress *after the fact* with temporal voting and a
  scene-aware gate (which also delays/weakens true detections).

**Core idea of Paper #2:** stop estimating the rigidity reference from the
(possibly contaminated) features. Instead, **predict the rigid-scene epipolar
geometry from the IMU / VINS state** and score each feature against that
inertial prediction. Because the prediction does not depend on the static
feature majority, it does not collapse when dynamics dominate — directly
targeting the parking-lot failure case.

This is the future-work line already stated in Paper #1's limitation section
("IMU-compensated epipolar scoring"), now made the central contribution.

---

## 2. Hypothesis

> If the rigid-scene epipolar constraint between consecutive frames is taken
> from the inertially-predicted relative camera pose (metric, available from the
> VINS back-end) rather than from a feature-fit fundamental matrix, then dynamic
> feature detection becomes robust to high dynamic density and to low-parallax /
> rotation-dominated motion, removing Paper #1's parking-lot regression while
> preserving its static-scene safety.

Falsifiable predictions to test:
1. On `parking_lot/{2_mid,3_high}` the regression turns non-negative (or at
   least materially smaller) vs Paper #1.
2. Detection lift on high-density scenes rises above the ~1.4x floor.
3. EuRoC static-scene safety is preserved (no regression).
4. The advantage is largest exactly where Paper #1's feature-fit `F`
   outlier-floor was highest.

---

## 3. Method

### 3.1 Inertially-predicted epipolar geometry

Let the (bias-corrected) relative IMU rotation and the metric relative
translation between image times `t-1` and `t` be `R_b`, `p_b`, expressed in the
body/IMU frame. Using the camera-IMU extrinsics `(R_ic, t_ic)` the relative
**camera** motion is

```text
R_rel = R_ic^T  R_b  R_ic
t_rel = R_ic^T ( R_b t_ic + p_b - t_ic )
```

The predicted rigid-scene essential matrix and (pseudo-pixel) fundamental matrix
are

```text
E_imu = [t_rel]_x R_rel
F_imu = K^{-T} E_imu K^{-1}
```

Two properties make this practical *inside a VINS* (and impossible in a pure
monocular front-end):
- **Metric translation is available.** `t_rel` scale comes from the optimized
  window state, so `E_imu` is fully determined (no scale ambiguity).
- **No feature fit is needed.** `E_imu` is independent of how many features are
  dynamic, so it is immune to majority-rigid contamination.

### 3.2 Inertial Sampson residual + dual gate

For each tracked correspondence `(x_{t-1}, x_t)` (lifted to the normalized
plane and mapped to pseudo-pixels exactly as in Paper #1), score

```text
r_i = Sampson(F_imu, x_t, x_{t-1})
```

A feature is a dynamic candidate when `r_i > tau_imu`. Keep the Paper #1 *dual*
philosophy but make the second gate inertial: candidate iff
`(r_i > tau_imu)` AND `(it is also inconsistent with a robust refit / right-view
check)`. The threshold `tau_imu` is set from the **propagated covariance** of
`R_rel, t_rel` (the preintegration covariance) instead of a fixed pixel value,
so noisier inertial predictions automatically widen the gate.

### 3.3 Degeneracy handling: gyro-derotated residual flow

When `||t_rel||` is below a parallax floor (hover / pure rotation), the epipolar
constraint is weak. In that regime switch to **rotation-compensated flow**:
derotate the predicted static flow using `R_rel` and flag features whose
residual image motion (observed minus rotation-induced) exceeds a noise-scaled
threshold. Rotation from the gyro is accurate at short horizons, so independently
moving objects still stand out even when translation parallax is unusable —
covering the case Paper #1 handled only with temporal voting.

### 3.4 Reliability gating + fallback to Paper #1

Three-state arbitration per frame, by inertial reliability:
- **Inertial-reliable** (good excitation, low preintegration covariance,
  sufficient parallax): use `F_imu` scoring (3.2).
- **Low-parallax**: use derotated residual flow (3.3).
- **Inertial-unavailable / pre-init / high covariance**: fall back to Paper #1's
  scene-aware feature-fit GeoDF-Adaptive.

So Paper #1 is not discarded — it becomes the *safe fallback*, and Paper #2 is
the primary path whenever the inertial geometry is trustworthy. This is also a
clean ablation axis (IMU-only vs feature-only vs hybrid).

### 3.5 Concrete integration points in this codebase (low-risk)

The data path the method needs already exists:

- The estimator already projects 3D landmarks into the predicted next frame and
  pushes them to the front-end:
  `Estimator::predictPtsInNextFrame()` -> `featureTracker.setPrediction(...)`
  (`src/pht_vio/src/estimator/estimator.cpp:1477-1508`). We extend the same hook
  to also push `R_rel, t_rel` (and their covariance) to the tracker.
- Relative rotation/translation and uncertainty come from
  `IntegrationBase::{delta_q, delta_p, covariance}`
  (`src/pht_vio/src/factor/integration_base.h`) and the propagated state
  `latest_Q/latest_P` (`fastPredictIMU`, `estimator.cpp:1584`).
- Extrinsics `R_ic = ric`, `t_ic = tic[0]` are already in `vinsConfig()`.
- The scoring slots into the existing front-end function `rejectGeoDynamic()`
  (`src/pht_vio/src/featureTracker/feature_tracker.cpp:378`) right where
  `findFundamentalMat` is called today — i.e. a localized change, the back-end
  optimizer stays untouched (same non-invasive story as Paper #1).

New config keys (mirroring Paper #1 style): `geodf_imu_enable`,
`geodf_imu_tau_scale`, `geodf_imu_parallax_min`, `geodf_imu_cov_max`,
`geodf_imu_fallback`.

---

## 4. Contributions (claimed)

1. An **inertially-predicted epipolar rigidity reference** for front-end dynamic
   feature rejection in stereo-inertial VINS, removing the static-majority
   assumption of feature-fit fundamental matrices.
2. A **covariance-scaled inertial Sampson gate** that adapts the rejection
   threshold to preintegration uncertainty rather than a fixed pixel value.
3. A **gyro-derotated residual-flow mode** for low-parallax / rotation-dominated
   frames where two-view epipolar geometry degenerates.
4. A **reliability-gated hybrid** that uses inertial geometry when trustworthy
   and falls back to geometry-only GeoDF-Adaptive (Paper #1) otherwise.
5. A focused evaluation showing recovery of Paper #1's high-density failure case
   (VIODE parking_lot) with preserved EuRoC static safety, including a
   feature-level (segmentation-mask) detection study and a runtime budget.

---

## 5. Experiment Plan

**Datasets:** same as Paper #1 (EuRoC static safety; VIODE city_day/city_night/
parking_lot x {0_none,1_low,2_mid,3_high}). The headline target is
`parking_lot/{2_mid,3_high}` where Paper #1 regressed.

**Baselines / methods:**
- B0: VINS-Fusion (no dynamic filtering).
- B1: GeoDF-Adaptive (Paper #1, feature-fit F).
- M : GeoDF-Inertial (proposed, F_imu + derotation + fallback).
- Ablations: IMU-only (no fallback); IMU without derotation; fixed `tau_imu`
  vs covariance-scaled; hybrid on/off.

**Metrics:** ATE/RPE RMSE (mean+/-std over N=5, same protocol/seeds as Paper #1);
feature-level detection lift / recall / static-FPR against VIODE masks; per-frame
runtime (same in-pipeline `t_geo`-style timer, must stay comparable to Paper #1's
0.28 ms/frame).

**Decision rule:** reuse Paper #1's +/-3% improvement band so results are
directly comparable across the two papers.

---

## 6. Claims to make / avoid

Make: robustness to high dynamic density; metric inertial geometry as the key
enabler inside a VINS; preserved static safety; honest per-regime behaviour.

Avoid: universal SOTA dynamic-VIO claims; claims requiring perfect calibration;
any "removes all dynamics" wording. Keep the same applied-engineering framing
that fit AECE (and is a candidate venue here too).

---

## 7. Risks and mitigations

| Risk | Mitigation |
|---|---|
| Small inter-frame translation makes `E_imu` weak | parallax floor -> derotation mode (3.3) |
| Extrinsic / time-offset error biases `F_imu` | covariance-scaled gate; require `estimate_td` consistency; calibrate on EuRoC first |
| Bias / scale error right after init | reliability gate falls back to Paper #1 until window converges |
| Added latency | scoring is O(N) per feature like Paper #1; no extra RANSAC in inertial-reliable mode (cheaper) |
| Stereo right-view check cost | keep optional, reuse Paper #1's stereo cross-check code path |

---

## 8. Work plan / milestones

1. **M1 — Plumbing:** done (`pushImuEpipolarAtImage`, `setImuEpipolar`).
2. **M2 — Inertial scoring:** done (`imuFundamental`, mode 1 Sampson gate).
3. **M3 — Degeneracy + fallback:** done (mode 2 derotation, mode 0 fallback, `max_dyn_frac`).
4. **M4 — Config + build:** done (VIODE/EuRoC YAML, colcon build OK).
5. **M5 — N=5 benchmark:** in progress (`run_geodf_inertial_n5.sh`).
6. **M6 — Manuscript + figures:** draft + `make_inertial_figure.py` (update after N=5).

**Validated smoke:** `parking_lot/3_high` ATE 0.108 m (trial 1) vs Paper #1 adaptive 0.172 m.

---

## 9. Relationship to Paper #1

- Paper #1 = geometry-only, training-free, self-contained (frozen, submittable).
- Paper #2 = adds inertial prediction to fix Paper #1's documented limitation;
  cites Paper #1 as the geometry-only baseline and fallback.
- Shared evaluation harness and metrics keep the two papers directly comparable
  and let the series "đăng từ từ" (publish incrementally) without redoing setup.
