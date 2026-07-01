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

#include "feature_tracker.h"
#include <opencv2/imgproc/imgproc_c.h>
#include <fstream>
#include <cmath>
#include <set>
#include <algorithm>

namespace {

// OpenCV/HZ epipolar: x'^T F x = 0 with x=points1 (cur), x'=points2 (prev).
// p1=x, p2=x' => Sampson^2 = (x'^T F x)^2 / (||F x||_{1:2}^2 + ||F^T x'||_{1:2}^2).
double sampsonDistance(const cv::Mat &F, const cv::Point2f &p1, const cv::Point2f &p2)
{
    const double f11 = F.at<double>(0, 0), f12 = F.at<double>(0, 1), f13 = F.at<double>(0, 2);
    const double f21 = F.at<double>(1, 0), f22 = F.at<double>(1, 1), f23 = F.at<double>(1, 2);
    const double f31 = F.at<double>(2, 0), f32 = F.at<double>(2, 1), f33 = F.at<double>(2, 2);

    const double x1 = p1.x, y1 = p1.y;
    const double x2 = p2.x, y2 = p2.y;

    const double Fx1x = f11 * x1 + f12 * y1 + f13;
    const double Fx1y = f21 * x1 + f22 * y1 + f23;
    const double Ftx2x = f11 * x2 + f21 * y2 + f31;
    const double Ftx2y = f12 * x2 + f22 * y2 + f32;

    const double num = x2 * Fx1x + y2 * Fx1y + f31 * x1 + f32 * y1 + f33;
    const double denom = Fx1x * Fx1x + Fx1y * Fx1y + Ftx2x * Ftx2x + Ftx2y * Ftx2y;
    if (denom < 1e-12)
        return 0.0;
    return (num * num) / denom;
}

}  // namespace

bool FeatureTracker::inBorder(const cv::Point2f &pt)
{
    const int BORDER_SIZE = 1;
    int img_x = cvRound(pt.x);
    int img_y = cvRound(pt.y);
    return BORDER_SIZE <= img_x && img_x < col - BORDER_SIZE && BORDER_SIZE <= img_y && img_y < row - BORDER_SIZE;
}

double distance(cv::Point2f pt1, cv::Point2f pt2)
{
    //printf("pt1: %f %f pt2: %f %f\n", pt1.x, pt1.y, pt2.x, pt2.y);
    double dx = pt1.x - pt2.x;
    double dy = pt1.y - pt2.y;
    return sqrt(dx * dx + dy * dy);
}

void reduceVector(vector<cv::Point2f> &v, vector<uchar> status)
{
    int j = 0;
    for (int i = 0; i < int(v.size()); i++)
        if (status[i])
            v[j++] = v[i];
    v.resize(j);
}

void reduceVector(vector<int> &v, vector<uchar> status)
{
    int j = 0;
    for (int i = 0; i < int(v.size()); i++)
        if (status[i])
            v[j++] = v[i];
    v.resize(j);
}

FeatureTracker::FeatureTracker()
{
    stereo_cam = 0;
    n_id = 0;
    hasPrediction = false;
}

bool FeatureTracker::isSemanticStatic(const cv::Point2f &pt) const
{
    if (!vinsConfig().sem_enable || sem_mask.empty())
        return true;
    const int x = cvRound(pt.x);
    const int y = cvRound(pt.y);
    if (x < 0 || y < 0 || x >= sem_mask.cols || y >= sem_mask.rows)
        return false;
    return sem_mask.at<uchar>(y, x) >= static_cast<uchar>(vinsConfig().sem_static_value);
}

bool FeatureTracker::applySemanticSoftMask() const
{
    auto &cfg = vinsConfig();
    if (!cfg.sem_enable || sem_mask.empty() || !sem_mask_trusted)
        return false;
    if (cfg.sem_adaptive_policy)
        return sem_policy_soft_mask_active;
    return !cfg.sem_mask_gated || sem_scene_active;
}

bool FeatureTracker::applySemanticHardReject() const
{
    auto &cfg = vinsConfig();
    if (!cfg.sem_enable || sem_mask.empty() || !sem_mask_trusted)
        return false;
    if (cfg.sem_adaptive_policy)
        return sem_policy_hard_reject_active;
    return sem_scene_active;
}

double FeatureTracker::computeDynamicPixelRatio(int &mask_available) const
{
    mask_available = sem_mask.empty() ? 0 : 1;
    if (!mask_available)
        return 0.0;

    cv::Mat sized;
    if (sem_mask.size() != cv::Size(col, row))
        cv::resize(sem_mask, sized, cv::Size(col, row), 0, 0, cv::INTER_NEAREST);
    else
        sized = sem_mask;

    int dynamic_pixels = 0;
    for (int y = 0; y < sized.rows; y++)
    {
        const uchar *row_ptr = sized.ptr<uchar>(y);
        for (int x = 0; x < sized.cols; x++)
        {
            if (row_ptr[x] < static_cast<uchar>(vinsConfig().sem_static_value))
                dynamic_pixels++;
        }
    }
    return static_cast<double>(dynamic_pixels) / static_cast<double>(sized.total());
}

void FeatureTracker::updateSemanticSceneGate()
{
    auto &cfg = vinsConfig();
    if (!cfg.sem_enable)
        return;

    int mask_available = 0;
    const double dynamic_pixel_ratio = computeDynamicPixelRatio(mask_available);
    if (sem_activation_ema < 0.0)
        sem_activation_ema = dynamic_pixel_ratio;
    else
        sem_activation_ema = cfg.sem_activate_ema * dynamic_pixel_ratio +
                               (1.0 - cfg.sem_activate_ema) * sem_activation_ema;

    const double sem_rho_off = cfg.sem_activate_ratio * cfg.sem_deactivate_frac;
    sem_scene_active = sem_scene_active ? (sem_activation_ema >= sem_rho_off)
                                        : (sem_activation_ema >= cfg.sem_activate_ratio);
}

