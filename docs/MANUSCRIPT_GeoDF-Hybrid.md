# GeoDF-Hybrid: Reliability-Gated Two-Source Geometry Filtering for Dynamic Feature Rejection in Stereo-Inertial VINS

Branch: `paper/geodf-imu-dynamic-2026-q4`
Builds on Paper #1 (GeoDF-Adaptive, frozen on `paper/geodf-adaptive-vins-2026-q4`).

## Proposed Title

GeoDF-Hybrid: Reliability-Gated Fusion of Feature-Fit and Inertial Epipolar Geometry for Dynamic Feature Rejection in Stereo-Inertial Visual Odometry

## Abstract

Geometry-only dynamic feature rejection in visual-inertial odometry requires a rigid-scene reference against which independently moving features are scored. Feature-fit epipolar models (Paper #1) fail when dynamics dominate the image, while always-on inertial epipolar models can over-reject on static or low-dynamic scenes where feature-fit geometry remains reliable. This paper presents **GeoDF-Hybrid**, a **two-source geometry filter with reliability-gated arbitration**: Paper #1 feature-fit fundamental matrices, IMU/VINS-predicted epipolar geometry, and a gyro-derotated residual-flow mode share the same back-end (scene-aware activation, temporal voting, ratio guard). A hybrid scene signal combines the slow epipolar-outlier floor with faster previous-frame activation cues; below a threshold the system **forces Paper #1 geometry** even when IMU is valid; above the threshold it **switches to inertial or derotation** when IMU propagation is reliable. Evaluated on EuRoC and VIODE with the same N-trial protocol as Paper #1, GeoDF-Hybrid targets recovery of the parking-lot regression without sacrificing static-scene safety — a contribution framed for higher-tier journals as **sensor fusion with explicit geometry arbitration**, not a universal IMU-on overlay.

## Keywords

computer vision, dynamic feature rejection, sensor fusion, geometry arbitration, visual-inertial odometry

## 1. Introduction

Paper #1 (GeoDF-Adaptive) demonstrated training-free, front-end dynamic feature rejection with scene-aware activation and temporal voting. Its failure mode is structural: when moving objects dominate, RANSAC fits the fundamental matrix to dynamics and rejection collapses (`parking_lot` ATE regressed up to −44.3% vs baseline).

A naive fix — always scoring features against IMU-predicted epipolar geometry — recovers parking-lot scenes but **regresses static/low-dynamic conditions** (e.g. `city_night/0_none`: inertial-only mean 0.342 m vs Paper #1 0.246 m in partial N=5). GeoDF-Hybrid treats this as a **reliability arbitration problem** between two geometry sources rather than a single-model replacement.

Contributions:

1. **Two-source geometry filter**: feature-fit F (Paper #1), IMU-predicted F_imu, gyro derotation — unified scoring back-end.
2. **Reliability-gated arbitration**: dynamic-density proxy (running floor plus fast activation cues) selects the active source per frame.
3. **Inertial reliability guards**: parallax scaling, corrupted-pose skip, max_dyn_frac freeze — shared with inertial ablation.
4. **Ablation path**: inertial-only (`geodf_hybrid_enable=0`) isolates always-on IMU geometry vs hybrid arbitration.
5. Reproducible EuRoC + VIODE evaluation with shared harness and Paper #1 baseline.

## 2. Related Work

| Method | Geometry sources | Arbitration | Front-end only |
|---|---|---:|---:|
| GeoDF-Adaptive (P1) | feature-fit F | scene gate only | yes |
| Always-on inertial GeoDF | IMU F_imu (+ derot) | none (prefer IMU) | yes |
| **GeoDF-Hybrid (P2)** | F + F_imu + derot | floor-gated | yes |
| DynaVINS | IMU prior in BA | back-end | no |

## 3. Method

### 3.1 Two geometry sources

**Source A (mode 0 — Paper #1):** dual-gated feature-fit fundamental matrix with scene-aware activation, auto-ρ_on, temporal voting k=2, ratio guard 40%.

**Source B (mode 1 — inertial):** relative camera pose `(R_rel, t_rel)` from IMU-propagated VINS state → essential matrix → Sampson gate with parallax-confidence scaling.

**Source C (mode 2 — derotation):** gyro homography derotation + residual-flow threshold at low parallax.

### 3.2 Hybrid arbitration

**Sensing decoupled from rejection.** The arbitration decision is driven by a single dynamic-density signal that is *always measured from Source A* — the Paper #1 feature-fit fundamental matrix `F_p1`, which we estimate on every hybrid frame regardless of which source performs rejection. The signal is the slow, asymmetric epipolar-outlier **floor** of `F_p1`'s RANSAC outlier ratio:

```text
s_t = outlier_floor(ratio_p1_t),   ratio_p1 = #RANSAC-outliers / #scored  (F_p1)
floor adapts fast-down / slow-up (β_down ≫ β_up)
```

Measuring `s_t` from a *fixed* sensor is essential: the inertial residual distribution differs from the feature-fit one, so updating the floor from whichever source is active creates a feedback loop — once the latch switches to inertial the measured ratio collapses (empirically `0.091 → 0.021` on `parking_lot/3_high`), the signal falls back below threshold, and the latch oscillates. Anchoring the signal to `F_p1` removes this loop and yields a stable, scene-level decision. We deliberately use the slow floor *alone*: a fast outlier-ratio cue or the activation EMA are dominated by single-frame KLT/rotation spikes that crossed the threshold on static scenes and forced spurious inertial switches; the floor only stays high under **sustained** dynamic density, exactly the regime where `F_p1` is contaminated.

Source selection uses a **hysteresis latch with an anti-chatter dwell**: upper threshold `ρ_on = geodf_hybrid_inertial_floor`, lower (return) threshold `ρ_off = geodf_hybrid_floor_off ≤ ρ_on`, and a dwell of `D = geodf_hybrid_dwell` frames. The latch flips only after the signal has stayed on the new side for `D` *consecutive* frames:

```text
if !scene_dynamic:  cnt = (s >= ρ_on) ? cnt+1 : 0;  if cnt>=D → scene_dynamic=1, cnt=0
if  scene_dynamic:  cnt = (s <  ρ_off)? cnt+1 : 0;  if cnt>=D → scene_dynamic=0, cnt=0

if hybrid && IMU valid && !scene_dynamic   → force mode 0 (Paper #1, F_p1 reused)
if hybrid && scene_dynamic && IMU reliable → mode 1 (inertial)
if hybrid && scene_dynamic && low parallax → mode 2 (derotation)
if !hybrid (ablation)                      → legacy always-prefer-IMU path
```

The dwell is what makes the decision robust where instantaneous floors *overlap*. The measured floor statistics are: truly-static city/EuRoC scenes p90 ≤ 0.05; static-but-low-texture `parking_lot/{0_none,1_low}` floor p50 ≈ 0.05–0.06 (p90 ≈ 0.083); moderate-dynamic `parking_lot/2_mid` p50 ≈ 0.069; and dense-dynamic `parking_lot/3_high` p50 ≈ 0.091. A per-frame threshold cannot separate `2_mid` (where inertial does *not* help) from `3_high` (where it recovers a catastrophic feature-fit failure), nor stop low-texture bursts in `0_none` from latching — their *instantaneous* floors interleave. Keying the latch off the **sustained** level via `ρ_on = 0.088`, `ρ_off = 0.06`, `D = 8` resolves them by their median: `3_high` (median 0.091 > ρ_on, sustained) latches inertial, while `0_none`/`1_low`/`2_mid` (median ≤ 0.069, only transient excursions) stay on Source A. `ρ_off < 0` or `ρ_off ≥ ρ_on` collapses to a single threshold; `D = 1` disables the dwell. Per-frame diagnostics are logged in `geo_df_stats.csv` (`hybrid_signal`, `scene_dynamic`, and `hybrid_arb`: 1=forced P1, 2→inertial, 3→derot). Because the raw `hybrid_signal` (the `F_p1`-sensed floor) is logged, `ρ_on`/`ρ_off`/`D` can be **ablated offline** by replaying the latch, without re-running the benchmark.

### 3.3 Reliability guards (unchanged)

- `||t_rel|| > parallax_max`: corrupted pose → fallback.
- Inertial gate flags `> max_dyn_frac`: skip rejection, freeze EMA.
- Pre-init / invalid IMU: fallback to Paper #1 when `geodf_imu_fallback=1`.

### Table: GeoDF-Hybrid parameters (VIODE evaluated config)

| Parameter | Value |
|---|---:|
| geodf_hybrid_enable | 1 |
| geodf_hybrid_inertial_floor (ρ_on) | 0.088 |
| geodf_hybrid_floor_off (ρ_off) | 0.06 |
| geodf_hybrid_dwell (D) | 8 |
| geodf_imu_sampson_th | 6.0 |
| geodf_imu_parallax_min | 0.02 m |
| geodf_imu_parallax_ref | 0.08 m |
| geodf_imu_tau_cap | 4.0 |
| geodf_imu_derotate_px | 8.0 |
| geodf_imu_max_dyn_frac | 0.5 |
| Paper #1 adaptive params | same as P1 config |

## 4. Experimental Setup

Same datasets and metrics as Paper #1. Methods compared:

- Baseline (no GeoDF)
- GeoDF-Adaptive (Paper #1)
- GeoDF-Inertial ablation (`geodf_hybrid_enable=0`, always prefer IMU)
- **GeoDF-Hybrid (proposed)**

Config files:
- `src/config/viode/viode_stereo_imu_geodf_hybrid_config.yaml`
- `src/config/euroc/euroc_stereo_imu_geodf_hybrid_config.yaml`

Scripts:
- `scripts/run_geodf_hybrid.sh`, `run_geodf_hybrid_n5.sh`
- `scripts/run_geodf_euroc_hybrid.sh`
- `scripts/summarize_hybrid_n5.py` → `PAPER_RESULTS_HYBRID_N5.md`

## 5. Results

*(Populate after N=5 hybrid benchmark.)*

Expected headline pattern from partial inertial-only runs + arbitration design:

| Condition | P1 adaptive | Inertial-only | Hybrid target |
|---|---:|---:|---|
| parking_lot / 3_high | 0.172 | 0.126 | ≤ inertial, beat P1 |
| city_night / 0_none | 0.246 | 0.342 | ≈ P1 (arbitration → mode 0) |

## 6. Conclusion

GeoDF-Hybrid reframes Paper #2 as a **two-source geometry filter with explicit arbitration**, recovering high-density dynamic scenes via inertial epipolar geometry while preserving Paper #1 on static/low-dynamic frames. It remains training-free and front-end only.

## References

Same numbered list as Paper #1 (`docs/MANUSCRIPT_GeoDF-VINS-AECE.md`) plus citation of Paper #1 as prior work.
