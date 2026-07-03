# GeoDF-Adaptive: Geometry-Based Dynamic Feature Rejection for Stereo-Inertial Visual Odometry in Dynamic Scenes

## Abstract

Dynamic objects violate the static-scene assumption used by feature-based visual-inertial odometry (VIO). This paper presents GeoDF-Adaptive, a geometry-based front-end dynamic feature rejection method for stereo-inertial VINS-Fusion. The method combines stereo 3D motion consistency, PnP-RANSAC reprojection scoring, temporal epipolar support, scene-aware activation with auto-calibrated thresholding, quality-aware gating, track-level temporal voting, and ratio-guarded hard rejection. Unlike semantic approaches, GeoDF-Adaptive requires no object detector, segmentation model, or dataset-specific training, and leaves the stereo-inertial back-end unchanged.

The method was implemented in a ROS 2 VINS-Fusion pipeline and evaluated on the original EuRoC and VIODE datasets. On EuRoC (N=3 repeats per sequence), GeoDF-Adaptive preserved static-scene accuracy: no sequence regressed beyond ±5% ATE, and `MH_02_easy` improved by +3.9%. On VIODE (N=5), GeoDF-Adaptive improved ATE in 3 of 12 environment-level conditions under a ±3% decision band, with the strongest gain on `city_day/3_high` (+14.8%). Six conditions remained within the band and three parking-lot conditions regressed when dynamic objects occupied large image regions. A stereo 3D gate ablation outperformed a temporal 2D fundamental-matrix gate on `city_day/3_high` by +9.5% ATE under the same activation stack. Feature-level evaluation against VIODE moving-vehicle masks showed that rejected features on `city_day/3_high` were 17.43× more likely to lie on dynamic objects than randomly tracked features, with static false-positive rate 0.03%. The module added a mean of 1.76 ms per frame on a CPU-only host (about 3.5% of the 20 Hz frame budget). The results support GeoDF-Adaptive as a reproducible, training-free front-end enhancement for dynamic-scene stereo-inertial odometry under moderate dynamic density.

## Keywords

computer vision, dynamic feature rejection, sensor fusion, stereo-inertial odometry, visual odometry

## 1. Introduction

Visual-inertial odometry (VIO) estimates platform motion by fusing visual feature tracks with inertial measurements. It is widely used in mobile robotics, unmanned aerial vehicles, and outdoor autonomous navigation. Feature-based stereo-inertial pipelines such as VINS-Fusion assume that most tracked points belong to a static rigid scene. In urban and traffic environments, moving vehicles and pedestrians violate this assumption and can degrade pose estimation when their features enter the visual-inertial optimizer.

Existing responses include semantic segmentation, object detection, motion segmentation, and robust back-end estimation. Semantic methods can identify dynamic regions directly, but they depend on trained models, add computational cost, and may fail on unseen object classes or domains. Geometry-based filters are attractive for applied systems because they are model-free and easy to reproduce. However, always-on geometric outlier rejection can remove useful static features in low-dynamic scenes, while contaminated geometric models can fail when dynamic content dominates the image.

This paper presents GeoDF-Adaptive, a front-end module for stereo-inertial VINS that rejects geometrically inconsistent feature tracks before state estimation. The method uses stereo 3D motion consistency as the primary dynamic-candidate gate, with temporal epipolar geometry as support and fallback. Unlike always-on hard rejection, GeoDF-Adaptive arms deletion only when a smoothed frame outlier signal and a quality score indicate sustained dynamic inconsistency. Track-level temporal voting further suppresses transient false positives caused by fast rotation, weak texture, or short low-parallax intervals.

The contribution is intentionally scoped as an engineering baseline rather than a universal dynamic VIO system. Both trajectory-level metrics (ATE/RPE) and feature-level metrics against VIODE vehicle masks are reported so that accuracy gains can be related to whether rejected points actually lie on moving objects.

The main contributions are:

1. A training-free, front-end-only dynamic feature rejection module for stereo-inertial VINS based on stereo 3D motion consistency and temporal epipolar support.
2. A scene-aware activation mechanism with auto-calibrated arm threshold `rho_on` estimated from the running epipolar outlier floor of the scene.
3. A quality-aware activation gate and track-level temporal voting that reduce false deletion in static or weakly dynamic frames.
4. A reproducible evaluation on EuRoC and VIODE with N-repeat trajectory benchmarking, stereo 3D versus 2D-F ablation, and feature-level detection analysis using published VIODE segmentation masks.
5. An explicit limitation analysis for high dynamic-density scenes where geometric model contamination causes trajectory regression.