void FeatureTracker::updateSemanticAdaptivePolicy(double dynamic_pixel_ratio,
                                                  int mask_available,
                                                  const GeoDynamicAnalysis *geo)
{
    auto &cfg = vinsConfig();
    if (!cfg.sem_adaptive_policy)
    {
        sem_policy_state = sem_scene_active ? 1 : 0;
        sem_policy_hold = 0;
        sem_policy_soft_mask_active = applySemanticSoftMask();
        sem_policy_hard_reject_active = sem_scene_active;
        sem_geo_overlap_last = 0.0;
        return;
    }

    if (!cfg.sem_enable || !mask_available || !sem_mask_trusted)
    {
        sem_policy_state = 0;
        sem_policy_hold = 0;
        sem_policy_soft_mask_active = false;
        sem_policy_hard_reject_active = false;
        sem_geo_overlap_last = 0.0;
        return;
    }

    if (cfg.sem_policy_dynamic_level >= 0)
    {
        sem_geo_overlap_last = 0.0;
        if (sem_geo_overlap_ema < 0.0)
            sem_geo_overlap_ema = 0.0;

        if (cfg.sem_policy_dynamic_level == 0)
        {
            // Manual static/low override: protect feature supply from YOLO FP.
            sem_policy_state = 0;
            sem_policy_hold = 0;
            sem_policy_soft_mask_active = sem_scene_active;
            sem_policy_hard_reject_active = false;
            return;
        }
        if (cfg.sem_policy_dynamic_level == 1)
        {
            // Manual mid-dynamic override: recover legacy default soft-mask recall
            // from the beginning of the run; hard reject remains scene-gated.
            sem_policy_state = 1;
            sem_policy_hold = std::max(0, cfg.sem_policy_hold_frames);
            sem_policy_soft_mask_active = true;
            sem_policy_hard_reject_active = sem_scene_active;
            return;
        }

        // Manual high-dynamic override: keep semantic hard reject armed when the
        // scene gate is active, while soft masking remains gated to avoid over-mask.
        sem_policy_state = 2;
        sem_policy_hold = std::max(0, cfg.sem_policy_hold_frames);
        sem_policy_soft_mask_active = sem_scene_active;
        sem_policy_hard_reject_active = sem_scene_active;
        return;
    }

    int geo_overlap_hits = 0;
    int geo_overlap_total = 0;
    sem_policy_trigger_burst = 0;
    sem_policy_trigger_strong = 0;
    sem_policy_trigger_overlap = 0;
    const bool geo_frame_active = geo && geo->valid && geo->frame_active;
    if (geo_frame_active)
    {
        geo_overlap_total = static_cast<int>(geo->confirmed.size());
        for (int idx : geo->confirmed)
        {
            if (0 <= idx && idx < static_cast<int>(cur_pts.size()) && !isSemanticStatic(cur_pts[idx]))
                geo_overlap_hits++;
        }
    }

    sem_geo_overlap_last = geo_overlap_total > 0
                               ? static_cast<double>(geo_overlap_hits) / geo_overlap_total
                               : 0.0;
    if (sem_geo_overlap_ema < 0.0)
        sem_geo_overlap_ema = sem_geo_overlap_last;
    else
        sem_geo_overlap_ema = cfg.sem_policy_overlap_ema * sem_geo_overlap_last +
                              (1.0 - cfg.sem_policy_overlap_ema) * sem_geo_overlap_ema;

    const bool semantic_burst = dynamic_pixel_ratio >= cfg.sem_policy_burst_ratio;
    const bool semantic_strong = sem_activation_ema >= cfg.sem_policy_strong_ratio;
    const bool semantic_geo_agree =
        geo_frame_active &&
        geo_overlap_total >= std::max(1, cfg.sem_policy_min_geo_candidates) &&
        sem_geo_overlap_ema >= cfg.sem_policy_overlap_ratio;

    sem_policy_trigger_burst = semantic_burst ? 1 : 0;
    sem_policy_trigger_strong = semantic_strong ? 1 : 0;
    sem_policy_trigger_overlap = semantic_geo_agree ? 1 : 0;

    if (semantic_burst || semantic_strong || semantic_geo_agree)
        sem_policy_hold = std::max(0, cfg.sem_policy_hold_frames);
    else if (sem_policy_hold > 0)
        sem_policy_hold--;

    const bool dynamic_assist = sem_policy_hold > 0;
    if (semantic_strong || (dynamic_assist && geo_frame_active))
        sem_policy_state = 2;  // strong-dynamic: full OR fusion remains armed.
    else if (dynamic_assist)
        sem_policy_state = 1;  // dynamic-assist: hold soft mask across intermittent motion.
    else
        sem_policy_state = 0;  // static-safe: suppress semantic hard reject.

    // Static-safe still allows sem_scene_active-gated soft masking, which preserves
    // the observed 0_none fix, while assist/strong temporarily behave like the
    // default always-on soft mask around real dynamic bursts.
    sem_policy_soft_mask_active = sem_scene_active || dynamic_assist;
    sem_policy_hard_reject_active = sem_scene_active && (sem_policy_state > 0);
}

void FeatureTracker::collectSemanticRawCandidates(std::vector<int> &sem_raw) const
{
    sem_raw.clear();
    if (!vinsConfig().sem_enable || sem_mask.empty() || !sem_mask_trusted)
        return;

    const int total = static_cast<int>(cur_pts.size());
    sem_raw.reserve(total);
    for (int i = 0; i < total; i++)
    {
        if (!isSemanticStatic(cur_pts[i]))
            sem_raw.push_back(i);
    }
}

bool FeatureTracker::semanticHardRejectArmed() const
{
    auto &cfg = vinsConfig();
    if (!cfg.sem_enable || sem_mask.empty() || !sem_mask_trusted)
        return false;
    if (cfg.sem_geodf_fusion && cfg.sem_adaptive_policy)
        return applySemanticHardReject();
    if (cfg.sem_mask_gated)
        return sem_scene_active;
    return true;
}

int FeatureTracker::confirmSemanticCandidates(const std::vector<int> &sem_raw,
                                              std::vector<int> &confirmed,
                                              bool update_streak)
{
    confirmed.clear();
    auto &cfg = vinsConfig();
    const int total = static_cast<int>(cur_pts.size());
    const int vote_k = std::max(1, cfg.sem_vote_frames);
    const bool have_ids = (static_cast<int>(ids.size()) == total);

    if (update_streak)
    {
        std::map<int, int> next_streak;
        if (have_ids)
        {
            for (int idx : sem_raw)
            {
                const int id = ids[idx];
                const auto it = sem_dyn_streak.find(id);
                next_streak[id] = (it != sem_dyn_streak.end() ? it->second : 0) + 1;
            }
            sem_dyn_streak.swap(next_streak);
        }
        else
        {
            sem_dyn_streak.clear();
        }
    }

    for (int idx : sem_raw)
    {
        if (vote_k <= 1 || !have_ids)
        {
            confirmed.push_back(idx);
            continue;
        }
        const auto it = sem_dyn_streak.find(ids[idx]);
        if (it != sem_dyn_streak.end() && it->second >= vote_k)
            confirmed.push_back(idx);
    }
    return static_cast<int>(confirmed.size());
}

int FeatureTracker::applySemanticCandidateRejection(const std::vector<int> &confirmed)
{
    if (confirmed.empty())
        return 0;

    const int total = static_cast<int>(cur_pts.size());
    vector<uchar> keep(total, 1);
    int rejected = 0;
    for (int idx : confirmed)
    {
        if (0 <= idx && idx < total && keep[idx])
        {
            keep[idx] = 0;
            rejected++;
        }
    }
    if (rejected > 0)
    {
        vector<uchar> status(keep.begin(), keep.end());
        reduceVector(prev_pts, status);
        reduceVector(cur_pts, status);
        reduceVector(ids, status);
        reduceVector(track_cnt, status);
    }
    return rejected;
}

