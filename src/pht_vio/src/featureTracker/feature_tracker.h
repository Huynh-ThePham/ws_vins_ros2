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
#include "stereo_validity.h"
#include "sem_policy.h"
#include "geodf_degeneracy.h"
#include "adaptive_lifecycle_v2.h"
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
    std::map<int, cv::Point2f> prev_right_pts_map;   // id -> right pixel at t-1 (validated)
    std::map<int, cv::Point2f> prev2_right_pts_map;  // id -> right pixel at t-2 (validated)

    // Plan P1.5/P1.6: the single source of truth for stereo measurements. Populated
    // once per frame by applyStereoValidityContract(); GeoDF reads it instead of
    // running a second, unvalidated stereo matcher of its own.
    stereo_validity::ValidityById stereo_validity_by_id;
    stereo_validity::Counters stereo_counters_frame;
    stereo_validity::Counters stereo_counters_total;
    stereo_validity::Rig stereo_rig;
    bool stereo_rig_ready = false;

    // Marks status[i] = 0 for every left-right match that fails the physical
    // contract, and refreshes stereo_validity_by_id / the frame counters.
    void applyStereoValidityContract(std::vector<uchar> &status,
                                     const std::vector<double> &fb_error);
    void logStereoValidityStats();
    stereo_validity::Config stereoValidityConfig() const;
    bool ensureStereoRig();

    // Live track reliability for active-window factor construction. Returns the
    // current Sem-GeoDF weight for `id`, or 1.0 if the track is unweighted.
    // Marginalized priors keep the weight frozen into their ResidualBlockInfo;
    // only newly built factors see this value.
    double currentFeatureWeight(int id) const
    {
        if (sem_geodf_feature_weights.empty())
            return 1.0;
        const auto it = sem_geodf_feature_weights.find(id);
        if (it == sem_geodf_feature_weights.end())
            return 1.0;
        return std::min(1.0, std::max(0.0, it->second));
    }

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
    // P1.1: support behind the overlap number, so a coincidence between two tiny
    // candidate sets can be told apart from real agreement in the logs.
    double sem_geo_overlap_support = 0.0;
    bool sem_geo_overlap_has_support = false;
    // P1.2: timestamp-driven scene policy. Replaces the frame-counted hold.
    sem_policy::PolicyFsm sem_policy_fsm;
    // P1.3/P1.4: health measured separately from the action it authorises, and a
    // per-track lifecycle so hard rejection needs risk AND redundancy AND
    // observability rather than evidence alone.
    sem_policy::Health sem_policy_health;
    sem_policy::LifecycleManager sem_track_lifecycle;
    adaptive_lifecycle_v2::Manager sem_track_lifecycle_v2;
    // Previous per-track semantic status is the temporal-semantic cue for q_s.
    // It is deliberately independent of every GeoDF value.
    std::map<int, bool> previous_semantic_status;
    // P1.7: GeoDF may not hard-reject or count as strong agreement on a degenerate
    // fundamental matrix.
    geodf_degeneracy::Result geo_degeneracy;
    long long geo_hard_reject_suppressed_frames = 0;
    int sem_policy_trigger_burst = 0;
    int sem_policy_trigger_strong = 0;
    int sem_policy_trigger_overlap = 0;
    // Feature id -> visual residual weight exported to the estimator. Tracks
    // that are semantically/geometrically suspicious but not hard-deleted are
    // kept with reduced influence in the backend.
    std::map<int, double> sem_geodf_feature_weights;
    // Per-track measurement quality for adaptive arbitration only. Always
    // computed from LK/FB/age when sem_adaptive_arbitration is on, independent
    // of visual_adaptive_quality (so Proposed does not silently enable the
    // visual-adaptation overlay key).
    std::map<int, double> arbitration_measurement_quality;

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
        // P1.7 degeneracy verdict for this frame (geodf_degeneracy::Health / ::Cause).
        int degeneracy_health = 0;
        int degeneracy_cause = 0;
        double geometry_conditioning = 0.0;
        double geometry_inlier_ratio = 0.0;
        double effective_design_condition = 0.0;
        bool design_metrics_valid = false;
        double median_parallax_px = 0.0;
        double grid_occupancy = 0.0;
        std::vector<int> confirmed;
        std::vector<int> raw_candidates;
        std::vector<double> errors;
        std::vector<double> right_err;
        std::vector<uchar> right_valid;
        cv::Mat F;
        std::vector<uchar> f_status;
    };

    bool analyzeGeoDynamic(GeoDynamicAnalysis &out);
    int applyTrackRejection(const std::vector<int> &indices,
                            GeoDynamicAnalysis *geo,
                            const std::vector<double> *priority_scores = nullptr);
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
