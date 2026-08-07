/*******************************************************
 * Copyright (C) 2019, Aerial Robotics Group, Hong Kong University of Science and Technology
 *
 * VINS configuration (algorithm layer).
 *******************************************************/

#pragma once

#include <pht_slam_common/logging.hpp>
#include <pht_slam_common/utility.hpp>
#include <vector>
#include <eigen3/Eigen/Dense>
#include <opencv2/opencv.hpp>
#include <opencv2/core/eigen.hpp>
#include <fstream>
#include <map>
#include <string>

using namespace std;

const double FOCAL_LENGTH = 460.0;
const int WINDOW_SIZE = 10;
const int NUM_OF_F = 1000;
const int FEATURE_OBS_SIZE = 8;

using FeatureObservation = Eigen::Matrix<double, FEATURE_OBS_SIZE, 1>;

struct VinsConfig
{
    double init_depth = 5.0;
    double min_parallax = 0.0;
    int estimate_extrinsic = 0;

    double acc_n = 0.0;
    double acc_w = 0.0;
    double gyr_n = 0.0;
    double gyr_w = 0.0;

    std::vector<Eigen::Matrix3d> ric;
    std::vector<Eigen::Vector3d> tic;
    Eigen::Vector3d g{0.0, 0.0, 9.8};

    double bias_acc_threshold = 0.1;
    double bias_gyr_threshold = 0.1;
    double solver_time = 0.0;
    int num_iterations = 0;
    std::string ex_calib_result_path;
    std::string vins_result_path;
    std::string output_folder;
    std::string imu_topic;
    double td = 0.0;
    int estimate_td = 0;
    int rolling_shutter = 0;
    int row = 0;
    int col = 0;
    int num_of_cam = 1;
    int stereo = 0;
    int use_imu = 1;
    int multiple_thread = 0;
    map<int, Eigen::Vector3d> pts_gt;

    std::string image0_topic;
    std::string image1_topic;
    std::string fisheye_mask;
    std::vector<std::string> cam_names;
    int max_cnt = 150;
    int min_dist = 30;
    double f_threshold = 1.0;
    int show_track = 0;
    int flow_back = 0;

    // GeoDF-VINS-Hard (geometry-only dynamic feature rejection, front-end only)
    int geodf_enable = 0;
    int geodf_hard_reject = 1;
    double geodf_ransac_th_px = 1.0;
    // Threshold on Sampson SQUARED distance (num^2/denom), not unsquared Sampson.
    double geodf_sampson_th = 3.0;
    int geodf_min_track_cnt = 2;
    int geodf_min_feature_num = 40;
    double geodf_max_reject_ratio = 0.4;
    int geodf_ratio_guard = 1;
    int geodf_debug = 0;
    int geodf_dump_features = 0;
    int geodf_adaptive = 0;
    double geodf_activate_ratio = 0.12;
    double geodf_activate_ema = 0.15;
    double geodf_deactivate_frac = 0.6;
    // (B) auto-calibrated per-scene activation threshold (tracks static outlier-floor)
    int geodf_auto_rho = 0;
    double geodf_auto_mult = 1.8;
    double geodf_auto_margin = 0.05;
    double geodf_activate_ratio_min = 0.08;
    double geodf_activate_ratio_max = 0.40;
    double geodf_auto_floor_down = 0.02;
    double geodf_auto_floor_up = 0.004;
    // Track-level temporal voting: hard-delete a feature only after it has been
    // flagged dynamic on >= vote_frames consecutive frames (suppresses transient
    // false positives that hurt local accuracy / static scenes). 1 = off.
    int geodf_vote_frames = 1;
    // Skip rejection for the first warmup_frames (estimator still converging). 0 = off.
    int geodf_warmup_frames = 0;
    // (F) stereo temporal cross-check (right-view epipolar agreement)
    int geodf_stereo_check = 0;
    // Stereo Sampson SQUARED distance threshold (same units as geodf_sampson_th).
    double geodf_stereo_sampson_th = 3.0;
    // Only trust the stereo cross-check when the scene epipolar-outlier floor is
    // low (reliable geometry). 0 = always trust. In low-parallax scenes the floor
    // is high and the noisier right-view pair causes false rejections, so gate it.
    double geodf_stereo_floor_max = 0.0;
    std::string geodf_stats_path;
    std::string geodf_feat_path;