void FeatureTracker::rejectSemanticDynamic()
{
    auto &cfg = vinsConfig();
    if (!cfg.sem_enable || cur_pts.empty())
        return;

    const int total = static_cast<int>(cur_pts.size());
    int mask_available = 0;
    const double dynamic_pixel_ratio = computeDynamicPixelRatio(mask_available);

    int rejected = 0;
    int sem_candidates = 0;
    int sem_confirmed = 0;
    if (semanticHardRejectArmed() && mask_available)
    {
        std::vector<int> sem_raw;
        collectSemanticRawCandidates(sem_raw);
        sem_candidates = static_cast<int>(sem_raw.size());
        std::vector<int> confirmed;
        sem_confirmed = confirmSemanticCandidates(sem_raw, confirmed, true);
        rejected = applySemanticCandidateRejection(confirmed);
    }
    else
    {
        sem_dyn_streak.clear();
    }

    if (!cfg.sem_stats_path.empty())
    {
        std::ofstream sem_stats(cfg.sem_stats_path, std::ios::app);
        const double ratio = total > 0 ? static_cast<double>(rejected) / total : 0.0;
        sem_stats << static_cast<long long>(cur_time * 1e9) << ","
                  << total << "," << rejected << "," << ratio << ","
                  << static_cast<int>(cur_pts.size()) << ","
                  << mask_available << "," << dynamic_pixel_ratio << ","
                  << sem_candidates << "," << sem_confirmed << "\n";
    }
}

void FeatureTracker::setMask()
{
    mask = cv::Mat(row, col, CV_8UC1, cv::Scalar(255));
    if (applySemanticSoftMask())
    {
        if (sem_mask.size() != mask.size())
            cv::resize(sem_mask, sem_mask, mask.size(), 0, 0, cv::INTER_NEAREST);
        cv::bitwise_and(mask, sem_mask, mask);
    }

    // prefer to keep features that are tracked for long time
    vector<pair<int, pair<cv::Point2f, int>>> cnt_pts_id;

    for (unsigned int i = 0; i < cur_pts.size(); i++)
        cnt_pts_id.push_back(make_pair(track_cnt[i], make_pair(cur_pts[i], ids[i])));

    sort(cnt_pts_id.begin(), cnt_pts_id.end(), [](const pair<int, pair<cv::Point2f, int>> &a, const pair<int, pair<cv::Point2f, int>> &b)
         {
            return a.first > b.first;
         });

    cur_pts.clear();
    ids.clear();
    track_cnt.clear();

    for (auto &it : cnt_pts_id)
    {
        const bool sem_ok = !applySemanticSoftMask() || isSemanticStatic(it.second.first);
        if (mask.at<uchar>(it.second.first) == 255 && sem_ok)
        {
            cur_pts.push_back(it.second.first);
            ids.push_back(it.second.second);
            track_cnt.push_back(it.first);
            cv::circle(mask, it.second.first, vinsConfig().min_dist, 0, -1);
        }
    }
}

double FeatureTracker::distance(cv::Point2f &pt1, cv::Point2f &pt2)
{
    //printf("pt1: %f %f pt2: %f %f\n", pt1.x, pt1.y, pt2.x, pt2.y);
    double dx = pt1.x - pt2.x;
    double dy = pt1.y - pt2.y;
    return sqrt(dx * dx + dy * dy);
}

