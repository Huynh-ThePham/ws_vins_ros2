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
    int geodf_max_reject_per_frame = 0;
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
    // Quality-aware activation: require candidate density and residual
    // separation before an adaptive frame can arm hard rejection. This protects
    // against high RANSAC-outlier ratios caused by degenerate static geometry.
    int geodf_quality_gate = 0;
    double geodf_quality_ema = 0.15;
    double geodf_quality_min = 0.5;
    double geodf_min_candidate_ratio = 0.01;
    double geodf_min_residual_lift = 2.0;
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
    // Stereo 3D motion consistency: triangulate previous stereo tracks, estimate
    // the dominant rigid motion with PnP RANSAC into the current left image, and
    // flag tracks whose reprojection residual is inconsistent with that motion.
    // This is preferred over 2D-F gating when enough stereo correspondences are
    // available.
    int geodf_motion3d_enable = 0;
    int geodf_motion3d_min_points = 25;
    double geodf_motion3d_min_depth = 0.2;
    double geodf_motion3d_max_depth = 40.0;
    double geodf_motion3d_residual_th = 3.0;
    int geodf_motion3d_ransac_iters = 96;
    double geodf_motion3d_min_2d_ratio = 0.0;
    double geodf_motion3d_arm_2d_ratio = 0.0;
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
