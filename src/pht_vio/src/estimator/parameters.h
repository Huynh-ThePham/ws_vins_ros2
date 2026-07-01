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
    double geodf_stereo_sampson_th = 3.0;
    // Only trust the stereo cross-check when the scene epipolar-outlier floor is
    // low (reliable geometry). 0 = always trust. In low-parallax scenes the floor
    // is high and the noisier right-view pair causes false rejections, so gate it.
    double geodf_stereo_floor_max = 0.0;

    // GeoDF-Inertial (GeoDF-Weighted): score features against the IMU/VINS-predicted
    // rigid-scene epipolar geometry instead of a feature-fit fundamental matrix,
    // so the rigidity reference does not collapse when dynamics dominate.
    int geodf_imu_enable = 0;             // 1: use inertial epipolar scoring when reliable
    double geodf_imu_sampson_th = 3.0;    // base Sampson threshold (pseudo-pixel^2)
    double geodf_imu_parallax_min = 0.02; // metres; below -> low-parallax (fallback/derotate)
    double geodf_imu_parallax_ref = 0.08; // metres; confidence-scaling reference baseline
    double geodf_imu_tau_cap = 4.0;       // max multiplier applied to sampson_th at low parallax
    int geodf_imu_fallback = 1;           // 1: fall back to feature-fit GeoDF when imu invalid
    int geodf_imu_derotate = 1;           // 1: gyro-derotated residual-flow gate at low parallax
    double geodf_imu_derotate_px = 3.0;   // residual-flow threshold (pseudo-pixels) for derotate mode
    // Robust per-frame scale gate: reject only residuals that clearly exceed the
    // frame's own central tendency (tau_eff' = max(tau_eff, mult * median)). A
    // whole-frame geometry error (transient bad IMU pose) inflates the median and
    // so widens the gate, preventing mass false rejection on static scenes, while
    // genuine dynamics (low static median, high outliers) are still caught.
    double geodf_imu_median_mult = 5.0;   // 0 disables the robust scale gate
    double geodf_imu_parallax_max = 1.0;  // metres; above -> pose deemed corrupted, fall back
    // Reliability skip: if the inertial gate would flag more than this fraction
    // of the frame, the rigid-scene model explains too little (transient bad
    // pose, or dynamics so dense that front-end rejection is unsafe) -> skip
    // rejection on this frame and freeze the scene EMA. 0 disables.
    double geodf_imu_max_dyn_frac = 0.5;

    // GeoDF-Hybrid (GeoDF-Weighted): reliability-gated arbitration between GeoDF-Adaptive
    // feature-fit geometry and IMU-predicted epipolar geometry. When enabled,
    // P1 is preferred while the hybrid scene signal is below the threshold;
    // inertial/derotation is used once the signal indicates dynamic density.
    int geodf_hybrid_enable = 0;
    double geodf_hybrid_inertial_floor = 0.08; // hysteresis upper threshold (floor_on)
    // Hysteresis lower threshold (floor_off): once the inertial/derotation side is
    // active it stays active until hybrid_signal drops below this. < 0 or
    // >= inertial_floor disables hysteresis (single-threshold arbitration).
    double geodf_hybrid_floor_off = -1.0;
    // Anti-chatter dwell (frames): the hybrid source latch only flips after the
    // signal stays past the threshold for this many consecutive frames, so
    // arbitration tracks the sustained dynamic regime, not single-frame spikes.
    int geodf_hybrid_dwell = 5;

    // GeoDF-Weighted (GeoDF-Weighted candidate): keep all scored features in the
    // estimator, but down-weight visual residuals by the active geometry score.
    int geodf_backend_weight = 0;
    double geodf_backend_min_weight = 0.15;
    double geodf_backend_weight_power = 2.0;
    // Temporal reliability memory for backend weighting. This turns per-frame
    // geometry residuals into a persistent dynamic belief per feature track,
    // damping one-frame false positives while keeping consistently dynamic
    // tracks weak in the estimator. 0 = original instantaneous weighting.
    int geodf_backend_temporal = 0;
    double geodf_backend_temporal_attack = 0.45;
    double geodf_backend_temporal_recovery = 0.12;
    double geodf_backend_temporal_prior = 0.0;

    std::string geodf_stats_path;
    std::string geodf_feat_path;

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