map<int, vector<pair<int, Eigen::Matrix<double, 7, 1>>>> FeatureTracker::trackImage(double _cur_time, const cv::Mat &_img, const cv::Mat &_img1, const cv::Mat &_sem_mask, double _sem_mask_lag_ms)
{
    TicToc t_r;
    cur_time = _cur_time;
    cur_img = _img;
    cur_img1 = _img1;  // (F) keep right image accessible to rejectGeoDynamic()
    row = cur_img.rows;
    col = cur_img.cols;
    sem_mask = _sem_mask;
    sem_mask_lag_ms = _sem_mask_lag_ms;
    sem_mask_trusted = false;
    if (vinsConfig().sem_enable && !sem_mask.empty())
    {
        if (sem_mask.size() != cur_img.size())
            cv::resize(sem_mask, sem_mask, cur_img.size(), 0, 0, cv::INTER_NEAREST);
        if (sem_mask_lag_ms < 0.0)
            sem_mask_trusted = true;
        else
            sem_mask_trusted = sem_mask_lag_ms <= vinsConfig().sem_mask_max_age_ms;
    }
    cv::Mat rightImg = _img1;
    /*
    {
        cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(3.0, cv::Size(8, 8));
        clahe->apply(cur_img, cur_img);
        if(!rightImg.empty())
            clahe->apply(rightImg, rightImg);
    }
    */
    cur_pts.clear();

    if (prev_pts.size() > 0)
    {
        TicToc t_o;
        vector<uchar> status;
        vector<float> err;
        if(hasPrediction)
        {
            cur_pts = predict_pts;
            cv::calcOpticalFlowPyrLK(prev_img, cur_img, prev_pts, cur_pts, status, err, cv::Size(21, 21), 1, 
            cv::TermCriteria(cv::TermCriteria::COUNT+cv::TermCriteria::EPS, 30, 0.01), cv::OPTFLOW_USE_INITIAL_FLOW);
            
            int succ_num = 0;
            for (size_t i = 0; i < status.size(); i++)
            {
                if (status[i])
                    succ_num++;
            }
            if (succ_num < 10)
               cv::calcOpticalFlowPyrLK(prev_img, cur_img, prev_pts, cur_pts, status, err, cv::Size(21, 21), 3);
        }
        else
            cv::calcOpticalFlowPyrLK(prev_img, cur_img, prev_pts, cur_pts, status, err, cv::Size(21, 21), 3);
        // reverse check
        if(vinsConfig().flow_back)
        {
            vector<uchar> reverse_status;
            vector<cv::Point2f> reverse_pts = prev_pts;
            cv::calcOpticalFlowPyrLK(cur_img, prev_img, cur_pts, reverse_pts, reverse_status, err, cv::Size(21, 21), 1, 
            cv::TermCriteria(cv::TermCriteria::COUNT+cv::TermCriteria::EPS, 30, 0.01), cv::OPTFLOW_USE_INITIAL_FLOW);
            //cv::calcOpticalFlowPyrLK(cur_img, prev_img, cur_pts, reverse_pts, reverse_status, err, cv::Size(21, 21), 3); 
            for(size_t i = 0; i < status.size(); i++)
            {
                if(status[i] && reverse_status[i] && distance(prev_pts[i], reverse_pts[i]) <= 0.5)
                {
                    status[i] = 1;
                }
                else
                    status[i] = 0;
            }
        }
        
        for (int i = 0; i < int(cur_pts.size()); i++)
            if (status[i] && !inBorder(cur_pts[i]))
                status[i] = 0;
        reduceVector(prev_pts, status);
        reduceVector(cur_pts, status);
        reduceVector(ids, status);
        reduceVector(track_cnt, status);
        ROS_DEBUG("temporal optical flow costs: %fms", t_o.toc());
        //printf("track cnt %d\n", (int)ids.size());
    }

    if (vinsConfig().sem_enable)
        updateSemanticSceneGate();

    if (vinsConfig().geodf_enable && vinsConfig().sem_enable && vinsConfig().sem_geodf_fusion)
        rejectSemGeoFused();
    else
    {
        if (vinsConfig().geodf_enable)
            rejectGeoDynamic();
        if (vinsConfig().sem_enable)
            rejectSemanticDynamic();
    }

    for (auto &n : track_cnt)
        n++;

    if (1)
    {
        //rejectWithF();
        ROS_DEBUG("set mask begins");
        TicToc t_m;
        setMask();
        ROS_DEBUG("set mask costs %fms", t_m.toc());

        ROS_DEBUG("detect feature begins");
        TicToc t_t;
        int n_max_cnt = vinsConfig().max_cnt - static_cast<int>(cur_pts.size());
        if (n_max_cnt > 0)
        {
            if(mask.empty())
                cout << "mask is empty " << endl;
            if (mask.type() != CV_8UC1)
                cout << "mask type wrong " << endl;
            cv::goodFeaturesToTrack(cur_img, n_pts, vinsConfig().max_cnt - cur_pts.size(), 0.01, vinsConfig().min_dist, mask);
        }
        else
            n_pts.clear();
        ROS_DEBUG("detect feature costs: %f ms", t_t.toc());

        for (auto &p : n_pts)
        {
            cur_pts.push_back(p);
            ids.push_back(n_id++);
            track_cnt.push_back(1);
        }
        //printf("feature cnt after add %d\n", (int)ids.size());
    }

    cur_un_pts = undistortedPts(cur_pts, m_camera[0]);
    pts_velocity = ptsVelocity(ids, cur_un_pts, cur_un_pts_map, prev_un_pts_map);

    if(!_img1.empty() && stereo_cam)
    {
        ids_right.clear();
        cur_right_pts.clear();
        cur_un_right_pts.clear();
        right_pts_velocity.clear();
        cur_un_right_pts_map.clear();
        if(!cur_pts.empty())
        {
            //printf("stereo image; track feature on right image\n");
            vector<cv::Point2f> reverseLeftPts;
            vector<uchar> status, statusRightLeft;
            vector<float> err;
            // cur left ---- cur right
            cv::calcOpticalFlowPyrLK(cur_img, rightImg, cur_pts, cur_right_pts, status, err, cv::Size(21, 21), 3);
            // reverse check cur right ---- cur left
            if(vinsConfig().flow_back)
            {
                cv::calcOpticalFlowPyrLK(rightImg, cur_img, cur_right_pts, reverseLeftPts, statusRightLeft, err, cv::Size(21, 21), 3);
                for(size_t i = 0; i < status.size(); i++)
                {
                    if(status[i] && statusRightLeft[i] && inBorder(cur_right_pts[i]) && distance(cur_pts[i], reverseLeftPts[i]) <= 0.5)
                        status[i] = 1;
                    else
                        status[i] = 0;
                }
            }

            ids_right = ids;
            reduceVector(cur_right_pts, status);
            reduceVector(ids_right, status);
            // only keep left-right pts
            /*
            reduceVector(cur_pts, status);
            reduceVector(ids, status);
            reduceVector(track_cnt, status);
            reduceVector(cur_un_pts, status);
            reduceVector(pts_velocity, status);
            */
            cur_un_right_pts = undistortedPts(cur_right_pts, m_camera[1]);
            right_pts_velocity = ptsVelocity(ids_right, cur_un_right_pts, cur_un_right_pts_map, prev_un_right_pts_map);
        }
        prev_un_right_pts_map = cur_un_right_pts_map;
        // (F) store id -> right pixel for next-frame stereo temporal cross-check.
        prev_right_pts_map.clear();
        for (size_t i = 0; i < ids_right.size(); i++)
            prev_right_pts_map[ids_right[i]] = cur_right_pts[i];
    }
    if(vinsConfig().show_track)
        drawTrack(cur_img, rightImg, ids, cur_pts, cur_right_pts, prevLeftPtsMap);

    prev_img = cur_img;
    prev_pts = cur_pts;
    prev_un_pts = cur_un_pts;
    prev_un_pts_map = cur_un_pts_map;
    prev_time = cur_time;
    hasPrediction = false;

    prevLeftPtsMap.clear();
    for(size_t i = 0; i < cur_pts.size(); i++)
        prevLeftPtsMap[ids[i]] = cur_pts[i];

    map<int, vector<pair<int, Eigen::Matrix<double, 7, 1>>>> featureFrame;
    for (size_t i = 0; i < ids.size(); i++)
    {
        int feature_id = ids[i];
        double x, y ,z;
        x = cur_un_pts[i].x;
        y = cur_un_pts[i].y;
        z = 1;
        double p_u, p_v;
        p_u = cur_pts[i].x;
        p_v = cur_pts[i].y;
        int camera_id = 0;
        double velocity_x, velocity_y;
        velocity_x = pts_velocity[i].x;
        velocity_y = pts_velocity[i].y;
        Eigen::Matrix<double, 7, 1> xyz_uv_velocity;
        xyz_uv_velocity << x, y, z, p_u, p_v, velocity_x, velocity_y;
        featureFrame[feature_id].emplace_back(camera_id,  xyz_uv_velocity);
    }

    if (!_img1.empty() && stereo_cam)
    {
        for (size_t i = 0; i < ids_right.size(); i++)
        {
            int feature_id = ids_right[i];
            double x, y ,z;
            x = cur_un_right_pts[i].x;
            y = cur_un_right_pts[i].y;
            z = 1;
            double p_u, p_v;
            p_u = cur_right_pts[i].x;
            p_v = cur_right_pts[i].y;
            int camera_id = 1;
            double velocity_x, velocity_y;
            velocity_x = right_pts_velocity[i].x;
            velocity_y = right_pts_velocity[i].y;
            Eigen::Matrix<double, 7, 1> xyz_uv_velocity;
            xyz_uv_velocity << x, y, z, p_u, p_v, velocity_x, velocity_y;
            featureFrame[feature_id].emplace_back(camera_id,  xyz_uv_velocity);
        }
    }

    //printf("feature track whole time %f\n", t_r.toc());
    return featureFrame;
}