## 2. Related Work

Feature-based VIO systems estimate camera motion by tracking image features and optimizing visual-inertial residuals. They perform well in static scenes but remain sensitive to dynamic features that violate rigid-scene constraints.

Classical robust estimation uses RANSAC and epipolar constraints to discard inconsistent correspondences. These methods are lightweight and model-free, but they typically operate as generic outlier filters rather than scene-adaptive dynamic-object filters. Always-on rejection can harm static-scene accuracy when false positives accumulate frame after frame.

Semantic dynamic SLAM and VIO systems use detectors or segmentation networks to suppress features on cars, pedestrians, or other dynamic classes. Methods such as DynaSLAM provide strong object-level reasoning at the cost of model dependency and higher runtime. Back-end robust methods such as DynaVINS improve optimization under dynamics but modify the estimator itself.

GeoDF-Adaptive occupies a middle ground: it uses only geometric consistency, modifies only the front-end, and adds scene-aware activation so that rejection is deferred in static or low-dynamic conditions. Table 1 summarizes this positioning.

### Table 1. Positioning Against Related Approaches

| Approach family | Uses semantics | Needs training/model | Front-end only | Scene-adaptive | Main limitation |
|---|---:|---:|---:|---:|---|
| Standard VINS-Fusion [1], [2] | no | no | yes | no | assumes mostly static scene |
| Generic RANSAC / epipolar rejection [7], [8] | no | no | yes | no | not scene-aware |
| Robust back-end dynamic VINS (DynaVINS) [6] | no | no | no | no | modifies back-end optimization |
| Semantic dynamic SLAM/VIO (DynaSLAM) [5] | yes | yes | varies | no | detector/segmentation dependency |
| GeoDF-Adaptive (this work) | no | no | yes | yes | high dynamic density can corrupt geometry |

## 3. Proposed Method

GeoDF-Adaptive is inserted in the feature-tracking front-end after temporal KLT tracking and before feature masking and new feature detection [10]. Figure 1 shows the module in the pipeline. Only the front-end is modified; the stereo-inertial back-end is unchanged.

![Figure 1. GeoDF-Adaptive front-end pipeline. The proposed module is inserted after KLT tracking and before new feature detection; the stereo-inertial back-end is unchanged.](../results/geodf_evaluation/figures/pipeline_geodf_adaptive.png)

**Figure 1.** GeoDF-Adaptive front-end pipeline (steps a–g inside the proposed module).

### 3.1 Stereo 3D Motion Consistency

For a tracked stereo feature, GeoDF-Adaptive triangulates the previous-frame left/right observation using calibrated stereo extrinsics. The resulting 3D point in the previous left-camera frame is projected into the current left image. A dominant rigid motion is estimated with PnP RANSAC, and each track is scored by its reprojection residual:

```text
r_i = || project(K, R, t, P_i^{t-1}) - u_i^t ||
```

where `P_i^{t-1}` is the triangulated previous stereo point and `u_i^t` is the current left observation. A feature becomes a dynamic candidate when `r_i` exceeds the configured threshold and sufficient stereo correspondences are available.

Temporal epipolar geometry remains as support and fallback. A fundamental matrix `F` is estimated with RANSAC on temporal left-camera correspondences, and the Sampson residual is computed as

```text
e_i = ((x_i^T F x'_i)^2) /
      ((F x'_i)_1^2 + (F x'_i)_2^2 + (F^T x_i)_1^2 + (F^T x_i)_2^2)
```

If the stereo-3D gate is unavailable, the 2D fallback requires both RANSAC outlier status and `e_i` above threshold. In stereo-3D mode, frame activation additionally requires the 2D epipolar outlier ratio to exceed a small support threshold so that noisy short-baseline depth alone cannot arm hard rejection.

### 3.2 Scene-Aware Activation

Always-on rejection can damage static scenes. GeoDF-Adaptive therefore maintains an exponential moving average of the frame outlier ratio and arms hard deletion only when this signal exceeds an adaptive threshold. The arm threshold is computed from a running outlier floor:

```text
rho_on = clamp(floor * 1.8 + 0.10, 0.14, 0.40)
```

