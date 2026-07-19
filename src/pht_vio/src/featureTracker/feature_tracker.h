/*******************************************************
 * Copyright (C) 2019, Aerial Robotics Group, Hong Kong University of Science and Technology
 * 
 * This file is part of VINS.
 * 
 * Licensed under the GNU General Public License v3.0;
 * you may not use this file except in compliance with the License.
 *
 * Author: Qin Tong (qintonguav@gmail.com)
 *******************************************************/

#pragma once

#include <cstdio>
#include <iostream>
#include <queue>
#include <execinfo.h>
#include <csignal>
#include <opencv2/opencv.hpp>
#include <eigen3/Eigen/Dense>

#include "camodocal/camera_models/CameraFactory.h"
#include "camodocal/camera_models/CataCamera.h"
#include "camodocal/camera_models/PinholeCamera.h"
#include "../estimator/parameters.h"
#include <pht_slam_common/tic_toc.hpp>

using namespace std;
using namespace camodocal;
using namespace Eigen;

bool inBorder(const cv::Point2f &pt);
void reduceVector(vector<cv::Point2f> &v, vector<uchar> status);
void reduceVector(vector<int> &v, vector<uchar> status);

class FeatureTracker
{
public:
    FeatureTracker();
    map<int, vector<pair<int, FeatureObservation>>> trackImage(double _cur_time, const cv::Mat &_img, const cv::Mat &_img1 = cv::Mat(), const cv::Mat &_sem_mask = cv::Mat(), double _sem_mask_lag_ms = -1.0);
    void setMask();
    void rejectSemanticDynamic();
    void rejectSemGeoFused();
    bool isSemanticStatic(const cv::Point2f &pt) const;
    bool applySemanticSoftMask() const;
    bool applySemanticHardReject() const;
    double computeDynamicPixelRatio(int &mask_available) const;
    void updateSemanticSceneGate();
    void readIntrinsicParameter(const vector<string> &calib_file);
    void showUndistortion(const string &name);
    void rejectWithF();
    void rejectGeoDynamic();
    void undistortedPoints();
    vector<cv::Point2f> undistortedPts(vector<cv::Point2f> &pts, camodocal::CameraPtr cam);
    vector<cv::Point2f> ptsVelocity(vector<int> &ids, vector<cv::Point2f> &pts, 
                                    map<int, cv::Point2f> &cur_id_pts, map<int, cv::Point2f> &prev_id_pts);
    void showTwoImage(const cv::Mat &img1, const cv::Mat &img2, 
                      vector<cv::Point2f> pts1, vector<cv::Point2f> pts2);
    void drawTrack(const cv::Mat &imLeft, const cv::Mat &imRight, 
                                   vector<int> &curLeftIds,
                                   vector<cv::Point2f> &curLeftPts, 
                                   vector<cv::Point2f> &curRightPts,
                                   map<int, cv::Point2f> &prevLeftPtsMap);
    void setPrediction(map<int, Eigen::Vector3d> &predictPts);
    double distance(cv::Point2f &pt1, cv::Point2f &pt2);
    void removeOutliers(set<int> &removePtsIds);
    cv::Mat getTrackImage();
    bool inBorder(const cv::Point2f &pt);

    int row, col;
    cv::Mat imTrack;
    cv::Mat mask;
    cv::Mat fisheye_mask;
    cv::Mat sem_mask;
    cv::Mat prev_img, cur_img;
    vector<cv::Point2f> n_pts;
    vector<cv::Point2f> predict_pts;
    vector<cv::Point2f> predict_pts_debug;
    vector<cv::Point2f> prev_pts, cur_pts, cur_right_pts;
    vector<cv::Point2f> prev_un_pts, cur_un_pts, cur_un_right_pts;
    vector<cv::Point2f> pts_velocity, right_pts_velocity;
    vector<int> ids, ids_right;
    vector<int> track_cnt;
    map<int, cv::Point2f> cur_un_pts_map, prev_un_pts_map;
    map<int, cv::Point2f> cur_un_right_pts_map, prev_un_right_pts_map;
    map<int, cv::Point2f> prevLeftPtsMap;
    vector<camodocal::CameraPtr> m_camera;
    double cur_time;
    double prev_time;
    bool stereo_cam;
    int n_id;
    bool hasPrediction;

    // GeoDF scene-aware activation state (EMA of frame epipolar-outlier ratio).
    double geo_activation_ema = -1.0;
    bool geo_activation_active = false;
    // (B) running estimate of the static epipolar-outlier floor (for auto rho_on).
    double geo_outlier_floor = -1.0;
    // Track-level temporal voting state: id -> consecutive frames flagged dynamic,
    // and a frame counter for the warmup guard.
    std::map<int, int> geo_dyn_streak;
    long long geo_frame_count = 0;
    // (F) stereo temporal cross-check state.
    cv::Mat cur_img1;                            // current right image (set in trackImage)
    std::map<int, cv::Point2f> prev_right_pts_map;  // id -> previous-frame right pixel

    // SAD-VINS scene-aware semantic activation (EMA of dynamic pixel ratio).
    double sem_activation_ema = -1.0;
    bool sem_scene_active = false;
    bool sem_mask_trusted = false;
    double sem_mask_lag_ms = -1.0;
    std::map<int, int> sem_dyn_streak;
    // Adaptive Semantic-GeoDF policy:
    // 0=static-safe, 1=dynamic-assist, 2=strong-dynamic.
    int sem_policy_state = 0;
    int sem_policy_hold = 0;
    bool sem_policy_soft_mask_active = false;
    bool sem_policy_hard_reject_active = false;
    double sem_geo_overlap_ema = -1.0;
    double sem_geo_overlap_last = 0.0;
    int sem_policy_trigger_burst = 0;
    int sem_policy_trigger_strong = 0;
    int sem_policy_trigger_overlap = 0;
    // Feature id -> visual residual weight exported to the estimator. Tracks
    // that are semantically/geometrically suspicious but not hard-deleted are
    // kept with reduced influence in the backend.
    std::map<int, double> sem_geodf_feature_weights;

    struct GeoDynamicAnalysis
    {
        bool valid = false;
        int total = 0;
        int scored = 0;
        int ransac_outliers = 0;
        int sampson_above_th = 0;
        int frame_active = 1;
        int guard_triggered = 0;
        int guard_capped = 0;
        size_t candidates_raw = 0;
        size_t confirmed_n = 0;
        int stereo_added = 0;
        double mean_sampson = 0.0;
        double median_sampson = 0.0;
        double max_sampson = 0.0;
        double rho_on = 0.0;
        double geo_ms = 0.0;
        std::vector<int> confirmed;
        std::vector<int> raw_candidates;
        std::vector<double> errors;
        std::vector<double> right_err;
        std::vector<uchar> right_valid;
        cv::Mat F;
        std::vector<uchar> f_status;
    };

    bool analyzeGeoDynamic(GeoDynamicAnalysis &out);
    int applyTrackRejection(const std::vector<int> &indices, GeoDynamicAnalysis *geo);
    void logGeoDynamicStats(const GeoDynamicAnalysis &analysis, int rejected);
    void updateSemanticAdaptivePolicy(double dynamic_pixel_ratio,
                                      int mask_available,
                                      const GeoDynamicAnalysis *geo);
    void collectSemanticRawCandidates(std::vector<int> &sem_raw) const;
    bool semanticHardRejectArmed() const;
    int confirmSemanticCandidates(const std::vector<int> &sem_raw,
                                  std::vector<int> &confirmed,
                                  bool update_streak);
    int applySemanticCandidateRejection(const std::vector<int> &confirmed);
};