void FeatureTracker::rejectWithF()
{
    if (cur_pts.size() >= 8)
    {
        ROS_DEBUG("FM ransac begins");
        TicToc t_f;
        vector<cv::Point2f> un_cur_pts(cur_pts.size()), un_prev_pts(prev_pts.size());
        for (unsigned int i = 0; i < cur_pts.size(); i++)
        {
            Eigen::Vector3d tmp_p;
            m_camera[0]->liftProjective(Eigen::Vector2d(cur_pts[i].x, cur_pts[i].y), tmp_p);
            tmp_p.x() = FOCAL_LENGTH * tmp_p.x() / tmp_p.z() + col / 2.0;
            tmp_p.y() = FOCAL_LENGTH * tmp_p.y() / tmp_p.z() + row / 2.0;
            un_cur_pts[i] = cv::Point2f(tmp_p.x(), tmp_p.y());

            m_camera[0]->liftProjective(Eigen::Vector2d(prev_pts[i].x, prev_pts[i].y), tmp_p);
            tmp_p.x() = FOCAL_LENGTH * tmp_p.x() / tmp_p.z() + col / 2.0;
            tmp_p.y() = FOCAL_LENGTH * tmp_p.y() / tmp_p.z() + row / 2.0;
            un_prev_pts[i] = cv::Point2f(tmp_p.x(), tmp_p.y());
        }

        vector<uchar> status;
        cv::findFundamentalMat(un_cur_pts, un_prev_pts, cv::FM_RANSAC, vinsConfig().f_threshold, 0.99, status);
        int size_a = cur_pts.size();
        reduceVector(prev_pts, status);
        reduceVector(cur_pts, status);
        reduceVector(cur_un_pts, status);
        reduceVector(ids, status);
        reduceVector(track_cnt, status);
        ROS_DEBUG("FM ransac: %d -> %lu: %f", size_a, cur_pts.size(), 1.0 * cur_pts.size() / size_a);
        ROS_DEBUG("FM ransac costs: %fms", t_f.toc());
    }
}

bool FeatureTracker::analyzeGeoDynamic(GeoDynamicAnalysis &out)
{
    out = GeoDynamicAnalysis{};
    auto &cfg = vinsConfig();
    if (!cfg.geodf_enable || !cfg.geodf_hard_reject)
        return false;

    const int total = static_cast<int>(cur_pts.size());
    out.total = total;
    if (total < 8 || prev_pts.size() != cur_pts.size())
        return false;
    if (total < cfg.geodf_min_feature_num)
        return false;

    TicToc t_geo;
    vector<cv::Point2f> un_cur_pts(total), un_prev_pts(total);
    for (int i = 0; i < total; i++)
    {
        Eigen::Vector3d tmp_p;
        m_camera[0]->liftProjective(Eigen::Vector2d(cur_pts[i].x, cur_pts[i].y), tmp_p);
        tmp_p.x() = FOCAL_LENGTH * tmp_p.x() / tmp_p.z() + col / 2.0;
        tmp_p.y() = FOCAL_LENGTH * tmp_p.y() / tmp_p.z() + row / 2.0;
        un_cur_pts[i] = cv::Point2f(tmp_p.x(), tmp_p.y());

        m_camera[0]->liftProjective(Eigen::Vector2d(prev_pts[i].x, prev_pts[i].y), tmp_p);
        tmp_p.x() = FOCAL_LENGTH * tmp_p.x() / tmp_p.z() + col / 2.0;
        tmp_p.y() = FOCAL_LENGTH * tmp_p.y() / tmp_p.z() + row / 2.0;
        un_prev_pts[i] = cv::Point2f(tmp_p.x(), tmp_p.y());
    }

    vector<uchar> f_status;
    out.F = cv::findFundamentalMat(un_cur_pts, un_prev_pts, cv::FM_RANSAC,
                                   cfg.geodf_ransac_th_px, 0.99, f_status);
    out.f_status = f_status;
    if (out.F.empty())
        return false;

    out.errors.assign(total, 0.0);
    vector<uchar> left_outlier(total, 0);
    vector<double> scored_errors;
    scored_errors.reserve(total);
    int scored = 0;
    int ransac_outliers = 0;
    int sampson_above_th = 0;

    for (int i = 0; i < total; i++)
    {
        if (track_cnt[i] < cfg.geodf_min_track_cnt)
            continue;
        scored++;
        out.errors[i] = sampsonDistance(out.F, un_cur_pts[i], un_prev_pts[i]);
        scored_errors.push_back(out.errors[i]);
        if (out.errors[i] > cfg.geodf_sampson_th)
            sampson_above_th++;
        const bool ransac_outlier = f_status.empty() || f_status[i] == 0;
        left_outlier[i] = ransac_outlier ? 1 : 0;
        if (ransac_outlier)
            ransac_outliers++;
    }
    out.scored = scored;
    out.ransac_outliers = ransac_outliers;
    out.sampson_above_th = sampson_above_th;

    out.right_err.assign(total, 0.0);
    vector<uchar> right_outlier(total, 0);
    out.right_valid.assign(total, 0);
    if (cfg.geodf_stereo_check && stereo_cam && m_camera.size() > 1 &&
        !cur_img1.empty() && !prev_right_pts_map.empty())
    {
        vector<cv::Point2f> cur_r;
        vector<uchar> st;
        vector<float> er;
        cv::calcOpticalFlowPyrLK(cur_img, cur_img1, cur_pts, cur_r, st, er,
                                 cv::Size(21, 21), 3);
        vector<cv::Point2f> un_cr, un_pr;
        vector<int> ref_idx;
        un_cr.reserve(total);
        un_pr.reserve(total);
        ref_idx.reserve(total);
        for (int i = 0; i < total; i++)
        {
            if (track_cnt[i] < cfg.geodf_min_track_cnt)
                continue;
            if (i >= static_cast<int>(st.size()) || !st[i] || !inBorder(cur_r[i]))
                continue;
            auto it = prev_right_pts_map.find(ids[i]);
            if (it == prev_right_pts_map.end())
                continue;
            Eigen::Vector3d tp;
            m_camera[1]->liftProjective(Eigen::Vector2d(cur_r[i].x, cur_r[i].y), tp);
            cv::Point2f cr(FOCAL_LENGTH * tp.x() / tp.z() + col / 2.0,
                           FOCAL_LENGTH * tp.y() / tp.z() + row / 2.0);
            m_camera[1]->liftProjective(Eigen::Vector2d(it->second.x, it->second.y), tp);
            cv::Point2f pr(FOCAL_LENGTH * tp.x() / tp.z() + col / 2.0,
                           FOCAL_LENGTH * tp.y() / tp.z() + row / 2.0);
            un_cr.push_back(cr);
            un_pr.push_back(pr);
            ref_idx.push_back(i);
        }
        if (static_cast<int>(un_cr.size()) >= 8)
        {
            vector<uchar> r_status;
            cv::Mat Fr = cv::findFundamentalMat(un_cr, un_pr, cv::FM_RANSAC,
                                                cfg.geodf_ransac_th_px, 0.99, r_status);
            if (!Fr.empty())
            {
                for (size_t k = 0; k < ref_idx.size(); k++)
                {
                    const int i = ref_idx[k];
                    out.right_err[i] = sampsonDistance(Fr, un_cr[k], un_pr[k]);
                    right_outlier[i] = (r_status.empty() || r_status[k] == 0) ? 1 : 0;
                    out.right_valid[i] = 1;
                }
            }
        }
    }

    const bool stereo_trust =
        cfg.geodf_stereo_check &&
        (cfg.geodf_stereo_floor_max <= 0.0 || geo_outlier_floor < 0.0 ||
         geo_outlier_floor <= cfg.geodf_stereo_floor_max);
    vector<int> candidates;
    candidates.reserve(total);
    int stereo_added = 0;
    for (int i = 0; i < total; i++)
    {
        if (track_cnt[i] < cfg.geodf_min_track_cnt)
            continue;
        const bool left_cand = left_outlier[i] && (out.errors[i] > cfg.geodf_sampson_th);
        const bool right_cand = stereo_trust && out.right_valid[i] &&
                                right_outlier[i] && (out.right_err[i] > cfg.geodf_stereo_sampson_th);
        if (left_cand || right_cand)
        {
            candidates.push_back(i);
            if (!left_cand)
                stereo_added++;
        }
    }
    out.stereo_added = stereo_added;
    out.raw_candidates = candidates;

    if (!scored_errors.empty())
    {
        double sum = 0.0;
        for (double e : scored_errors)
            sum += e;
        out.mean_sampson = sum / scored_errors.size();
        std::sort(scored_errors.begin(), scored_errors.end());
        const size_t n = scored_errors.size();
        out.median_sampson = (n % 2 == 0)
                                 ? 0.5 * (scored_errors[n / 2 - 1] + scored_errors[n / 2])
                                 : scored_errors[n / 2];
        out.max_sampson = scored_errors.back();
    }

    const double frame_outlier_ratio =
        scored > 0 ? static_cast<double>(ransac_outliers) / scored : 0.0;
    if (geo_activation_ema < 0.0)
        geo_activation_ema = frame_outlier_ratio;
    else
        geo_activation_ema = cfg.geodf_activate_ema * frame_outlier_ratio +
                             (1.0 - cfg.geodf_activate_ema) * geo_activation_ema;

    if (geo_outlier_floor < 0.0)
        geo_outlier_floor = frame_outlier_ratio;
    else
    {
        const double b = (frame_outlier_ratio < geo_outlier_floor)
                             ? cfg.geodf_auto_floor_down
                             : cfg.geodf_auto_floor_up;
        geo_outlier_floor = b * frame_outlier_ratio + (1.0 - b) * geo_outlier_floor;
    }

    out.rho_on = cfg.geodf_activate_ratio;
    if (cfg.geodf_auto_rho)
    {
        out.rho_on = geo_outlier_floor * cfg.geodf_auto_mult + cfg.geodf_auto_margin;
        if (out.rho_on < cfg.geodf_activate_ratio_min)
            out.rho_on = cfg.geodf_activate_ratio_min;
        if (out.rho_on > cfg.geodf_activate_ratio_max)
            out.rho_on = cfg.geodf_activate_ratio_max;
    }
    const double rho_off = out.rho_on * cfg.geodf_deactivate_frac;

    out.candidates_raw = candidates.size();
    out.frame_active = 1;
    if (cfg.geodf_adaptive)
    {
        geo_activation_active = geo_activation_active
                                    ? (geo_activation_ema >= rho_off)
                                    : (geo_activation_ema >= out.rho_on);
        out.frame_active = geo_activation_active ? 1 : 0;
        if (!out.frame_active)
            candidates.clear();
    }

    geo_frame_count++;
    const bool have_ids = (static_cast<int>(ids.size()) == total);
    const int vote_k = std::max(1, cfg.geodf_vote_frames);
    if (have_ids)
    {
        std::map<int, int> next_streak;
        for (int idx : candidates)
        {
            const int id = ids[idx];
            const auto it = geo_dyn_streak.find(id);
            next_streak[id] = (it != geo_dyn_streak.end() ? it->second : 0) + 1;
        }
        geo_dyn_streak.swap(next_streak);
    }
    const bool warmup =
        geo_frame_count <= static_cast<long long>(cfg.geodf_warmup_frames);

    out.confirmed.clear();
    out.confirmed.reserve(candidates.size());
    if (!warmup)
    {
        for (int idx : candidates)
        {
            if (vote_k <= 1 || !have_ids)
            {
                out.confirmed.push_back(idx);
                continue;
            }
            const auto it = geo_dyn_streak.find(ids[idx]);
            if (it != geo_dyn_streak.end() && it->second >= vote_k)
                out.confirmed.push_back(idx);
        }
    }
    out.confirmed_n = out.confirmed.size();
    out.geo_ms = t_geo.toc();
    out.valid = true;
    return true;
}