The floor uses asymmetric smoothing: it decreases quickly when the scene becomes static and increases slowly when outlier ratio rises, reducing over-arming in noisy environments.

A quality gate further requires dynamic candidates to be sufficiently frequent and separated from the static background:

```text
candidate_ratio = |C| / N_scored
background_scale = max(tau, median_sampson(background))
residual_lift   = median_sampson(C) / background_scale
quality_score   = clamp(candidate_ratio / r_min) *
                  clamp(residual_lift / l_min)
```

Hard rejection is armed only when both the outlier-ratio signal and the smoothed quality score pass hysteresis gates.

### 3.3 Track-Level Temporal Voting and Guarded Hard Rejection

Each feature ID maintains a dynamic-candidate streak. Hard deletion requires at least `k=3` consecutive flagged frames after a 60-frame warm-up. A ratio guard caps deletions at 40% of tracked features per frame, preserves a minimum feature count, and limits hard deletions to five features per frame to avoid abrupt feature-set collapse.

### 3.4 Parameter Settings

All experiments used one fixed configuration without per-sequence tuning. Table 6 lists the evaluated parameters.

### Table 6. GeoDF-Adaptive Parameters (Evaluated Configuration)

| Parameter | Symbol / key | Value |
|---|---|---:|
| RANSAC reprojection threshold | `ransac_th_px` | 1.0 px |
| Sampson dynamic threshold | tau (`sampson_th`) | 3.0 |
| Min track count to score | `min_track_cnt` | 2 |
| Min features kept | `min_feature_num` | 40 |
| Outlier-ratio EMA factor | `activate_ema` | 0.15 |
| Auto threshold multiplier | `auto_mult` | 1.8 |
| Auto threshold margin | `auto_margin` | 0.10 |
| Arm threshold clamp | rho_on range | [0.14, 0.40] |
| Floor adapt-down / up rate | `floor_down` / `floor_up` | 0.02 / 0.004 |
| Disarm hysteresis fraction | `deactivate_frac` | 0.6 |
| Quality gate | `quality_gate` | enabled |
| Quality EMA factor | `quality_ema` | 0.15 |
| Min candidate ratio | `min_candidate_ratio` | 0.05 |
| Min residual lift | `min_residual_lift` | 2.5 |
| Temporal voting frames | k (`vote_frames`) | 3 |
| Warm-up frames | `warmup_frames` | 60 |
| Max reject ratio (guard) | `max_reject_ratio` | 0.40 |
| Max hard deletions per frame | `max_reject_per_frame` | 5 |
| Stereo 3D motion gate | `motion3d_enable` | enabled |
| PnP RANSAC reprojection threshold | `motion3d_residual_th` | 3.0 px |
| Min stereo 3D correspondences | `motion3d_min_points` | 25 |
| 2D support ratio for 3D activation | `motion3d_min_2d_ratio` | 0.10 |

## 4. Experimental Setup

### 4.1 Datasets and Protocol

EuRoC [3] was used as a static-scene safety benchmark on Machine Hall sequences `MH_01_easy` through `MH_05_difficult`. VIODE [4] was used as the dynamic-scene benchmark on `city_day`, `city_night`, and `parking_lot`, each at four dynamic levels: `0_none`, `1_low`, `2_mid`, and `3_high`.

Three front-end configurations were compared on VIODE with the same stereo-inertial back-end:

- `baseline`: standard VINS-Fusion feature tracking without GeoDF;
- `adaptive_2d`: GeoDF-Adaptive with temporal 2D fundamental-matrix gating;
- `adaptive`: GeoDF-Adaptive with stereo 3D motion-consistency gating (proposed).

Each VIODE cell was repeated five times within the same software build. EuRoC baseline and proposed runs were repeated three times per sequence. Trajectory decisions used a ±3% ATE improvement band for VIODE and a ±5% no-regression criterion for EuRoC static safety.

### 4.2 Metrics

Trajectory accuracy was measured using ATE RMSE and RPE RMSE in metres with the `evo` toolkit [9] after SE(3) Umeyama alignment. Feature-level evaluation matched feature coordinates to the nearest VIODE segmentation mask within 30 ms; points inside `vehicle_dynamic_*` masks were labelled dynamic. Reported detection metrics were precision lift, recall, and static false-positive rate.

