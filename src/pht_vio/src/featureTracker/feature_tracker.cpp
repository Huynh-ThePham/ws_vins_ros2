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
#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>
#include <opencv2/imgproc/imgproc_c.h>

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
    dynamic_scene_ema = -1.0;
    dynamic_scene_active = false;
    dynamic_outlier_floor = -1.0;
    dynamic_frame_count = 0;
    sgta_policy_signal_ema = -1.0;
    sgta_aggressive_active = false;
    sgta_aggressive_hold = 0;
    sem_mask_lag_ms = -1.0;
    sem_mask_trusted = false;
    latest_gyro_time = -1.0;
    latest_cam_gyro.setZero();
}

void FeatureTracker::setLatestCameraGyro(double t, const Eigen::Vector3d &gyro)
{
    latest_gyro_time = t;
    latest_cam_gyro = gyro;
}

bool FeatureTracker::isSemanticStatic(const cv::Point2f &pt) const
{
    if (!vinsConfig().sem_enable || sem_mask.empty() || !sem_mask_trusted)
        return true;
    const int x = cvRound(pt.x);
    const int y = cvRound(pt.y);
    if (x < 0 || y < 0 || x >= sem_mask.cols || y >= sem_mask.rows)
        return false;
    return sem_mask.at<uchar>(y, x) >= static_cast<uchar>(vinsConfig().sem_static_value);
}

bool FeatureTracker::semanticMaskTrusted() const
{
    return vinsConfig().sem_enable && !sem_mask.empty() && sem_mask_trusted;
}

bool FeatureTracker::applySemanticSoftMask() const
{
    const auto &cfg = vinsConfig();
    if (!semanticMaskTrusted())
        return false;
    if (!cfg.geodf_enable)
        return true;
    if (cfg.sgta_policy_enable && sgta_aggressive_active)
        return true;
    return !cfg.sem_mask_gated || dynamic_scene_active;
}

double FeatureTracker::computeDynamicPixelRatio(int &mask_available) const
{
    mask_available = semanticMaskTrusted() ? 1 : 0;
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
            dynamic_pixels += row_ptr[x] < static_cast<uchar>(vinsConfig().sem_static_value);
    }
    return static_cast<double>(dynamic_pixels) / static_cast<double>(sized.total());
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
    if (mask_available)
    {
        vector<uchar> status(cur_pts.size(), 1);
        for (size_t i = 0; i < cur_pts.size(); i++)
        {
            if (!isSemanticStatic(cur_pts[i]))
            {
                status[i] = 0;
                sem_candidates++;
                rejected++;
            }
        }
        if (rejected > 0)
        {
            reduceVector(prev_pts, status);
            reduceVector(cur_pts, status);
            reduceVector(ids, status);
            reduceVector(track_cnt, status);
        }
    }

    if (!cfg.sem_stats_path.empty())
    {
        std::ofstream sem_stats(cfg.sem_stats_path, std::ios::app);
        const double ratio = total > 0 ? static_cast<double>(rejected) / total : 0.0;
        sem_stats << static_cast<long long>(cur_time * 1e9) << ","
                  << total << "," << rejected << "," << ratio << ","
                  << static_cast<int>(cur_pts.size()) << ","
                  << mask_available << "," << dynamic_pixel_ratio << ","
                  << sem_candidates << "," << sem_candidates << ","
                  << (sem_mask_trusted ? 1 : 0) << "," << sem_mask_lag_ms << "\n";
    }
}