    // SAD-VINS: semantic dynamic masking (YOLO segmentation front-end)
    int sem_enable = 0;
    std::string sem_mask_topic = "/dynamic_mask";
    int sem_static_value = 255;
    std::string sem_stats_path;
    // Semantic–GeoDF fusion: gated union (OR) with independent scene activation (rejectSemGeoFused)
    int sem_geodf_fusion = 0;
    double sem_activate_ratio = 0.015;
    double sem_activate_ema = 0.15;
    double sem_deactivate_frac = 0.6;
    // 1: apply YOLO mask in setMask only when sem_scene_active (ablation vs always-on soft mask)
    int sem_mask_gated = 0;
    // Hard-delete semantic candidates only after >=k consecutive dynamic flags (fusion path)
    int sem_vote_frames = 1;
    // Adaptive Semantic-GeoDF policy:
    // 0 keeps legacy sem_mask_gated behavior.
    // 1 uses a three-state policy (static-safe / dynamic-assist / strong-dynamic)
    // driven by semantic bursts and semantic-geometry agreement.
    int sem_adaptive_policy = 0;
    // -1: online auto policy (main result). 0/1/2 are manual diagnostic
    // overrides for static/low, mid-dynamic, and high-dynamic behavior.
    int sem_policy_dynamic_level = -1;
    // A high instantaneous semantic-mask ratio arms dynamic-assist hold. This
    // separates true moving-object bursts from low-level YOLO noise on static scenes.
    double sem_policy_burst_ratio = 0.18;
    // Sustained semantic EMA above this value is treated as strong dynamic pressure.
    // Keep this high enough to avoid static-scene YOLO FP bursts.
    double sem_policy_strong_ratio = 0.20;
    // Keep the soft mask active for this many frames after dynamic evidence appears.
    int sem_policy_hold_frames = 120;
    // Semantic-geometry overlap gate: fraction of raw GeoDF outlier candidates that
    // also lie on the YOLO dynamic mask.
    double sem_policy_overlap_ratio = 0.35;
    double sem_policy_overlap_ema = 0.20;
    int sem_policy_min_geo_candidates = 2;
    // Semantic-GeoDF consensus backend weighting. Hard rejection removes the
    // highest-risk tracks first; suspicious tracks that survive the shared guard
    // stay in the estimator with reduced visual residual weight.
    int sem_geodf_backend_weight = 0;
    double sem_geodf_backend_min_weight = 0.25;
    double sem_geodf_backend_semantic_weight = 0.55;
    double sem_geodf_backend_geo_weight = 0.75;
    double sem_geodf_backend_agree_weight = 0.25;
    double sem_geodf_backend_recovery = 0.20;
    // Real-time robustness: non-blocking mask sync + Geo-only fallback when mask missing/stale
    double sem_mask_max_age_ms = 150.0;
    int sem_use_latest_mask = 1;
    int sem_block_on_mask = 0;
    std::string sem_geodf_stats_path;

    // Keep extension fields at the end to avoid shifting legacy field offsets
    // when an algorithm-only rebuild is temporarily used with an older ROS node.
    // A complete dependent-package rebuild is still recommended.
    double visual_sigma_px = 1.5;
    double visual_huber_delta = 1.0;
    // Rank tracks competing for the shared hard-rejection budget with the same
    // risk model used for backend weights. 0 preserves legacy Sampson sorting.
    int sem_geodf_rank_by_risk = 0;

    // Per-observation visual covariance proxy. The exported factor weight
    // combines LK/forward-backward/track-age quality with Semantic-GeoDF risk.
    int visual_adaptive_quality = 0;
    double visual_quality_min_weight = 0.35;
    double visual_lk_error_scale = 20.0;
    double visual_fb_error_scale = 0.5;
    int visual_quality_full_age = 4;

    // Robust scale learned from the previous optimized window's whitened
    // reprojection norms. Disabled by default for legacy configurations.
    int visual_adaptive_huber = 0;
    double visual_huber_delta_min = 0.75;
    double visual_huber_delta_max = 3.0;
    double visual_huber_k = 1.345;
    double visual_huber_ema = 0.10;
    int visual_huber_min_samples = 30;

    // Inflate preintegration process noise only for observable IMU transport
    // faults (large sample gaps or sensor saturation).
    int imu_adaptive_covariance = 0;
    double imu_gap_threshold_s = 0.015;
    double imu_acc_saturation = 80.0;
    double imu_gyr_saturation = 8.0;
    double imu_gap_inflation_gain = 3.0;
    double imu_saturation_inflation_gain = 10.0;
    double imu_max_cov_inflation = 20.0;
    std::string adaptive_factor_stats_path;

    // Prevent online extrinsic/time-offset states from opening before the
    // current window contains enough translational and visual excitation.
    int calibration_observability_gate = 1;
    double calibration_min_speed = 0.20;
    double calibration_min_parallax_px = 5.0;
    int calibration_min_tracked_features = 40;