### 4.3 Implementation and Runtime Measurement

The method was implemented in C++ inside a VINS-Fusion stereo-inertial ROS 2 pipeline. Runtime was logged in-pipeline as `geo_ms` for every processed frame. Statistics were aggregated over 75,012 logged frames from 60 VIODE adaptive trials. Experiments ran on an Intel Xeon W-11955M CPU (8 cores, 16 threads, 2.60 GHz) with 64 GB RAM; no GPU was used in the front-end.

## 5. Results and Discussion

### 5.1 VIODE Trajectory Results

Table 2 summarizes VIODE ATE results. GeoDF-Adaptive improved ATE in 3 of 12 conditions under the ±3% band, remained neutral in 6 conditions, and regressed in 3 parking-lot conditions. The strongest improvement was on `city_day/3_high` (+14.8%), where baseline ATE was 0.344 ± 0.003 m and proposed ATE was 0.293 ± 0.000 m. Non-parametric testing on per-trial ATE values indicated a significant improvement on this cell (Mann-Whitney U, p = 0.012). The three parking-lot regressions were also statistically significant under the same test, confirming that high dynamic density is a structured failure mode rather than random noise.

Secondary gains appeared on `city_day/0_none` (+7.7%) and `city_night/0_none` (+5.8%), both within or near the decision band. Most low- and mid-dynamic cells remained within ±3%, indicating that scene-aware activation preserved baseline behaviour when hard rejection was unnecessary.

### Table 2. VIODE N=5 Trajectory Summary (ATE RMSE, metres)

| Environment | Level | Baseline ATE | Proposed ATE | Improvement |
|---|---|---:|---:|---:|
| city_day | 0_none | 0.118 ± 0.018 | 0.109 ± 0.000 | +7.7% |
| city_day | 1_low | 0.139 ± 0.000 | 0.139 ± 0.000 | −0.2% |
| city_day | 2_mid | 0.166 ± 0.001 | 0.166 ± 0.000 | −0.2% |
| city_day | 3_high | 0.344 ± 0.003 | 0.293 ± 0.000 | +14.8% |
| city_night | 0_none | 0.418 ± 0.000 | 0.394 ± 0.049 | +5.8% |
| city_night | 1_low | 0.505 ± 0.009 | 0.500 ± 0.000 | +0.9% |
| city_night | 2_mid | 0.497 ± 0.000 | 0.502 ± 0.010 | −1.0% |
| city_night | 3_high | 0.884 ± 0.000 | 0.884 ± 0.000 | +0.0% |
| parking_lot | 0_none | 0.167 ± 0.000 | 0.167 ± 0.000 | −0.1% |
| parking_lot | 1_low | 0.118 ± 0.000 | 0.125 ± 0.000 | −6.1% |
| parking_lot | 2_mid | 0.144 ± 0.000 | 0.152 ± 0.000 | −5.6% |
| parking_lot | 3_high | 0.119 ± 0.000 | 0.148 ± 0.000 | −23.8% |

Table 2b compares the proposed stereo 3D gate with the older 2D-F gate under the same activation stack. On `city_day/3_high`, stereo 3D reduced ATE from 0.324 m to 0.293 m (+9.5% relative to 2D-F). On `parking_lot/2_mid`, stereo 3D also outperformed 2D-F (+30.5%), showing that the 3D gate can mitigate false activation caused by epipolar contamination even though absolute trajectory accuracy still regressed relative to baseline on several parking-lot cells.

### Table 2b. Stereo 3D vs 2D-F Gate Ablation (N=5, ATE RMSE, metres)

| Environment | Level | 2D-F ATE | Stereo 3D ATE | 3D vs 2D-F |
|---|---|---:|---:|---:|
| city_day | 0_none | 0.109 ± 0.000 | 0.109 ± 0.000 | +0.0% |
| city_day | 1_low | 0.139 ± 0.000 | 0.139 ± 0.000 | +0.1% |
| city_day | 2_mid | 0.167 ± 0.002 | 0.166 ± 0.000 | +0.7% |
| city_day | 3_high | 0.324 ± 0.005 | 0.293 ± 0.000 | +9.5% |
| city_night | 0_none | 0.418 ± 0.000 | 0.394 ± 0.049 | +5.8% |
| city_night | 1_low | 0.500 ± 0.000 | 0.500 ± 0.000 | +0.0% |
| city_night | 2_mid | 0.497 ± 0.000 | 0.502 ± 0.010 | −1.0% |
| city_night | 3_high | 0.884 ± 0.000 | 0.884 ± 0.000 | +0.0% |
| parking_lot | 0_none | 0.167 ± 0.000 | 0.167 ± 0.000 | +0.0% |
| parking_lot | 1_low | 0.101 ± 0.000 | 0.125 ± 0.000 | −23.7% |
| parking_lot | 2_mid | 0.219 ± 0.007 | 0.152 ± 0.000 | +30.5% |
| parking_lot | 3_high | 0.140 ± 0.003 | 0.148 ± 0.000 | −5.6% |

