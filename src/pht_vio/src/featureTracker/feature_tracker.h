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
    map<int, vector<pair<int, FeatureObservation>>> trackImage(double _cur_time, const cv::Mat &_img, const cv::Mat &_img1 = cv::Mat());
    void setMask();
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
    void setImuEpipolar(const Eigen::Matrix3d &R_rel, const Eigen::Vector3d &t_rel, bool valid);
    // Hybrid static-P1 gating (estimator): publish IMU epipolar to the front-end
    // only when the latch is on or imminently turning on (dwell precharge).
    bool hybridNeedImuEpipolar() const;
    double distance(cv::Point2f &pt1, cv::Point2f &pt2);
    void removeOutliers(set<int> &removePtsIds);
    cv::Mat getTrackImage();
    bool inBorder(const cv::Point2f &pt);

    int row, col;
    cv::Mat imTrack;
    cv::Mat mask;
    cv::Mat fisheye_mask;
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
    // Hysteresis latch for hybrid source arbitration (true = inertial/derot side).
    bool geo_hybrid_dynamic_active = false;
    // Consecutive-frame counter for the anti-chatter dwell on the latch above.
    int geo_hybrid_dwell_cnt = 0;
    // Track-level temporal voting state: id -> consecutive frames flagged dynamic,
    // and a frame counter for the warmup guard.
    std::map<int, int> geo_dyn_streak;
    long long geo_frame_count = 0;
    // Feature id -> visual residual weight exported to the estimator. Missing ids
    // use weight 1.0, preserving the baseline/GeoDF-Adaptive path.
    std::map<int, double> geo_feature_weights;
    // Feature id -> smoothed dynamic belief in [0, 1] for temporal backend
    // weighting. It is updated only while backend weighting is active and pruned
    // to the currently tracked ids each image.
    std::map<int, double> geo_dynamic_belief;
    // (F) stereo temporal cross-check state.
    cv::Mat cur_img1;                            // current right image (set in trackImage)
    std::map<int, cv::Point2f> prev_right_pts_map;  // id -> previous-frame right pixel

    // GeoDF-Inertial (GeoDF-Weighted): IMU/VINS-predicted prev->cur relative CAMERA
    // pose, pushed from the estimator. Used to build the rigid-scene epipolar
    // geometry without fitting a fundamental matrix to the features.
    Eigen::Matrix3d imu_R_rel = Eigen::Matrix3d::Identity();
    Eigen::Vector3d imu_t_rel = Eigen::Vector3d::Zero();
    bool imu_epi_valid = false;
    double imu_t_norm = 0.0;
};