    // Policy correctness (plan P1.1-P1.4).
    // Overlap metric and its minimum support. max(|I|/|G|, |I|/|S|) scored 0.5 when
    // two 2-element sets shared one feature, arming the policy on coincidence.
    std::string sem_policy_overlap_metric = "dice";
    int sem_policy_min_sem_candidates = 4;
    int sem_policy_min_intersection = 2;
    int sem_policy_overlap_support_saturation = 8;
    // Hold as a duration on the sensor clock. sem_policy_hold_frames is retained only
    // so older configs still load; these values are what the FSM uses.
    double sem_policy_assist_hold_s = 1.5;
    double sem_policy_strong_hold_s = 1.0;
    double sem_policy_min_state_dwell_s = 0.3;
    // Health thresholds. An unhealthy expert may not hard-reject, and low
    // observability downgrades deletion to down-weighting.
    double sem_health_mask_saturation_ratio = 0.60;
    double sem_health_min_semantic = 0.35;
    double sem_health_min_geometric = 0.35;
    double sem_health_min_observability = 0.35;
    int sem_health_redundancy_target = 60;
    double sem_health_parallax_target_px = 3.0;
    int sem_health_min_tracks_for_hard_reject = 40;
    // Per-track lifecycle. Enabled by default: hard rejection is the only
    // irreversible action, so it must pass every guard. Ablatable (policy
    // ablation P5). Inert without sem_geodf_backend_weight, which supplies risk.
    int sem_lifecycle_enable = 1;
    int sem_lifecycle_suspect_frames = 1;
    int sem_lifecycle_downweight_frames = 2;
    double sem_lifecycle_hard_reject_risk = 0.60;
    int sem_lifecycle_require_agreement = 1;
    double sem_lifecycle_recover_dwell_s = 0.5;

    // GeoDF fundamental-matrix degeneracy guard (plan P1.7). Checking only F.empty()
    // let a confidently wrong F from pure rotation or low parallax hard-reject static
    // structure.
    double geodf_min_grid_occupancy = 0.35;
    double geodf_min_median_parallax_px = 1.0;
    double geodf_max_design_condition_number = 1.0e6;
    // Nullspace gap g_F = σ8/σ9. Default 0 = telemetry only (not a hard gate).
    double geodf_min_nullspace_gap = 0.0;
    int geodf_min_ransac_inliers = 20;
    double geodf_min_ransac_inlier_ratio = 0.35;
    double geodf_max_mover_share = 0.60;

    // Stereo physical validity contract (plan P1.5/P1.6). One validated set of
    // stereo matches feeds the stereo factor, depth init, the right-camera GeoDF
    // branch and the backend weight evidence.
    int stereo_validity_enable = 1;
    double stereo_lr_cycle_max_px = 1.0;
    double stereo_epipolar_max_px = 2.0;
    double stereo_min_disparity_px = 0.5;
    double stereo_max_disparity_px = 200.0;
    double stereo_reprojection_max_px = 2.0;
    int stereo_require_positive_depth = 1;
    // Publication stereo contract: without a calibrated rig, publication mode must
    // hard-fail rather than silently admitting LK-only stereo measurements.
    int stereo_contract_enable = 1;
    int stereo_contract_require_calibration = 1;
    int stereo_contract_allow_lk_fallback = 0;
    std::string stereo_stats_path;
    // Recorded mode string for the manifest / failure status.
    std::string stereo_contract_mode = "unset";

    // Structured failure detection (plan P0.4). Enabled by default: a diverged run
    // must be reported as failed rather than emit a plausible-looking trajectory.
    int failure_detection_enable = 1;
    double failure_max_acc_bias = 2.5;
    double failure_max_gyro_bias = 1.0;
    double failure_max_translation_step_m = 5.0;
    double failure_max_rotation_step_deg = 50.0;
    int failure_min_tracked_features = 10;
    int failure_max_consecutive_low_feature_frames = 5;
    // Written once, on the first detected failure, so the run manifest can carry a
    // structured reason instead of an operator reading logs.
    std::string failure_status_path;

    void reset();
    bool loadFromYaml(const std::string &config_file);
};

VinsConfig &vinsConfig();

/** @deprecated Prefer vinsConfig().loadFromYaml() */
inline bool readParameters(const std::string &config_file)
{
    return vinsConfig().loadFromYaml(config_file);
}

enum SIZE_PARAMETERIZATION
{
    SIZE_POSE = 7,
    SIZE_SPEEDBIAS = 9,
    SIZE_FEATURE = 1
};

enum StateOrder
{
    O_P = 0,
    O_R = 3,
    O_V = 6,
    O_BA = 9,
    O_BG = 12
};

enum NoiseOrder
{
    O_AN = 0,
    O_GN = 3,
    O_AW = 6,
    O_GW = 9
};