Figure 2 visualizes the per-condition ATE change against the ±3% decision band.

![Figure 2. VIODE N=5 ATE improvement per environment-level condition. Solid bars improve, hatched bars regress; the shaded region is the ±3% decision band.](../results/geodf_evaluation/figures/viode_ate_delta_n5_gray.png)

**Figure 2.** VIODE N=5 ATE improvement of GeoDF-Adaptive over baseline (CD = city_day, CN = city_night, PL = parking_lot).

### 5.2 EuRoC Static Safety

Table 3 reports EuRoC results. GeoDF-Adaptive did not exceed the ±5% static-safety regression limit on any sequence. `MH_02_easy` improved by +3.9% ATE. Because repeated runs within the same build were nearly deterministic, the main EuRoC conclusion is absence of static-scene harm rather than large universal improvement.

### Table 3. EuRoC Static Safety (N=3, ATE RMSE, metres)

| Sequence | Baseline ATE | Proposed ATE | Improvement |
|---|---:|---:|---:|
| MH_01_easy | 0.180 ± 0.000 | 0.180 ± 0.000 | +0.0% |
| MH_02_easy | 0.169 ± 0.000 | 0.162 ± 0.005 | +3.9% |
| MH_03_medium | 0.292 ± 0.000 | 0.292 ± 0.000 | +0.0% |
| MH_04_difficult | 0.447 ± 0.000 | 0.447 ± 0.000 | +0.0% |
| MH_05_difficult | 0.298 ± 0.000 | 0.298 ± 0.000 | +0.0% |

### 5.3 Feature-Level Dynamic Detection

Trajectory metrics alone cannot verify that rejected features belong to moving objects. Dedicated dump runs with the stereo 3D gate were therefore compared against VIODE moving-vehicle masks (Table 4). On `city_day/3_high`, rejected features were 17.43× more likely to fall on moving vehicles than randomly selected tracked features, with static false-positive rate 0.03%. Lift remained above 1× on parking-lot cells but decreased as dynamic base rate increased, explaining why trajectory accuracy can regress even when some dynamic features are identified.

### Table 4. VIODE Dynamic Feature Detection (stereo 3D gate)

| Environment | Level | Dynamic base rate | Precision lift | Static FPR |
|---|---|---:|---:|---:|
| city_day | 3_high | 4.27% | 17.43× | 0.03% |
| parking_lot | 1_low | 0.78% | 78.11× | 0.06% |
| parking_lot | 2_mid | 10.42% | 6.36× | 0.19% |
| parking_lot | 3_high | 13.73% | 4.96× | 0.17% |

![Figure 3. Dynamic-feature detection lift on VIODE GT masks. Bars above the dashed line (lift = 1×) indicate rejections concentrated on moving vehicles more than random sampling.](../results/geodf_evaluation/figures/viode_detection_lift_gray.png)

**Figure 3.** Dynamic-feature detection lift on VIODE moving-vehicle masks.

### 5.4 Computational Overhead

Table 5 reports runtime on VIODE adaptive trials. The mean GeoDF-Adaptive cost was 1.76 ms per frame (median 1.65 ms, 95th percentile 2.57 ms), corresponding to about 3.5% of the 50 ms budget at 20 Hz. Hard rejection was armed on only 2.4% of frames, so most frames paid mainly for geometric scoring rather than full deletion.

### Table 5. Measured GeoDF-Adaptive Runtime (VIODE, CPU-only)

| Metric | Value |
|---|---:|
| Mean per-frame cost | 1.76 ms |
| Median per-frame cost | 1.65 ms |
| 95th percentile | 2.57 ms |
| 99th percentile | 3.64 ms |
| Fraction of 50 ms (20 Hz) budget | 3.52% |
| Frames with rejection armed | 2.4% |
| Logged frames | 75,012 (60 trials) |

