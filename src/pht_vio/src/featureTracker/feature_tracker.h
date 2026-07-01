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
    map<int, vector<pair<int, FeatureObservation>>> trackImage(
        double _cur_time, const cv::Mat &_img, const cv::Mat &_img1 = cv::Mat(),
        const cv::Mat &_sem_mask = cv::Mat(), double _sem_mask_lag_ms = -1.0);
    void setMask();
    void rejectSemanticDynamic();
    void rejectSemanticGeometricDynamic();
    bool isSemanticStatic(const cv::Point2f &pt) const;
    bool semanticMaskTrusted() const;
    bool applySemanticSoftMask() const;
    double computeDynamicPixelRatio(int &mask_available) const;
    void setLatestCameraGyro(double t, const Eigen::Vector3d &gyro);
    void setImuEpipolar(const Eigen::Matrix3d &R_rel, const Eigen::Vector3d &t_rel, bool valid);
    void readIntrinsicParameter(const vector<string> &calib_file);
    void showUndistortion(const string &name);
    void rejectWithF();
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
    double dynamic_scene_ema = -1.0;
    bool dynamic_scene_active = false;
    double dynamic_outlier_floor = -1.0;
    long long dynamic_frame_count = 0;
    double sgta_policy_signal_ema = -1.0;
    bool sgta_aggressive_active = false;
    int sgta_aggressive_hold = 0;
    double sem_mask_lag_ms = -1.0;
    bool sem_mask_trusted = false;
    double latest_gyro_time = -1.0;
    Eigen::Vector3d latest_cam_gyro = Eigen::Vector3d::Zero();
    Eigen::Matrix3d imu_R_rel = Eigen::Matrix3d::Identity();
    Eigen::Vector3d imu_t_rel = Eigen::Vector3d::Zero();
    bool imu_epi_valid = false;
    double imu_t_norm = 0.0;
    map<int, double> dynamic_prob;
    map<int, int> dynamic_streak;
    map<int, double> dynamic_belief;
};