int FeatureTracker::applyTrackRejection(const std::vector<int> &indices, GeoDynamicAnalysis *geo)
{
    auto &cfg = vinsConfig();
    const int total = static_cast<int>(cur_pts.size());
    if (indices.empty() || total <= 0)
        return 0;

    vector<int> to_reject = indices;
    if (geo && static_cast<int>(geo->errors.size()) == total)
    {
        std::sort(to_reject.begin(), to_reject.end(), [&](int a, int b) {
            const double sa = std::max(geo->errors[a],
                                       geo->right_valid[a] ? geo->right_err[a] : 0.0);
            const double sb = std::max(geo->errors[b],
                                       geo->right_valid[b] ? geo->right_err[b] : 0.0);
            return sa > sb;
        });
    }

    vector<uchar> keep(total, 1);
    int rejected = 0;
    int guard_triggered = 0;
    int guard_capped = 0;
    int max_reject = static_cast<int>(to_reject.size());
    if (cfg.geodf_ratio_guard)
    {
        const int ratio_cap = static_cast<int>(std::floor(cfg.geodf_max_reject_ratio * total));
        if (static_cast<int>(to_reject.size()) > ratio_cap)
            guard_triggered = 1;
        max_reject = ratio_cap;
    }
    const int max_reject_by_min = std::max(0, total - cfg.geodf_min_feature_num);
    max_reject = std::min(max_reject, max_reject_by_min);

    for (int idx : to_reject)
    {
        if (rejected >= max_reject)
            break;
        keep[idx] = 0;
        rejected++;
    }
    if (static_cast<int>(to_reject.size()) > rejected)
        guard_capped = 1;

    if (geo && cfg.geodf_dump_features && !cfg.geodf_feat_path.empty())
    {
        std::ofstream feat(cfg.geodf_feat_path, std::ios::app);
        const long long ts = static_cast<long long>(cur_time * 1e9);
        for (int i = 0; i < total; i++)
        {
            if (track_cnt[i] < cfg.geodf_min_track_cnt)
                continue;
            const int ransac_outlier =
                (geo->f_status.empty() || geo->f_status[i] == 0) ? 1 : 0;
            feat << ts << "," << ids[i] << ","
                 << cur_pts[i].x << "," << cur_pts[i].y << ","
                 << geo->errors[i] << "," << ransac_outlier << ","
                 << (keep[i] ? 0 : 1) << "\n";
        }
    }

    if (geo)
    {
        geo->guard_triggered = guard_triggered;
        geo->guard_capped = guard_capped;
    }

    if (rejected > 0)
    {
        vector<uchar> status(keep.begin(), keep.end());
        reduceVector(prev_pts, status);
        reduceVector(cur_pts, status);
        reduceVector(ids, status);
        reduceVector(track_cnt, status);
    }
    return rejected;
}