### 5.5 Limitation Analysis

GeoDF-Adaptive is most effective when dynamic objects create clear geometric inconsistency but do not dominate the scene. `city_day/3_high` satisfies this condition and yields the main trajectory gain. In `parking_lot`, large moving regions increase dynamic base rate above 10% and weaken the rigid-scene assumption behind both 2D and 3D gates. The parking-lot regressions therefore represent a structural limitation rather than an implementation artefact.

Future work may combine the current front-end with IMU-compensated epipolar scoring, progressive model estimation, and soft-weighted back-end down-weighting instead of hard deletion.

## 6. Conclusion

This paper presented GeoDF-Adaptive, a geometry-only dynamic feature rejection module for stereo-inertial VINS. The method integrates stereo 3D motion consistency, temporal epipolar support, scene-aware activation, quality-aware gating, temporal voting, and guarded hard rejection in the front-end while leaving the back-end unchanged. On VIODE, it improved ATE in 3 of 12 conditions under a ±3% band, with a +14.8% gain on `city_day/3_high`. On EuRoC, it preserved static-scene accuracy with no sequence regressing beyond ±5%. Feature-level evaluation confirmed that rejected points align with moving vehicles on favourable cells, while high dynamic-density parking-lot scenes remain a limitation. GeoDF-Adaptive is therefore best viewed as a reproducible, training-free enhancement for moderate-dynamic outdoor stereo-inertial odometry rather than a universal dynamic VIO solution.

## References

[1] T. Qin, P. Li, and S. Shen, "VINS-Mono: A robust and versatile monocular visual-inertial state estimator," *IEEE Transactions on Robotics*, vol. 34, no. 4, pp. 1004-1020, 2018. DOI: 10.1109/TRO.2018.2853729

[2] T. Qin, J. Pan, S. Cao, and S. Shen, "A general optimization-based framework for local odometry estimation with multiple sensors," *arXiv preprint* arXiv:1901.03638, 2019.

[3] M. Burri, J. Nikolic, P. Gohl, T. Schneider, J. Rehder, S. Omari, M. W. Achtelik, and R. Siegwart, "The EuRoC micro aerial vehicle datasets," *The International Journal of Robotics Research*, vol. 35, no. 10, pp. 1157-1163, 2016. DOI: 10.1177/0278364915620033

[4] K. Minoda, F. Schilling, V. Wuest, D. Floreano, and T. Yairi, "VIODE: A simulated dataset to address the challenges of visual-inertial odometry in dynamic environments," *IEEE Robotics and Automation Letters*, vol. 6, no. 2, pp. 1343-1350, 2021. DOI: 10.1109/LRA.2021.3058073

[5] B. Bescos, J. M. Facil, J. Civera, and J. Neira, "DynaSLAM: Tracking, mapping, and inpainting in dynamic scenes," *IEEE Robotics and Automation Letters*, vol. 3, no. 4, pp. 4076-4083, 2018. DOI: 10.1109/LRA.2018.2860039

[6] S. Song, H. Lim, A. J. Lee, and H. Myung, "DynaVINS: A visual-inertial SLAM for dynamic environments," *IEEE Robotics and Automation Letters*, vol. 7, no. 4, pp. 11523-11530, 2022. DOI: 10.1109/LRA.2022.3203231

[7] M. A. Fischler and R. C. Bolles, "Random sample consensus: A paradigm for model fitting with applications to image analysis and automated cartography," *Communications of the ACM*, vol. 24, no. 6, pp. 381-395, 1981. DOI: 10.1145/358669.358692

[8] R. Hartley and A. Zisserman, *Multiple View Geometry in Computer Vision*, 2nd ed. Cambridge, U.K.: Cambridge Univ. Press, 2004. DOI: 10.1017/CBO9780511811685

[9] M. Grupp, "evo: Python package for the evaluation of odometry and SLAM," 2017. [Online]. Available: https://github.com/MichaelGrupp/evo

[10] J. Shi and C. Tomasi, "Good features to track," in *Proc. IEEE Conf. Computer Vision and Pattern Recognition (CVPR)*, 1994, pp. 593-600. DOI: 10.1109/CVPR.1994.323794