void FeatureTracker::rejectSemanticGeometricDynamic()
{
    auto &cfg = vinsConfig();
    if (!cfg.geodf_enable || cur_pts.empty())
        return;

    TicToc t_geo;
    dynamic_frame_count++;
    const int total = static_cast<int>(cur_pts.size());
    const vector<int> original_ids = ids;
    const vector<int> original_track_cnt = track_cnt;
    int mask_available = 0;
    const double dynamic_pixel_ratio = computeDynamicPixelRatio(mask_available);

    vector<uchar> semantic_dynamic(total, 0);
    int semantic_candidates = 0;
    for (int i = 0; i < total; i++)
    {
        if (mask_available && !isSemanticStatic(cur_pts[i]))
        {
            semantic_dynamic[i] = 1;
            semantic_candidates++;
        }
    }

    vector<double> sampson(total, 0.0);
    vector<uchar> ransac_outlier(total, 0);
    vector<uchar> sampson_outlier(total, 0);
    vector<uchar> imu_dynamic(total, 0);
    vector<int> scored_indices;
    scored_indices.reserve(total);

    for (int i = 0; i < total; i++)
    {
        if (track_cnt[i] >= cfg.geodf_min_track_cnt)
            scored_indices.push_back(i);
    }

    int ransac_outliers = 0;
    int sampson_above = 0;
    vector<double> scored_sampson;
    scored_sampson.reserve(scored_indices.size());

    if (static_cast<int>(scored_indices.size()) >= std::max(8, cfg.geodf_min_feature_num))
    {
        vector<cv::Point2f> prev_norm, cur_norm;
        prev_norm.reserve(scored_indices.size());
        cur_norm.reserve(scored_indices.size());
        for (int idx : scored_indices)
        {
            Eigen::Vector3d tmp_p;
            m_camera[0]->liftProjective(Eigen::Vector2d(prev_pts[idx].x, prev_pts[idx].y), tmp_p);
            prev_norm.emplace_back(
                FOCAL_LENGTH * tmp_p.x() / tmp_p.z() + col / 2.0,
                FOCAL_LENGTH * tmp_p.y() / tmp_p.z() + row / 2.0);

            m_camera[0]->liftProjective(Eigen::Vector2d(cur_pts[idx].x, cur_pts[idx].y), tmp_p);
            cur_norm.emplace_back(
                FOCAL_LENGTH * tmp_p.x() / tmp_p.z() + col / 2.0,
                FOCAL_LENGTH * tmp_p.y() / tmp_p.z() + row / 2.0);
        }

        vector<uchar> inlier_status;
        cv::Mat F = cv::findFundamentalMat(prev_norm, cur_norm, cv::FM_RANSAC,
                                           cfg.geodf_ransac_th_px, 0.99, inlier_status);
        if (!F.empty() && F.rows == 3 && F.cols == 3 &&
            inlier_status.size() == scored_indices.size())
        {
            cv::Matx33d f;
            F.convertTo(F, CV_64F);
            for (int r = 0; r < 3; r++)
                for (int c = 0; c < 3; c++)
                    f(r, c) = F.at<double>(r, c);

            for (size_t k = 0; k < scored_indices.size(); k++)
            {
                const int idx = scored_indices[k];
                const cv::Vec3d x1(prev_norm[k].x, prev_norm[k].y, 1.0);
                const cv::Vec3d x2(cur_norm[k].x, cur_norm[k].y, 1.0);
                const cv::Vec3d fx1 = f * x1;
                const cv::Vec3d ftx2 = f.t() * x2;
                const double err = x2.dot(fx1);
                const double denom = fx1[0] * fx1[0] + fx1[1] * fx1[1] +
                                     ftx2[0] * ftx2[0] + ftx2[1] * ftx2[1] + 1e-12;
                sampson[idx] = (err * err) / denom;
                scored_sampson.push_back(sampson[idx]);

                if (!inlier_status[k])
                {
                    ransac_outlier[idx] = 1;
                    ransac_outliers++;
                }
                if (sampson[idx] > cfg.geodf_sampson_th)
                {
                    sampson_outlier[idx] = 1;
                    sampson_above++;
                }
            }
        }
    }

    int imu_outliers = 0;
    if (cfg.sgta_imu_gate_enable && prev_time > 0 && cur_time > prev_time &&
        m_camera.size() > 0 && latest_gyro_time > 0)
    {
        const double dt = cur_time - prev_time;
        const Eigen::Quaterniond dq = Utility::deltaQ(latest_cam_gyro * dt);
        for (int i = 0; i < total; i++)
        {
            Eigen::Vector3d prev_ray;
            m_camera[0]->liftProjective(Eigen::Vector2d(prev_pts[i].x, prev_pts[i].y), prev_ray);
            Eigen::Vector3d pred_ray = dq * prev_ray;
            Eigen::Vector2d pred_px;
            m_camera[0]->spaceToPlane(pred_ray, pred_px);
            const double flow_residual =
                std::hypot(cur_pts[i].x - pred_px.x(), cur_pts[i].y - pred_px.y());
            if (flow_residual > cfg.sgta_imu_flow_th_px)
            {
                imu_dynamic[i] = 1;
                imu_outliers++;
            }
        }
    }

    const double geo_ratio = scored_indices.empty()
                                 ? 0.0
                                 : static_cast<double>(std::max(ransac_outliers, sampson_above)) /
                                       static_cast<double>(scored_indices.size());
    const double semantic_ratio = total > 0 ? static_cast<double>(semantic_candidates) / total : 0.0;
    const double imu_ratio = total > 0 ? static_cast<double>(imu_outliers) / total : 0.0;
    const double scene_observation = std::max({geo_ratio, 0.7 * semantic_ratio, 0.5 * imu_ratio});
    const double alpha = std::clamp(cfg.geodf_activate_ema, 0.0, 1.0);
    if (dynamic_scene_ema < 0.0)
        dynamic_scene_ema = scene_observation;
    else
        dynamic_scene_ema = alpha * scene_observation + (1.0 - alpha) * dynamic_scene_ema;

    if (dynamic_outlier_floor < 0.0)
        dynamic_outlier_floor = geo_ratio;
    else if (geo_ratio < dynamic_outlier_floor)
        dynamic_outlier_floor = (1.0 - cfg.geodf_auto_floor_down) * dynamic_outlier_floor +
                                cfg.geodf_auto_floor_down * geo_ratio;
    else
        dynamic_outlier_floor = (1.0 - cfg.geodf_auto_floor_up) * dynamic_outlier_floor +
                                cfg.geodf_auto_floor_up * geo_ratio;

    const double policy_alpha = std::clamp(cfg.sgta_policy_ema_alpha, 0.0, 1.0);
    const double policy_decay_alpha = std::clamp(cfg.sgta_policy_decay_alpha, 0.0, 1.0);
    const double policy_observation = mask_available
                                          ? std::sqrt(std::max(0.0, dynamic_pixel_ratio * scene_observation))
                                          : 0.5 * scene_observation;
    if (sgta_policy_signal_ema < 0.0)
        sgta_policy_signal_ema = policy_observation;
    else
    {
        const double update_alpha =
            policy_observation >= sgta_policy_signal_ema ? policy_alpha : policy_decay_alpha;
        sgta_policy_signal_ema = update_alpha * policy_observation +
                                 (1.0 - update_alpha) * sgta_policy_signal_ema;
    }

    if (!cfg.sgta_policy_enable)
    {
        sgta_aggressive_active = false;
        sgta_aggressive_hold = 0;
    }
    else if (sgta_policy_signal_ema >= cfg.sgta_aggressive_sem_on)
    {
        sgta_aggressive_active = true;
        sgta_aggressive_hold = std::max(0, cfg.sgta_aggressive_hold_frames);
    }
    else if (sgta_aggressive_active &&
             (sgta_policy_signal_ema >= cfg.sgta_aggressive_sem_off ||
              sgta_aggressive_hold > 0))
    {
        if (sgta_aggressive_hold > 0)
            sgta_aggressive_hold--;
        sgta_aggressive_active = true;
    }
    else
    {
        sgta_aggressive_active = false;
        sgta_aggressive_hold = 0;
    }

    double rho_on = cfg.geodf_activate_ratio;
    const bool use_aggressive = cfg.sgta_policy_enable && sgta_aggressive_active;
    if (use_aggressive)
    {
        rho_on = cfg.sgta_aggressive_activate_ratio;
    }
    else if (cfg.geodf_auto_rho)
    {
        rho_on = dynamic_outlier_floor * cfg.geodf_auto_mult + cfg.geodf_auto_margin;
        rho_on = std::clamp(rho_on, cfg.geodf_activate_ratio_min, cfg.geodf_activate_ratio_max);
    }

    if (!cfg.geodf_adaptive)
    {
        dynamic_scene_active = true;
    }
    else if (dynamic_scene_active)
    {
        dynamic_scene_active = dynamic_scene_ema >= rho_on * cfg.geodf_deactivate_frac;
    }
    else
    {
        dynamic_scene_active = dynamic_scene_ema >= rho_on;
    }

    vector<double> p_dyn(total, 0.0);
    vector<uchar> reject_status(total, 0);
    vector<uchar> raw_candidate(total, 0);
    vector<pair<double, int>> reject_rank;
    reject_rank.reserve(total);

    const double p_alpha = std::clamp(cfg.geodf_temporal_alpha, 0.0, 1.0);
    int raw_candidates = 0;
    for (int i = 0; i < total; i++)
    {
        const bool geo_dynamic = ransac_outlier[i] || sampson_outlier[i];
        double observation = 0.0;
        if (semantic_dynamic[i] && geo_dynamic)
            observation = 1.0;
        else if (semantic_dynamic[i])
            observation = 0.65;
        else if (geo_dynamic)
            observation = 0.55;
        if (imu_dynamic[i])
            observation = std::max(observation, cfg.sgta_imu_dynamic_obs);

        const auto old_it = dynamic_prob.find(ids[i]);
        const double old_prob = old_it == dynamic_prob.end() ? 0.0 : old_it->second;
        p_dyn[i] = p_alpha * observation + (1.0 - p_alpha) * old_prob;
        dynamic_prob[ids[i]] = p_dyn[i];
        cfg.feature_dynamic_prob[ids[i]] = p_dyn[i];

        const double dynamic_prob_th = use_aggressive
                                           ? cfg.sgta_aggressive_dynamic_prob_th
                                           : cfg.geodf_dynamic_prob_th;
        const bool persistent_dynamic = p_dyn[i] >= dynamic_prob_th;
        const bool instant_agreement = semantic_dynamic[i] && geo_dynamic;
        const bool geometry_only = !mask_available && (geo_dynamic || imu_dynamic[i]);
        if (persistent_dynamic || instant_agreement || geometry_only)
        {
            raw_candidate[i] = 1;
            raw_candidates++;
        }
    }

    std::map<int, int> next_streak;
    for (int i = 0; i < total; i++)
    {
        if (!raw_candidate[i])
            continue;
        const auto old_it = dynamic_streak.find(ids[i]);
        next_streak[ids[i]] = (old_it == dynamic_streak.end() ? 0 : old_it->second) + 1;
    }
    dynamic_streak.swap(next_streak);

    int confirmed_candidates = 0;
    int semantic_confirmed = 0;
    const int vote_frames = std::max(1, use_aggressive
                                            ? cfg.sgta_aggressive_vote_frames
                                            : cfg.geodf_vote_frames);
    const int warmup_frames = use_aggressive
                                  ? cfg.sgta_aggressive_warmup_frames
                                  : cfg.geodf_warmup_frames;
    const bool warmup = warmup_frames > 0 && dynamic_frame_count <= warmup_frames;
    if (cfg.geodf_hard_reject && dynamic_scene_active && !warmup)
    {
        for (int i = 0; i < total; i++)
        {
            if (!raw_candidate[i])
                continue;
            const bool instant_agreement =
                semantic_dynamic[i] && (ransac_outlier[i] || sampson_outlier[i]);
            const auto streak_it = dynamic_streak.find(ids[i]);
            const int streak = streak_it == dynamic_streak.end() ? 0 : streak_it->second;
            if (instant_agreement || streak >= vote_frames)
            {
                reject_status[i] = 1;
                confirmed_candidates++;
                semantic_confirmed += semantic_dynamic[i] ? 1 : 0;
                const double sampson_score = cfg.geodf_sampson_th > 1e-9
                                                 ? std::min(1.0, sampson[i] / cfg.geodf_sampson_th)
                                                 : 0.0;
                reject_rank.emplace_back(p_dyn[i] + sampson_score, i);
            }
        }
    }

    bool guard_triggered = false;
    bool guard_capped = false;
    int rejected = static_cast<int>(reject_rank.size());
    const int reject_cap = static_cast<int>(std::floor(cfg.geodf_reject_ratio_max * total));
    if (cfg.geodf_ratio_guard && rejected > reject_cap)
    {
        guard_triggered = true;
        guard_capped = true;
        std::sort(reject_rank.begin(), reject_rank.end(),
                  [](const auto &a, const auto &b) { return a.first > b.first; });
        std::fill(reject_status.begin(), reject_status.end(), 0);
        for (int i = 0; i < reject_cap && i < static_cast<int>(reject_rank.size()); i++)
            reject_status[reject_rank[i].second] = 1;
        rejected = reject_cap;
    }

    if (rejected > 0)
    {
        vector<uchar> keep_status(total, 1);
        for (int i = 0; i < total; i++)
            keep_status[i] = reject_status[i] ? 0 : 1;
        reduceVector(prev_pts, keep_status);
        reduceVector(cur_pts, keep_status);
        reduceVector(ids, keep_status);
        reduceVector(track_cnt, keep_status);
    }

    if (dynamic_prob.size() > 2 * static_cast<size_t>(std::max(1, cfg.max_cnt)))
    {
        std::set<int> live_ids(ids.begin(), ids.end());
        for (auto it = dynamic_prob.begin(); it != dynamic_prob.end();)
        {
            if (live_ids.count(it->first) == 0)
                it = dynamic_prob.erase(it);
            else
                ++it;
        }
        for (auto it = dynamic_streak.begin(); it != dynamic_streak.end();)
        {
            if (live_ids.count(it->first) == 0)
                it = dynamic_streak.erase(it);
            else
                ++it;
        }
        for (auto it = cfg.feature_dynamic_prob.begin(); it != cfg.feature_dynamic_prob.end();)
        {
            if (live_ids.count(it->first) == 0)
                it = cfg.feature_dynamic_prob.erase(it);
            else
                ++it;
        }
    }

    double mean_sampson = 0.0;
    double median_sampson = 0.0;
    double max_sampson = 0.0;
    if (!scored_sampson.empty())
    {
        mean_sampson = std::accumulate(scored_sampson.begin(), scored_sampson.end(), 0.0) /
                       static_cast<double>(scored_sampson.size());
        std::sort(scored_sampson.begin(), scored_sampson.end());
        median_sampson = scored_sampson[scored_sampson.size() / 2];
        max_sampson = scored_sampson.back();
    }

    if (!cfg.geodf_stats_path.empty())
    {
        std::ofstream geodf_stats(cfg.geodf_stats_path, std::ios::app);
        geodf_stats << static_cast<long long>(cur_time * 1e9) << ","
                    << total << "," << scored_indices.size() << "," << ransac_outliers << ","
                    << sampson_above << "," << raw_candidates << "," << rejected << ","
                    << (total > 0 ? static_cast<double>(rejected) / total : 0.0) << ","
                    << static_cast<int>(cur_pts.size()) << "," << mean_sampson << ","
                    << median_sampson << "," << max_sampson << ","
                    << (guard_triggered ? 1 : 0) << "," << (guard_capped ? 1 : 0) << ","
                    << dynamic_scene_ema << "," << (dynamic_scene_active ? 1 : 0) << ","
                    << t_geo.toc() << "," << rho_on << "," << dynamic_outlier_floor << ","
                    << semantic_candidates << "," << semantic_confirmed << ","
                    << imu_outliers << "," << mask_available << ","
                    << (sem_mask_trusted ? 1 : 0) << "," << sem_mask_lag_ms << ","
                    << sgta_policy_signal_ema << "," << (use_aggressive ? 1 : 0) << "\n";
    }

    if (!cfg.sem_stats_path.empty())
    {
        std::ofstream sem_stats(cfg.sem_stats_path, std::ios::app);
        sem_stats << static_cast<long long>(cur_time * 1e9) << ","
                  << total << "," << rejected << ","
                  << (total > 0 ? static_cast<double>(rejected) / total : 0.0) << ","
                  << static_cast<int>(cur_pts.size()) << ","
                  << mask_available << "," << dynamic_pixel_ratio << ","
                  << semantic_candidates << "," << semantic_confirmed << ","
                  << (sem_mask_trusted ? 1 : 0) << "," << sem_mask_lag_ms << "\n";
    }

    if (!cfg.geodf_features_path.empty())
    {
        std::ofstream geodf_features(cfg.geodf_features_path, std::ios::app);
        for (int i = 0; i < total; i++)
        {
            geodf_features << static_cast<long long>(cur_time * 1e9) << ","
                           << original_ids[i] << "," << original_track_cnt[i] << ","
                           << static_cast<int>(semantic_dynamic[i]) << ","
                           << static_cast<int>(ransac_outlier[i]) << ","
                           << sampson[i] << "," << p_dyn[i] << ","
                           << static_cast<int>(reject_status[i]) << "\n";
        }
    }

    if (cfg.geodf_debug)
    {
        ROS_INFO("GeoDF/SGTA: reject %d/%d raw=%d confirmed=%d scored=%zu scene=%.3f rho=%.3f active=%d mode=%s policy=%.3f sem=%d geo=%d imu=%d %.2fms",
                 rejected, total, raw_candidates, confirmed_candidates,
                 scored_indices.size(), dynamic_scene_ema, rho_on,
                 dynamic_scene_active ? 1 : 0,
                 use_aggressive ? "aggressive" : "static",
                 sgta_policy_signal_ema, semantic_candidates,
                 std::max(ransac_outliers, sampson_above), imu_outliers, t_geo.toc());
    }
}