void FeatureTracker::logGeoDynamicStats(const GeoDynamicAnalysis &analysis, int rejected)
{
    auto &cfg = vinsConfig();
    const int total = analysis.total;
    if (!cfg.geodf_stats_path.empty())
    {
        std::ofstream geo_stats(cfg.geodf_stats_path, std::ios::app);
        const double ratio = total > 0 ? static_cast<double>(rejected) / total : 0.0;
        geo_stats << static_cast<long long>(cur_time * 1e9) << ","
                  << total << "," << analysis.scored << "," << analysis.ransac_outliers << ","
                  << analysis.sampson_above_th << "," << analysis.candidates_raw << ","
                  << rejected << "," << ratio << "," << cur_pts.size() << ","
                  << analysis.mean_sampson << "," << analysis.median_sampson << ","
                  << analysis.max_sampson << "," << analysis.guard_triggered << ","
                  << analysis.guard_capped << "," << geo_activation_ema << ","
                  << analysis.frame_active << "," << analysis.geo_ms << ","
                  << analysis.rho_on << "," << geo_outlier_floor << ","
                  << analysis.stereo_added << "," << analysis.confirmed_n << "\n";
    }

    if (cfg.geodf_debug)
    {
        static int debug_cnt = 0;
        if (debug_cnt++ % 50 == 0)
        {
            ROS_INFO("GeoDF: reject %d/%d (cand %zu scored %d guard %d) %.2fms",
                     rejected, total, analysis.candidates_raw, analysis.scored,
                     analysis.guard_triggered, analysis.geo_ms);
        }
    }
    else
    {
        ROS_DEBUG("GeoDF reject: %d/%d (cand %zu) cost %fms",
                  rejected, total, analysis.candidates_raw, analysis.geo_ms);
    }
}

void FeatureTracker::rejectGeoDynamic()
{
    GeoDynamicAnalysis analysis;
    if (!analyzeGeoDynamic(analysis))
        return;
    GeoDynamicAnalysis mutable_analysis = analysis;
    const int rejected = applyTrackRejection(mutable_analysis.confirmed, &mutable_analysis);
    logGeoDynamicStats(mutable_analysis, rejected);
}

void FeatureTracker::rejectSemGeoFused()
{
    auto &cfg = vinsConfig();
    if (cur_pts.empty())
        return;

    const int total = static_cast<int>(cur_pts.size());
    int mask_available = 0;
    const double dynamic_pixel_ratio = computeDynamicPixelRatio(mask_available);

    GeoDynamicAnalysis geo;
    const bool geo_ok = analyzeGeoDynamic(geo);
    updateSemanticAdaptivePolicy(dynamic_pixel_ratio, mask_available, geo_ok ? &geo : nullptr);

    std::set<int> fused_set;
    int sem_candidates = 0;
    int sem_confirmed = 0;
    const bool sem_hard_reject = applySemanticHardReject();
    if (sem_hard_reject && mask_available && sem_mask_trusted)
    {
        std::vector<int> sem_raw;
        collectSemanticRawCandidates(sem_raw);
        sem_candidates = static_cast<int>(sem_raw.size());
        std::vector<int> confirmed;
        sem_confirmed = confirmSemanticCandidates(sem_raw, confirmed, true);
        for (int idx : confirmed)
            fused_set.insert(idx);
    }
    else
    {
        sem_dyn_streak.clear();
    }

    int geo_candidates = 0;
    if (geo_ok && geo.frame_active)
    {
        geo_candidates = static_cast<int>(geo.confirmed.size());
        for (int idx : geo.confirmed)
            fused_set.insert(idx);
    }

    std::vector<int> fused(fused_set.begin(), fused_set.end());
    GeoDynamicAnalysis mutable_geo = geo;
    if (geo_ok && static_cast<int>(mutable_geo.errors.size()) == total)
    {
        const double sem_sort_score = cfg.geodf_sampson_th + 1.0;
        for (int idx : fused)
        {
            const bool geo_hit = geo.frame_active &&
                                 std::find(geo.confirmed.begin(), geo.confirmed.end(), idx) != geo.confirmed.end();
            if (!geo_hit)
                mutable_geo.errors[idx] = std::max(mutable_geo.errors[idx], sem_sort_score);
        }
    }
    const int rejected = applyTrackRejection(fused, geo_ok ? &mutable_geo : nullptr);

    if (!cfg.sem_geodf_stats_path.empty())
    {
        std::ofstream fusion_stats(cfg.sem_geodf_stats_path, std::ios::app);
        const double ratio = total > 0 ? static_cast<double>(rejected) / total : 0.0;
        fusion_stats << static_cast<long long>(cur_time * 1e9) << ","
                     << total << "," << (sem_scene_active ? 1 : 0) << ","
                     << (geo_ok && geo.frame_active ? 1 : 0) << ","
                     << (applySemanticSoftMask() ? 1 : 0) << ","
                     << sem_candidates << "," << sem_confirmed << ","
                     << geo_candidates << "," << static_cast<int>(fused.size()) << ","
                     << rejected << "," << ratio << "," << cur_pts.size() << ","
                     << mask_available << "," << (sem_mask_trusted ? 1 : 0) << ","
                     << dynamic_pixel_ratio << "," << sem_mask_lag_ms << ","
                     << sem_activation_ema << "," << geo_activation_ema << ","
                     << sem_policy_state << "," << sem_policy_hold << ","
                     << sem_geo_overlap_last << "," << sem_geo_overlap_ema << ","
                     << (sem_hard_reject ? 1 : 0) << ","
                     << sem_policy_trigger_burst << "," << sem_policy_trigger_strong << ","
                     << sem_policy_trigger_overlap << "\n";
    }
}

void FeatureTracker::readIntrinsicParameter(const vector<string> &calib_file)
{
    for (size_t i = 0; i < calib_file.size(); i++)
    {
        ROS_INFO("reading paramerter of camera %s", calib_file[i].c_str());
        camodocal::CameraPtr camera = CameraFactory::instance()->generateCameraFromYamlFile(calib_file[i]);
        m_camera.push_back(camera);
    }
    if (calib_file.size() == 2)
        stereo_cam = 1;
}

