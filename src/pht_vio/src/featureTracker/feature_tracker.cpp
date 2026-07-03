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
#include <algorithm>
#include <fstream>
#include <cmath>

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

double medianValue(std::vector<double> values)
{
    if (values.empty())
        return 0.0;
    std::sort(values.begin(), values.end());
    const size_t n = values.size();
    return (n % 2 == 0) ? 0.5 * (values[n / 2 - 1] + values[n / 2])
                        : values[n / 2];
}

double clamp01(double v)
{
    if (v < 0.0)
        return 0.0;
    if (v > 1.0)
        return 1.0;
    return v;
}

bool triangulateStereoPoint(const Eigen::Vector3d &ray0,
                            const Eigen::Vector3d &ray1,
                            const Eigen::Matrix3d &R01,
                            const Eigen::Vector3d &t01,
                            double min_depth,
                            double max_depth,
                            Eigen::Vector3d &point0)
{
    const Eigen::Vector3d d0 = ray0.normalized();
    const Eigen::Vector3d d1 = (R01 * ray1).normalized();
    Eigen::Matrix<double, 3, 2> A;
    A.col(0) = d0;
    A.col(1) = -d1;
    Eigen::Vector2d lambdas = A.colPivHouseholderQr().solve(t01);
    const double z0 = lambdas.x();
    const double z1 = lambdas.y();
    if (!std::isfinite(z0) || !std::isfinite(z1) || z0 < min_depth || z0 > max_depth || z1 < min_depth)
        return false;

    const Eigen::Vector3d p0 = z0 * d0;
    const Eigen::Vector3d p1 = t01 + z1 * d1;
    point0 = 0.5 * (p0 + p1);
    return point0.z() > min_depth && point0.z() < max_depth;
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

void FeatureTracker::setMask()
{
    mask = cv::Mat(row, col, CV_8UC1, cv::Scalar(255));

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
        if (mask.at<uchar>(it.second.first) == 255)
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

map<int, vector<pair<int, Eigen::Matrix<double, 7, 1>>>> FeatureTracker::trackImage(double _cur_time, const cv::Mat &_img, const cv::Mat &_img1)
{
    TicToc t_r;
    cur_time = _cur_time;
    cur_img = _img;
    cur_img1 = _img1;  // (F) keep right image accessible to rejectGeoDynamic()
    row = cur_img.rows;
    col = cur_img.cols;
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
        rejectGeoDynamic();

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

void FeatureTracker::rejectGeoDynamic()
{
    auto &cfg = vinsConfig();
    if (!cfg.geodf_enable || !cfg.geodf_hard_reject)
        return;

    const int total = static_cast<int>(cur_pts.size());
    if (total < 8 || prev_pts.size() != cur_pts.size())
        return;

    if (total < cfg.geodf_min_feature_num)
        return;

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

    cv::Mat F;
    vector<uchar> f_status;
    F = cv::findFundamentalMat(un_cur_pts, un_prev_pts, cv::FM_RANSAC,
                               cfg.geodf_ransac_th_px, 0.99, f_status);
    if (F.empty())
        return;

    vector<double> errors(total, 0.0);
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
        errors[i] = sampsonDistance(F, un_cur_pts[i], un_prev_pts[i]);
        scored_errors.push_back(errors[i]);
        if (errors[i] > cfg.geodf_sampson_th)
            sampson_above_th++;
        const bool ransac_outlier = f_status.empty() || f_status[i] == 0;
        left_outlier[i] = ransac_outlier ? 1 : 0;
        if (ransac_outlier)
            ransac_outliers++;
    }

    vector<cv::Point2f> cur_r(total);
    vector<uchar> cur_r_valid(total, 0);
    if ((cfg.geodf_stereo_check || cfg.geodf_motion3d_enable) && stereo_cam &&
        m_camera.size() > 1 && !cur_img1.empty() && !prev_right_pts_map.empty())
    {
        vector<uchar> st;
        vector<float> er;
        cv::calcOpticalFlowPyrLK(cur_img, cur_img1, cur_pts, cur_r, st, er,
                                 cv::Size(21, 21), 3);
        vector<cv::Point2f> reverse_left;
        vector<uchar> st_back;
        if (cfg.flow_back)
        {
            cv::calcOpticalFlowPyrLK(cur_img1, cur_img, cur_r, reverse_left, st_back, er,
                                     cv::Size(21, 21), 3);
        }
        for (int i = 0; i < total; i++)
        {
            bool ok = i < static_cast<int>(st.size()) && st[i] && inBorder(cur_r[i]);
            if (ok && cfg.flow_back)
                ok = i < static_cast<int>(st_back.size()) && st_back[i] &&
                     distance(cur_pts[i], reverse_left[i]) <= 0.5;
            cur_r_valid[i] = ok ? 1 : 0;
        }
    }

    // (F) Stereo temporal cross-check: a static point is consistent with BOTH the
    // left and right temporal epipolar geometry. This remains as a fallback when
    // stereo 3D motion consistency is unavailable.
    vector<double> right_err(total, 0.0);
    vector<uchar> right_outlier(total, 0);
    vector<uchar> right_valid(total, 0);
    if (cfg.geodf_stereo_check && stereo_cam && m_camera.size() > 1 &&
        !cur_img1.empty() && !prev_right_pts_map.empty())
    {
        vector<cv::Point2f> un_cr, un_pr;
        vector<int> ref_idx;
        un_cr.reserve(total);
        un_pr.reserve(total);
        ref_idx.reserve(total);
        for (int i = 0; i < total; i++)
        {
            if (track_cnt[i] < cfg.geodf_min_track_cnt)
                continue;
            if (!cur_r_valid[i])
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
                    right_err[i] = sampsonDistance(Fr, un_cr[k], un_pr[k]);
                    right_outlier[i] = (r_status.empty() || r_status[k] == 0) ? 1 : 0;
                    right_valid[i] = 1;
                }
            }
        }
    }

    vector<double> motion3d_residual(total, 0.0);
    vector<uchar> motion3d_valid(total, 0);
    vector<uchar> motion3d_outlier_mask(total, 0);
    int motion3d_valid_count = 0;
    int motion3d_outliers = 0;
    double motion3d_median_residual = 0.0;
    bool motion3d_used = false;
    if (cfg.geodf_motion3d_enable && stereo_cam && m_camera.size() > 1 &&
        !cur_img1.empty() && !prev_right_pts_map.empty() &&
        vinsConfig().ric.size() >= 2 && vinsConfig().tic.size() >= 2)
    {
        const Eigen::Matrix3d R01 = vinsConfig().ric[0].transpose() * vinsConfig().ric[1];
        const Eigen::Vector3d t01 = vinsConfig().ric[0].transpose() *
                                    (vinsConfig().tic[1] - vinsConfig().tic[0]);
        vector<cv::Point3f> prev3d;
        vector<cv::Point2f> cur2d;
        vector<int> ref_idx;
        prev3d.reserve(total);
        cur2d.reserve(total);
        ref_idx.reserve(total);
        for (int i = 0; i < total; i++)
        {
            if (track_cnt[i] < cfg.geodf_min_track_cnt)
                continue;
            auto it = prev_right_pts_map.find(ids[i]);
            if (it == prev_right_pts_map.end())
                continue;

            Eigen::Vector3d prev_l, prev_r;
            m_camera[0]->liftProjective(Eigen::Vector2d(prev_pts[i].x, prev_pts[i].y), prev_l);
            m_camera[1]->liftProjective(Eigen::Vector2d(it->second.x, it->second.y), prev_r);

            Eigen::Vector3d p_prev;
            if (!triangulateStereoPoint(prev_l, prev_r, R01, t01,
                                        cfg.geodf_motion3d_min_depth,
                                        cfg.geodf_motion3d_max_depth, p_prev))
                continue;
            prev3d.emplace_back(static_cast<float>(p_prev.x()),
                                static_cast<float>(p_prev.y()),
                                static_cast<float>(p_prev.z()));
            cur2d.emplace_back(un_cur_pts[i].x, un_cur_pts[i].y);
            ref_idx.push_back(i);
        }

        motion3d_valid_count = static_cast<int>(ref_idx.size());
        if (motion3d_valid_count >= cfg.geodf_motion3d_min_points)
        {
            cv::Mat rvec, tvec, inliers;
            cv::Mat K = (cv::Mat_<double>(3, 3) << FOCAL_LENGTH, 0.0, col / 2.0,
                         0.0, FOCAL_LENGTH, row / 2.0,
                         0.0, 0.0, 1.0);
            cv::setRNGSeed(0x47454f44);
            const bool ok = cv::solvePnPRansac(prev3d, cur2d, K, cv::Mat(), rvec, tvec,
                                               false,
                                               std::max(1, cfg.geodf_motion3d_ransac_iters),
                                               static_cast<float>(cfg.geodf_motion3d_residual_th),
                                               0.99, inliers, cv::SOLVEPNP_EPNP);
            if (ok && inliers.rows >= std::max(6, cfg.geodf_motion3d_min_points / 2))
            {
                vector<cv::Point2f> projected;
                cv::projectPoints(prev3d, rvec, tvec, K, cv::Mat(), projected);
                vector<double> residuals;
                residuals.reserve(motion3d_valid_count);
                for (int k = 0; k < motion3d_valid_count; k++)
                {
                    const int i = ref_idx[k];
                    const double res = cv::norm(projected[k] - cur2d[k]);
                    motion3d_residual[i] = res;
                    motion3d_valid[i] = 1;
                    residuals.push_back(res);
                    if (res > cfg.geodf_motion3d_residual_th)
                    {
                        motion3d_outlier_mask[i] = 1;
                        motion3d_outliers++;
                    }
                }
                motion3d_median_residual = medianValue(residuals);
                motion3d_used = true;
            }
        }
    }

    // Candidate = dynamic. Prefer stereo 3D motion consistency when enough
    // triangulated correspondences are available; otherwise fall back to temporal
    // epipolar dual-gates.
    // Scene gate: only trust the (noisier) right-view branch when the scene's
    // epipolar-outlier floor is low enough that the geometry is reliable. The
    // member geo_outlier_floor still holds the previous frame's (smooth) estimate.
    const bool stereo_trust =
        cfg.geodf_stereo_check &&
        (cfg.geodf_stereo_floor_max <= 0.0 || geo_outlier_floor < 0.0 ||
         geo_outlier_floor <= cfg.geodf_stereo_floor_max);
    vector<int> candidates;
    candidates.reserve(total);
    vector<uchar> candidate_mask(total, 0);
    int stereo_added = 0;
    for (int i = 0; i < total; i++)
    {
        if (track_cnt[i] < cfg.geodf_min_track_cnt)
            continue;
        if (motion3d_used)
        {
            if (motion3d_valid[i] && motion3d_outlier_mask[i])
            {
                candidates.push_back(i);
                candidate_mask[i] = 1;
            }
            continue;
        }
        const bool left_cand = left_outlier[i] && (errors[i] > cfg.geodf_sampson_th);
        const bool right_cand = stereo_trust && right_valid[i] &&
                                right_outlier[i] && (right_err[i] > cfg.geodf_stereo_sampson_th);
        if (left_cand || right_cand)
        {
            candidates.push_back(i);
            candidate_mask[i] = 1;
            if (!left_cand)
                stereo_added++;
        }
    }

    double mean_sampson = 0.0, median_sampson = 0.0, max_sampson = 0.0;
    vector<double> candidate_sampson;
    vector<double> background_sampson;
    candidate_sampson.reserve(candidates.size());
    background_sampson.reserve(scored_errors.size());
    for (int i = 0; i < total; i++)
    {
        if (track_cnt[i] < cfg.geodf_min_track_cnt)
            continue;
        double evidence_error = std::max(errors[i], right_valid[i] ? right_err[i] : 0.0);
        if (motion3d_used && motion3d_valid[i])
            evidence_error = cfg.geodf_sampson_th *
                             motion3d_residual[i] /
                             std::max(1e-6, cfg.geodf_motion3d_residual_th);
        if (candidate_mask[i])
            candidate_sampson.push_back(evidence_error);
        else
            background_sampson.push_back(evidence_error);
    }
    if (!scored_errors.empty())
    {
        double sum = 0.0;
        for (double e : scored_errors)
            sum += e;
        mean_sampson = sum / scored_errors.size();
        std::sort(scored_errors.begin(), scored_errors.end());
        const size_t n = scored_errors.size();
        median_sampson = (n % 2 == 0)
                             ? 0.5 * (scored_errors[n / 2 - 1] + scored_errors[n / 2])
                             : scored_errors[n / 2];
        max_sampson = scored_errors.back();
    }

    const double frame_2d_outlier_ratio =
        scored > 0 ? static_cast<double>(ransac_outliers) / scored : 0.0;
    const double motion3d_2d_support_ratio =
        geo_activation_active
            ? cfg.geodf_motion3d_min_2d_ratio
            : std::max(cfg.geodf_motion3d_min_2d_ratio,
                       cfg.geodf_motion3d_arm_2d_ratio);
    const bool motion3d_supported =
        !motion3d_used || motion3d_2d_support_ratio <= 0.0 ||
        frame_2d_outlier_ratio >= motion3d_2d_support_ratio;
    const double frame_outlier_ratio =
        (motion3d_used && !motion3d_supported)
            ? 0.0
            :
        motion3d_used
            ? static_cast<double>(motion3d_outliers) / std::max(1, motion3d_valid_count)
            : frame_2d_outlier_ratio;
    const size_t candidates_raw = candidates.size();
    const double frame_candidate_ratio =
        scored > 0 ? static_cast<double>(candidates_raw) / scored : 0.0;
    const double median_candidate_sampson = medianValue(candidate_sampson);
    const double median_background_sampson = medianValue(background_sampson);
    const double background_scale = std::max(cfg.geodf_sampson_th, median_background_sampson);
    const double residual_lift =
        median_candidate_sampson > 0.0
            ? median_candidate_sampson / std::max(1e-6, background_scale)
            : 0.0;
    const double ratio_quality =
        cfg.geodf_min_candidate_ratio > 0.0
            ? clamp01(frame_candidate_ratio / cfg.geodf_min_candidate_ratio)
            : 1.0;
    const double lift_quality =
        cfg.geodf_min_residual_lift > 0.0
            ? clamp01(residual_lift / cfg.geodf_min_residual_lift)
            : 1.0;
    const double quality_score = (candidate_sampson.empty() ||
                                  (motion3d_used && !motion3d_supported))
                                     ? 0.0
                                     : ratio_quality * lift_quality;
    if (geo_quality_ema < 0.0)
        geo_quality_ema = quality_score;
    else
        geo_quality_ema = cfg.geodf_quality_ema * quality_score +
                          (1.0 - cfg.geodf_quality_ema) * geo_quality_ema;

    if (geo_activation_ema < 0.0)
        geo_activation_ema = frame_outlier_ratio;
    else
        geo_activation_ema = cfg.geodf_activate_ema * frame_outlier_ratio +
                             (1.0 - cfg.geodf_activate_ema) * geo_activation_ema;

    // (B) Update a running estimate of the scene's static epipolar-outlier floor:
    // adapt down quickly (to catch genuinely static stretches) and creep up slowly
    // (so transient dynamics do not inflate it). The arm threshold then sits a
    // margin above this floor, so a high-noise scene (e.g. dense traffic) needs a
    // proportionally higher outlier ratio to arm — fixing fixed-threshold over-arming.
    if (geo_outlier_floor < 0.0)
        geo_outlier_floor = frame_outlier_ratio;
    else
    {
        const double b = (frame_outlier_ratio < geo_outlier_floor)
                             ? cfg.geodf_auto_floor_down
                             : cfg.geodf_auto_floor_up;
        geo_outlier_floor = b * frame_outlier_ratio + (1.0 - b) * geo_outlier_floor;
    }

    double rho_on = cfg.geodf_activate_ratio;
    if (cfg.geodf_auto_rho)
    {
        rho_on = geo_outlier_floor * cfg.geodf_auto_mult + cfg.geodf_auto_margin;
        if (rho_on < cfg.geodf_activate_ratio_min)
            rho_on = cfg.geodf_activate_ratio_min;
        if (rho_on > cfg.geodf_activate_ratio_max)
            rho_on = cfg.geodf_activate_ratio_max;
    }
    const double rho_off = rho_on * cfg.geodf_deactivate_frac;

    int frame_active = 1;
    if (cfg.geodf_adaptive)
    {
        const bool ratio_active = geo_activation_active
                                      ? (geo_activation_ema >= rho_off)
                                      : (geo_activation_ema >= rho_on);
        bool quality_active = true;
        if (cfg.geodf_quality_gate)
        {
            const double quality_off = cfg.geodf_quality_min * cfg.geodf_deactivate_frac;
            quality_active = geo_activation_active
                                 ? (geo_quality_ema >= quality_off)
                                 : (geo_quality_ema >= cfg.geodf_quality_min);
        }
        geo_activation_active = ratio_active && quality_active && motion3d_supported;
        frame_active = geo_activation_active ? 1 : 0;
        if (!frame_active)
            candidates.clear();
    }

    // Track-level temporal voting: only features flagged on >= vote_frames
    // consecutive frames are eligible for hard-delete. We always advance the
    // per-id streak (incrementing this frame's candidates, dropping the rest),
    // so transient false positives -- 1-frame epipolar spikes from fast rotation
    // or low parallax that dominate static / low-dynamic scenes -- never reach the
    // threshold and are kept, protecting local accuracy (RPE); persistent dynamics
    // accumulate and are removed (with a <= vote_frames-1 frame delay).
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

    vector<int> confirmed;
    confirmed.reserve(candidates.size());
    if (!warmup)
    {
        for (int idx : candidates)
        {
            if (vote_k <= 1 || !have_ids)
            {
                confirmed.push_back(idx);
                continue;
            }
            const auto it = geo_dyn_streak.find(ids[idx]);
            if (it != geo_dyn_streak.end() && it->second >= vote_k)
                confirmed.push_back(idx);
        }
    }

    vector<uchar> keep(total, 1);
    int rejected = 0;
    int guard_triggered = 0;
    int guard_capped = 0;
    int reject_limit = 0;
    if (!confirmed.empty())
    {
        sort(confirmed.begin(), confirmed.end(), [&](int a, int b) {
            const double sa = std::max(errors[a], right_valid[a] ? right_err[a] : 0.0);
            const double sb = std::max(errors[b], right_valid[b] ? right_err[b] : 0.0);
            return sa > sb;
        });

        int max_reject = static_cast<int>(confirmed.size());
        if (cfg.geodf_ratio_guard)
        {
            const int ratio_cap = static_cast<int>(std::floor(cfg.geodf_max_reject_ratio * total));
            if (static_cast<int>(confirmed.size()) > ratio_cap)
                guard_triggered = 1;
            max_reject = ratio_cap;
        }

        const int max_reject_by_min = std::max(0, total - cfg.geodf_min_feature_num);
        max_reject = std::min(max_reject, max_reject_by_min);
        if (cfg.geodf_max_reject_per_frame > 0)
            max_reject = std::min(max_reject, cfg.geodf_max_reject_per_frame);
        reject_limit = max_reject;

        for (int idx : confirmed)
        {
            if (rejected >= max_reject)
                break;
            keep[idx] = 0;
            rejected++;
        }
        if (static_cast<int>(confirmed.size()) > rejected)
            guard_capped = 1;
    }
    const size_t confirmed_n = confirmed.size();

    if (cfg.geodf_dump_features && !cfg.geodf_feat_path.empty())
    {
        std::ofstream feat(cfg.geodf_feat_path, std::ios::app);
        const long long ts = static_cast<long long>(cur_time * 1e9);
        for (int i = 0; i < total; i++)
        {
            if (track_cnt[i] < cfg.geodf_min_track_cnt)
                continue;
            const int ransac_outlier = (f_status.empty() || f_status[i] == 0) ? 1 : 0;
            feat << ts << "," << ids[i] << ","
                 << cur_pts[i].x << "," << cur_pts[i].y << ","
                 << errors[i] << "," << ransac_outlier << ","
                 << (keep[i] ? 0 : 1) << "\n";
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

    if (!cfg.geodf_stats_path.empty())
    {
        std::ofstream geo_stats(cfg.geodf_stats_path, std::ios::app);
        const double ratio = total > 0 ? static_cast<double>(rejected) / total : 0.0;
        geo_stats << static_cast<long long>(cur_time * 1e9) << ","
                  << total << "," << scored << "," << ransac_outliers << ","
                  << sampson_above_th << "," << candidates_raw << ","
                  << rejected << "," << ratio << "," << cur_pts.size() << ","
                  << mean_sampson << "," << median_sampson << "," << max_sampson << ","
                  << guard_triggered << "," << guard_capped << ","
                  << geo_activation_ema << "," << frame_active << ","
                  << t_geo.toc() << "," << rho_on << "," << geo_outlier_floor << ","
                  << stereo_added << "," << confirmed_n << ","
                  << frame_outlier_ratio << "," << frame_candidate_ratio << ","
                  << quality_score << "," << geo_quality_ema << "," << residual_lift << ","
                  << median_candidate_sampson << "," << median_background_sampson << ","
                  << reject_limit << ","
                  << motion3d_valid_count << "," << motion3d_outliers << ","
                  << motion3d_median_residual << "," << (motion3d_used ? 1 : 0) << "\n";
    }

    if (cfg.geodf_debug)
    {
        static int debug_cnt = 0;
        if (debug_cnt++ % 50 == 0)
        {
            ROS_INFO("GeoDF: reject %d/%d (cand %zu scored %d guard %d) %.2fms",
                     rejected, total, candidates.size(), scored, guard_triggered, t_geo.toc());
        }
    }
    else
    {
        ROS_DEBUG("GeoDF reject: %d/%d (cand %zu) cost %fms",
                  rejected, total, candidates.size(), t_geo.toc());
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