void FeatureTracker::setMask()
{
    mask = cv::Mat(row, col, CV_8UC1, cv::Scalar(255));
    const bool use_sem_mask = applySemanticSoftMask();
    if (use_sem_mask)
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
        if (mask.at<uchar>(it.second.first) == 255 && (!use_sem_mask || isSemanticStatic(it.second.first)))
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

map<int, vector<pair<int, Eigen::Matrix<double, 7, 1>>>> FeatureTracker::trackImage(
    double _cur_time, const cv::Mat &_img, const cv::Mat &_img1,
    const cv::Mat &_sem_mask, double _sem_mask_lag_ms)
{
    TicToc t_r;
    cur_time = _cur_time;
    cur_img = _img;
    row = cur_img.rows;
    col = cur_img.cols;
    sem_mask = _sem_mask;
    sem_mask_lag_ms = _sem_mask_lag_ms;
    sem_mask_trusted = false;
    if (vinsConfig().sem_enable && !sem_mask.empty())
    {
        if (sem_mask.size() != cur_img.size())
            cv::resize(sem_mask, sem_mask, cur_img.size(), 0, 0, cv::INTER_NEAREST);
        sem_mask_trusted = sem_mask_lag_ms < 0.0 ||
                           sem_mask_lag_ms <= vinsConfig().sem_mask_max_age_ms;
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

    if (vinsConfig().geodf_enable)
        rejectSemanticGeometricDynamic();
    else
        rejectSemanticDynamic();

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
        if (vinsConfig().feature_dynamic_prob.find(feature_id) == vinsConfig().feature_dynamic_prob.end())
            vinsConfig().feature_dynamic_prob[feature_id] = 0.0;
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