void FeatureTracker::showUndistortion(const string &name)
{
    cv::Mat undistortedImg(row + 600, col + 600, CV_8UC1, cv::Scalar(0));
    vector<Eigen::Vector2d> distortedp, undistortedp;
    for (int i = 0; i < col; i++)
        for (int j = 0; j < row; j++)
        {
            Eigen::Vector2d a(i, j);
            Eigen::Vector3d b;
            m_camera[0]->liftProjective(a, b);
            distortedp.push_back(a);
            undistortedp.push_back(Eigen::Vector2d(b.x() / b.z(), b.y() / b.z()));
            //printf("%f,%f->%f,%f,%f\n)\n", a.x(), a.y(), b.x(), b.y(), b.z());
        }
    for (int i = 0; i < int(undistortedp.size()); i++)
    {
        cv::Mat pp(3, 1, CV_32FC1);
        pp.at<float>(0, 0) = undistortedp[i].x() * FOCAL_LENGTH + col / 2;
        pp.at<float>(1, 0) = undistortedp[i].y() * FOCAL_LENGTH + row / 2;
        pp.at<float>(2, 0) = 1.0;
        //cout << trackerData[0].K << endl;
        //printf("%lf %lf\n", p.at<float>(1, 0), p.at<float>(0, 0));
        //printf("%lf %lf\n", pp.at<float>(1, 0), pp.at<float>(0, 0));
        if (pp.at<float>(1, 0) + 300 >= 0 && pp.at<float>(1, 0) + 300 < row + 600 && pp.at<float>(0, 0) + 300 >= 0 && pp.at<float>(0, 0) + 300 < col + 600)
        {
            undistortedImg.at<uchar>(pp.at<float>(1, 0) + 300, pp.at<float>(0, 0) + 300) = cur_img.at<uchar>(distortedp[i].y(), distortedp[i].x());
        }
        else
        {
            //ROS_ERROR("(%f %f) -> (%f %f)", distortedp[i].y, distortedp[i].x, pp.at<float>(1, 0), pp.at<float>(0, 0));
        }
    }
    // turn the following code on if you need
    // cv::imshow(name, undistortedImg);
    // cv::waitKey(0);
}

vector<cv::Point2f> FeatureTracker::undistortedPts(vector<cv::Point2f> &pts, camodocal::CameraPtr cam)
{
    vector<cv::Point2f> un_pts;
    for (unsigned int i = 0; i < pts.size(); i++)
    {
        Eigen::Vector2d a(pts[i].x, pts[i].y);
        Eigen::Vector3d b;
        cam->liftProjective(a, b);
        un_pts.push_back(cv::Point2f(b.x() / b.z(), b.y() / b.z()));
    }
    return un_pts;
}

vector<cv::Point2f> FeatureTracker::ptsVelocity(vector<int> &ids, vector<cv::Point2f> &pts, 
                                            map<int, cv::Point2f> &cur_id_pts, map<int, cv::Point2f> &prev_id_pts)
{
    vector<cv::Point2f> pts_velocity;
    cur_id_pts.clear();
    for (unsigned int i = 0; i < ids.size(); i++)
    {
        cur_id_pts.insert(make_pair(ids[i], pts[i]));
    }

    // caculate points velocity
    if (!prev_id_pts.empty())
    {
        double dt = cur_time - prev_time;
        
        for (unsigned int i = 0; i < pts.size(); i++)
        {
            std::map<int, cv::Point2f>::iterator it;
            it = prev_id_pts.find(ids[i]);
            if (it != prev_id_pts.end())
            {
                double v_x = (pts[i].x - it->second.x) / dt;
                double v_y = (pts[i].y - it->second.y) / dt;
                pts_velocity.push_back(cv::Point2f(v_x, v_y));
            }
            else
                pts_velocity.push_back(cv::Point2f(0, 0));

        }
    }
    else
    {
        for (unsigned int i = 0; i < cur_pts.size(); i++)
        {
            pts_velocity.push_back(cv::Point2f(0, 0));
        }
    }
    return pts_velocity;
}

void FeatureTracker::drawTrack(const cv::Mat &imLeft, const cv::Mat &imRight, 
                               vector<int> &curLeftIds,
                               vector<cv::Point2f> &curLeftPts, 
                               vector<cv::Point2f> &curRightPts,
                               map<int, cv::Point2f> &prevLeftPtsMap)
{
    //int rows = imLeft.rows;
    int cols = imLeft.cols;
    if (!imRight.empty() && stereo_cam)
        cv::hconcat(imLeft, imRight, imTrack);
    else
        imTrack = imLeft.clone();
    cv::cvtColor(imTrack, imTrack, CV_GRAY2RGB);

    for (size_t j = 0; j < curLeftPts.size(); j++)
    {
        double len = std::min(1.0, 1.0 * track_cnt[j] / 20);
        cv::circle(imTrack, curLeftPts[j], 2, cv::Scalar(255 * (1 - len), 0, 255 * len), 2);
    }
    if (!imRight.empty() && stereo_cam)
    {
        for (size_t i = 0; i < curRightPts.size(); i++)
        {
            cv::Point2f rightPt = curRightPts[i];
            rightPt.x += cols;
            cv::circle(imTrack, rightPt, 2, cv::Scalar(0, 255, 0), 2);
            //cv::Point2f leftPt = curLeftPtsTrackRight[i];
            //cv::line(imTrack, leftPt, rightPt, cv::Scalar(0, 255, 0), 1, 8, 0);
        }
    }
    
    map<int, cv::Point2f>::iterator mapIt;
    for (size_t i = 0; i < curLeftIds.size(); i++)
    {
        int id = curLeftIds[i];
        mapIt = prevLeftPtsMap.find(id);
        if(mapIt != prevLeftPtsMap.end())
        {
            cv::arrowedLine(imTrack, curLeftPts[i], mapIt->second, cv::Scalar(0, 255, 0), 1, 8, 0, 0.2);
        }
    }

    //draw prediction
    /*
    for(size_t i = 0; i < predict_pts_debug.size(); i++)
    {
        cv::circle(imTrack, predict_pts_debug[i], 2, cv::Scalar(0, 170, 255), 2);
    }
    */
    //printf("predict pts size %d \n", (int)predict_pts_debug.size());

    //cv::Mat imCur2Compress;
    //cv::resize(imCur2, imCur2Compress, cv::Size(cols, rows / 2));
}


void FeatureTracker::setPrediction(map<int, Eigen::Vector3d> &predictPts)
{
    hasPrediction = true;
    predict_pts.clear();
    predict_pts_debug.clear();
    map<int, Eigen::Vector3d>::iterator itPredict;
    for (size_t i = 0; i < ids.size(); i++)
    {
        //printf("prevLeftId size %d prevLeftPts size %d\n",(int)prevLeftIds.size(), (int)prevLeftPts.size());
        int id = ids[i];
        itPredict = predictPts.find(id);
        if (itPredict != predictPts.end())
        {
            Eigen::Vector2d tmp_uv;
            m_camera[0]->spaceToPlane(itPredict->second, tmp_uv);
            predict_pts.push_back(cv::Point2f(tmp_uv.x(), tmp_uv.y()));
            predict_pts_debug.push_back(cv::Point2f(tmp_uv.x(), tmp_uv.y()));
        }
        else
            predict_pts.push_back(prev_pts[i]);
    }
}


void FeatureTracker::removeOutliers(set<int> &removePtsIds)
{
    std::set<int>::iterator itSet;
    vector<uchar> status;
    for (size_t i = 0; i < ids.size(); i++)
    {
        itSet = removePtsIds.find(ids[i]);
        if(itSet != removePtsIds.end())
            status.push_back(0);
        else
            status.push_back(1);
    }

    reduceVector(prev_pts, status);
    reduceVector(ids, status);
    reduceVector(track_cnt, status);
}


cv::Mat FeatureTracker::getTrackImage()
{
    return imTrack;
}