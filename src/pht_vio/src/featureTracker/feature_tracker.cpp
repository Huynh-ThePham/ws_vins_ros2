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

// GeoDF-Inertial (Paper #2): build the pseudo-pixel fundamental matrix from the
// IMU/VINS-predicted relative camera pose (E = [t]_x R). The convention matches
// sampsonDistance(F, cur, prev), i.e. prev^T F cur = 0, so the inertial path and
// the feature-fit path share the same scoring code. Sampson distance is
// invariant to the scale of F, so t is normalized for numerical cleanliness.
cv::Mat imuFundamental(const Eigen::Matrix3d &R_rel, const Eigen::Vector3d &t_rel,
                       double f, double cx, double cy)
{
    Eigen::Vector3d t = t_rel;
    const double n = t.norm();
    if (n < 1e-9)
        return cv::Mat();
    t /= n;
    Eigen::Matrix3d tx;
    tx <<    0.0, -t.z(),  t.y(),
           t.z(),    0.0, -t.x(),
          -t.y(),  t.x(),    0.0;
    const Eigen::Matrix3d E = tx * R_rel;            // x_cur^T E x_prev = 0
    Eigen::Matrix3d Kinv;
    Kinv << 1.0 / f,     0.0, -cx / f,
                0.0, 1.0 / f, -cy / f,
                0.0,     0.0,     1.0;
    const Eigen::Matrix3d Feig = Kinv.transpose() * E.transpose() * Kinv;
    cv::Mat F(3, 3, CV_64F);
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            F.at<double>(r, c) = Feig(r, c);
    return F;
}

// Pure-rotation pseudo-pixel homography H = K R K^{-1}. Predicts where a
// previous pixel lands under the predicted rotation only; the residual versus
// the observed pixel reveals independently moving points when translation
// parallax is too small for epipolar geometry to be reliable.
cv::Mat imuRotationHomography(const Eigen::Matrix3d &R_rel,
                              double f, double cx, double cy)
{
    Eigen::Matrix3d K;
    K << f, 0.0, cx,
         0.0, f, cy,
         0.0, 0.0, 1.0;
    Eigen::Matrix3d Kinv;
    Kinv << 1.0 / f,     0.0, -cx / f,
                0.0, 1.0 / f, -cy / f,
                0.0,     0.0,     1.0;
    const Eigen::Matrix3d Heig = K * R_rel * Kinv;
    cv::Mat H(3, 3, CV_64F);
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            H.at<double>(r, c) = Heig(r, c);
    return H;
}

cv::Point2f applyHomography(const cv::Mat &H, const cv::Point2f &p)
{
    const double x = H.at<double>(0, 0) * p.x + H.at<double>(0, 1) * p.y + H.at<double>(0, 2);
    const double y = H.at<double>(1, 0) * p.x + H.at<double>(1, 1) * p.y + H.at<double>(1, 2);
    const double w = H.at<double>(2, 0) * p.x + H.at<double>(2, 1) * p.y + H.at<double>(2, 2);
    if (std::fabs(w) < 1e-12)
        return p;
    return cv::Point2f(static_cast<float>(x / w), static_cast<float>(y / w));
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

map<int, vector<pair<int, FeatureObservation>>> FeatureTracker::trackImage(double _cur_time, const cv::Mat &_img, const cv::Mat &_img1)
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

    map<int, vector<pair<int, FeatureObservation>>> featureFrame;
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
        double weight = 1.0;
        auto wit = geo_feature_weights.find(feature_id);
        if (wit != geo_feature_weights.end())
            weight = wit->second;
        FeatureObservation xyz_uv_velocity;
        xyz_uv_velocity << x, y, z, p_u, p_v, velocity_x, velocity_y, weight;
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
            double weight = 1.0;
            auto wit = geo_feature_weights.find(feature_id);
            if (wit != geo_feature_weights.end())
                weight = wit->second;
            FeatureObservation xyz_uv_velocity;
            xyz_uv_velocity << x, y, z, p_u, p_v, velocity_x, velocity_y, weight;
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
        (void)size_a;  // ROS_DEBUG may compile out its arguments in Release builds.
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
    geo_feature_weights.clear();
    if (!cfg.geodf_enable || (!cfg.geodf_hard_reject && !cfg.geodf_backend_weight))
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

    // ---- GeoDF-Hybrid mode selection (Paper #2) ---------------------------
    // Two geometry sources scored with the same Paper #1 back-end (scene gate,
    // voting, ratio guard):
    //   mode 0: feature-fit F (Paper #1 dual gate)
    //   mode 1: IMU-predicted F_imu + Sampson gate
    //   mode 2: gyro-derotated residual flow (low parallax, dynamic scene)
    //
    // Hybrid arbitration (geodf_hybrid_enable): previous-frame P1-sensed outlier
    // floor drives a dwell+hysteresis latch. Below floor_on -> forced Paper #1
    // (hybrid_static_p1); above -> inertial/derotation when IMU is reliable.
    const bool imu_pose_ok = cfg.geodf_imu_enable && imu_epi_valid &&
                             imu_t_norm <= cfg.geodf_imu_parallax_max;
    const bool imu_reliable = imu_pose_ok && imu_t_norm >= cfg.geodf_imu_parallax_min;
    const bool imu_derot_ok = imu_pose_ok && imu_t_norm < cfg.geodf_imu_parallax_min &&
                              cfg.geodf_imu_derotate;

    // Arbitration signal: the slow, Paper #1-derived epipolar-outlier floor ONLY.
    // Earlier designs max-combined a fast outlier-ratio cue and the activation EMA;
    // both are dominated by single-frame KLT/rotation spikes and pushed the signal
    // over the threshold on static scenes, forcing spurious inertial switches that
    // deleted good static features (e.g. city_night/0_none regressed from 0.246 to
    // ~0.38 m). The asymmetric floor (fast-down, slow-up) only stays high under
    // SUSTAINED dynamic density -- precisely the regime where the feature-fit
    // fundamental matrix is contaminated and the inertial model is the better
    // rigidity reference.
    const double hybrid_signal = (geo_outlier_floor >= 0.0) ? geo_outlier_floor : 0.0;
    const bool hybrid_on = cfg.geodf_hybrid_enable && cfg.geodf_imu_enable;
    // Hysteresis latch with anti-chatter dwell. The latch flips only after the
    // signal has stayed on the new side of the threshold for `dwell` CONSECUTIVE
    // frames, so arbitration keys off the SUSTAINED outlier floor (the scene's
    // dynamic regime) rather than single-frame excursions. This is what separates
    // adjacent regimes whose instantaneous floors overlap: e.g. parking_lot 2_mid
    // (floor median ~0.069, inertial unhelpful) must stay on Paper #1 while 3_high
    // (median ~0.091, inertial recovers a catastrophic feature-fit failure) must
    // latch inertial -- a per-frame threshold chatters between them, the dwell
    // resolves them by their sustained level. Upper threshold floor_on, lower
    // (return) threshold floor_off (< floor_on); floor_off < 0 or >= floor_on
    // collapses to a single threshold.
    const double floor_on = cfg.geodf_hybrid_inertial_floor;
    double floor_off = cfg.geodf_hybrid_floor_off;
    if (floor_off < 0.0 || floor_off > floor_on)
        floor_off = floor_on;
    if (hybrid_on)
    {
        const int dwell = std::max(1, cfg.geodf_hybrid_dwell);
        if (!geo_hybrid_dynamic_active)
        {
            geo_hybrid_dwell_cnt = (hybrid_signal >= floor_on) ? geo_hybrid_dwell_cnt + 1 : 0;
            if (geo_hybrid_dwell_cnt >= dwell)
            {
                geo_hybrid_dynamic_active = true;
                geo_hybrid_dwell_cnt = 0;
            }
        }
        else
        {
            geo_hybrid_dwell_cnt = (hybrid_signal < floor_off) ? geo_hybrid_dwell_cnt + 1 : 0;
            if (geo_hybrid_dwell_cnt >= dwell)
            {
                geo_hybrid_dynamic_active = false;
                geo_hybrid_dwell_cnt = 0;
            }
        }
    }
    else
    {
        geo_hybrid_dynamic_active = false;
        geo_hybrid_dwell_cnt = 0;
    }
    const bool scene_dynamic = geo_hybrid_dynamic_active;
    const bool hybrid_static_p1 = hybrid_on && !scene_dynamic;
    const bool arb_force_p1 = hybrid_static_p1;
    const bool use_inertial = cfg.geodf_imu_enable && imu_reliable &&
                              (!hybrid_on || scene_dynamic);
    const bool use_derot = cfg.geodf_imu_enable && imu_derot_ok &&
                           (!hybrid_on || scene_dynamic);

    int mode = 0;
    int hybrid_arb = 0;  // 0=n/a, 1=hybrid->P1, 2=hybrid->inertial, 3=hybrid->derot
    double tau_eff = cfg.geodf_sampson_th;
    cv::Mat F;
    vector<uchar> f_status;
    cv::Mat H_derot;

    // When the latch keeps the scene on Paper #1, skip IMU/derot geometry entirely
    // so the rejection path matches the adaptive config (no inertial F setup).
    if (!hybrid_static_p1)
    {
        if (use_inertial)
        {
            F = imuFundamental(imu_R_rel, imu_t_rel, FOCAL_LENGTH, col / 2.0, row / 2.0);
            if (!F.empty())
            {
                mode = 1;
                if (hybrid_on)
                    hybrid_arb = 2;
                double scale = cfg.geodf_imu_parallax_ref / std::max(imu_t_norm, 1e-6);
                scale = std::min(std::max(scale, 1.0), cfg.geodf_imu_tau_cap);
                tau_eff = cfg.geodf_imu_sampson_th * scale;
            }
        }
        else if (use_derot)
        {
            H_derot = imuRotationHomography(imu_R_rel, FOCAL_LENGTH, col / 2.0, row / 2.0);
            if (!H_derot.empty())
            {
                mode = 2;
                if (hybrid_on)
                    hybrid_arb = 3;
                tau_eff = cfg.geodf_imu_derotate_px;
            }
        }
    }

    if (mode == 0)
    {
        const bool inertial_only_skip =
            cfg.geodf_imu_enable && !cfg.geodf_hybrid_enable && !cfg.geodf_imu_fallback;
        if (inertial_only_skip)
            return;
    }

    // Paper #1 feature-fit F and hybrid arbitration sensor F_p1 share ONE RANSAC
    // when rejection stays on Paper #1 (mode 0, including hybrid_static_p1). A
    // separate estimate was previously taken whenever hybrid_on, even on forced-P1
    // frames; that duplicated the adaptive call pattern, changed RANSAC ordering
    // relative to the pure-P1 config, and made latch=0% runs diverge from Paper #1
    // (e.g. city_night extra rejections despite mode0=100%). On dynamic frames the
    // inertial/derot geometry performs rejection; F_p1 is estimated once more here
    // ONLY for the arbitration sensor (outlier ratio), not for deleting features.
    cv::Mat F_p1;
    vector<uchar> f_status_p1;
    const bool p1_sensor_only = hybrid_on && mode != 0;

    if (mode == 0)
    {
        F = cv::findFundamentalMat(un_cur_pts, un_prev_pts, cv::FM_RANSAC,
                                   cfg.geodf_ransac_th_px, 0.99, f_status);
        if (F.empty())
            return;
        tau_eff = cfg.geodf_sampson_th;
        if (hybrid_on)
        {
            F_p1 = F;
            f_status_p1 = f_status;
            if (arb_force_p1)
                hybrid_arb = 1;
        }
    }
    else if (p1_sensor_only)
    {
        F_p1 = cv::findFundamentalMat(un_cur_pts, un_prev_pts, cv::FM_RANSAC,
                                      cfg.geodf_ransac_th_px, 0.99, f_status_p1);
    }

    vector<double> errors(total, 0.0);
    vector<uchar> left_outlier(total, 0);
    vector<double> scored_errors;
    scored_errors.reserve(total);
    int scored = 0;
    int ransac_outliers = 0;
    int sampson_above_th = 0;

    // Pass 1: per-feature residual against the chosen geometry.
    for (int i = 0; i < total; i++)
    {
        if (track_cnt[i] < cfg.geodf_min_track_cnt)
            continue;
        scored++;
        if (mode == 2)
        {
            const cv::Point2f pred = applyHomography(H_derot, un_prev_pts[i]);
            const double dx = un_cur_pts[i].x - pred.x;
            const double dy = un_cur_pts[i].y - pred.y;
            errors[i] = std::sqrt(dx * dx + dy * dy);
        }
        else
        {
            errors[i] = sampsonDistance(F, un_cur_pts[i], un_prev_pts[i]);
        }
        scored_errors.push_back(errors[i]);
    }

    // Robust per-frame scale gate (inertial modes only): widen the threshold
    // when the whole frame's residual is elevated (a transient bad IMU pose
    // shifts every epipolar line together), so static scenes are not
    // mass-rejected; genuine dynamics keep a low static median and are caught.
    if (mode != 0 && cfg.geodf_imu_median_mult > 0.0 && !scored_errors.empty())
    {
        std::vector<double> tmp(scored_errors);
        const size_t midx = tmp.size() / 2;
        std::nth_element(tmp.begin(), tmp.begin() + midx, tmp.end());
        const double med = tmp[midx];
        tau_eff = std::max(tau_eff, cfg.geodf_imu_median_mult * med);
    }

    // Pass 2: outlier flags and frame counts at the (robust) threshold.
    for (int i = 0; i < total; i++)
    {
        if (track_cnt[i] < cfg.geodf_min_track_cnt)
            continue;
        if (errors[i] > tau_eff)
            sampson_above_th++;
        const bool outlier = (mode == 0)
                                 ? (f_status.empty() || f_status[i] == 0)
                                 : (errors[i] > tau_eff);
        left_outlier[i] = outlier ? 1 : 0;
        if (outlier)
            ransac_outliers++;
    }

    // (F) Stereo temporal cross-check: a static point is consistent with BOTH the
    // left and right temporal epipolar geometry. A dynamic point that happens to
    // slide along the left epipolar line (Sampson_left ~ 0) is generally still an
    // outlier in the right view, whose epipolar geometry differs by the stereo
    // baseline. We track cur-left -> cur-right and pair with the previous frame's
    // right pixel (by id) to estimate a right temporal F and score Sampson_right.
    vector<double> right_err(total, 0.0);
    vector<uchar> right_outlier(total, 0);
    vector<uchar> right_valid(total, 0);
    if (mode == 0 && cfg.geodf_stereo_check && stereo_cam && m_camera.size() > 1 &&
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
                    right_err[i] = sampsonDistance(Fr, un_cr[k], un_pr[k]);
                    right_outlier[i] = (r_status.empty() || r_status[k] == 0) ? 1 : 0;
                    right_valid[i] = 1;
                }
            }
        }
    }

    // Candidate = dynamic if EITHER view's temporal dual-gate (RANSAC outlier AND
    // Sampson > threshold) fires. The right branch raises recall on dynamics the
    // left epipolar geometry misses, while each branch keeps its own dual gate.
    // Scene gate: only trust the (noisier) right-view branch when the scene's
    // epipolar-outlier floor is low enough that the geometry is reliable. The
    // member geo_outlier_floor still holds the previous frame's (smooth) estimate.
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
        const bool left_cand = left_outlier[i] && (errors[i] > tau_eff);
        const bool right_cand = stereo_trust && right_valid[i] &&
                                right_outlier[i] && (right_err[i] > cfg.geodf_stereo_sampson_th);
        if (left_cand || right_cand)
        {
            candidates.push_back(i);
            if (!left_cand)
                stereo_added++;
        }
    }

    double mean_sampson = 0.0, median_sampson = 0.0, max_sampson = 0.0;
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

    const double frame_outlier_ratio =
        scored > 0 ? static_cast<double>(ransac_outliers) / scored : 0.0;

    // Hybrid arbitration sensor ratio: the Paper #1 feature-fit RANSAC outlier
    // ratio, measured every frame from F_p1 regardless of the rejection geometry.
    // In mode 0 it already equals frame_outlier_ratio; in inertial/derotation
    // frames it is taken from the separately-estimated F_p1 so the floor and the
    // latch never observe the inertial residual distribution. Outside hybrid it
    // stays frame_outlier_ratio, preserving Paper #1 and the inertial-only
    // ablation byte-for-byte.
    double arb_ratio = frame_outlier_ratio;
    if (hybrid_on && mode != 0 && !f_status_p1.empty())
    {
        int p1_out = 0;
        for (int i = 0; i < total; i++)
        {
            if (track_cnt[i] < cfg.geodf_min_track_cnt)
                continue;
            if (f_status_p1[i] == 0)
                p1_out++;
        }
        arb_ratio = scored > 0 ? static_cast<double>(p1_out) / scored : 0.0;
    }

    // Reliability skip (inertial modes): when the IMU-predicted geometry would
    // reject more than max_dyn_frac of the frame, the rigid-scene model explains
    // too little of the scene. This is either a transient corrupted IMU pose (a
    // whole-frame epipolar shift) or a dynamics-saturated frame where front-end
    // rejection starves the estimator; in both cases we skip rejection here and
    // freeze the scene EMA/floor so the bad frame cannot fool the scene gate.
    const bool pose_unreliable =
        (mode != 0) && cfg.geodf_imu_max_dyn_frac > 0.0 &&
        frame_outlier_ratio > cfg.geodf_imu_max_dyn_frac;

    // Scene-activation EMA and the static floor both track the P1-sensed ratio
    // (arb_ratio), so the Paper #1 scene gate and the hybrid latch are driven by
    // one consistent dynamic-density signal.
    if (!pose_unreliable)
    {
        if (geo_activation_ema < 0.0)
            geo_activation_ema = arb_ratio;
        else
            geo_activation_ema = cfg.geodf_activate_ema * arb_ratio +
                                 (1.0 - cfg.geodf_activate_ema) * geo_activation_ema;
    }

    // (B) Update a running estimate of the scene's static epipolar-outlier floor:
    // adapt down quickly (to catch genuinely static stretches) and creep up slowly
    // (so transient dynamics do not inflate it). The arm threshold then sits a
    // margin above this floor, so a high-noise scene (e.g. dense traffic) needs a
    // proportionally higher outlier ratio to arm — fixing fixed-threshold over-arming.
    if (!pose_unreliable)
    {
        if (geo_outlier_floor < 0.0)
            geo_outlier_floor = arb_ratio;
        else
        {
            const double b = (arb_ratio < geo_outlier_floor)
                                 ? cfg.geodf_auto_floor_down
                                 : cfg.geodf_auto_floor_up;
            geo_outlier_floor = b * arb_ratio + (1.0 - b) * geo_outlier_floor;
        }
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

    const size_t candidates_raw = candidates.size();
    int frame_active = 1;
    if (cfg.geodf_adaptive)
    {
        geo_activation_active = geo_activation_active
                                    ? (geo_activation_ema >= rho_off)
                                    : (geo_activation_ema >= rho_on);
        frame_active = geo_activation_active ? 1 : 0;
        if (!frame_active)
            candidates.clear();
    }
    if (pose_unreliable)
    {
        candidates.clear();
        frame_active = 0;
    }

    if (cfg.geodf_backend_weight)
    {
        const double min_weight = std::min(1.0, std::max(0.0, cfg.geodf_backend_min_weight));
        const double power = std::max(0.1, cfg.geodf_backend_weight_power);
        if (frame_active && tau_eff > 1e-12)
        {
            for (int i = 0; i < total; i++)
            {
                if (track_cnt[i] < cfg.geodf_min_track_cnt)
                    continue;
                const double normalized = std::max(0.0, errors[i] / tau_eff - 1.0);
                const double robust = 1.0 / (1.0 + std::pow(normalized, power));
                geo_feature_weights[ids[i]] = std::max(min_weight, robust);
            }
        }
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
            const int ransac_outlier = left_outlier[i];
            double weight = 1.0;
            auto wit = geo_feature_weights.find(ids[i]);
            if (wit != geo_feature_weights.end())
                weight = wit->second;
            feat << ts << "," << ids[i] << ","
                 << cur_pts[i].x << "," << cur_pts[i].y << ","
                 << errors[i] << "," << ransac_outlier << ","
                 << (keep[i] ? 0 : 1) << "," << weight << "\n";
        }
    }

    if (!cfg.geodf_hard_reject)
        rejected = 0;

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
                  << mode << "," << tau_eff << "," << imu_t_norm << ","
                  << hybrid_signal << ","
                  << hybrid_arb << "," << (scene_dynamic ? 1 : 0) << "\n";
    }

    if (cfg.geodf_debug)
    {
        static int debug_cnt = 0;
        if (debug_cnt++ % 50 == 0)
        {
            ROS_INFO("GeoDF: reject %d/%d (cand %zu mode %d arb %d signal %.3f floor %.3f) %.2fms",
                     rejected, total, candidates.size(), mode, hybrid_arb,
                     hybrid_signal, geo_outlier_floor, t_geo.toc());
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

void FeatureTracker::setImuEpipolar(const Eigen::Matrix3d &R_rel,
                                    const Eigen::Vector3d &t_rel, bool valid)
{
    imu_R_rel = R_rel;
    imu_t_rel = t_rel;
    imu_t_norm = t_rel.norm();
    imu_epi_valid = valid;
}

bool FeatureTracker::hybridNeedImuEpipolar() const
{
    const VinsConfig &cfg = vinsConfig();
    if (!cfg.geodf_hybrid_enable || !cfg.geodf_imu_enable)
        return true;

    if (geo_hybrid_dynamic_active)
        return true;

    const double sig = (geo_outlier_floor >= 0.0) ? geo_outlier_floor : 0.0;
    const double floor_on = cfg.geodf_hybrid_inertial_floor;
    // Precharge two frames before the dwell completes: the first re-establishes the
    // inter-image anchor after a static-P1 gap (no push during static), the second
    // delivers a valid relative pose on the frame the latch turns ON.
    const int dwell = std::max(1, cfg.geodf_hybrid_dwell);
    const int precharge = std::max(0, dwell - 2);
    if (sig >= floor_on && geo_hybrid_dwell_cnt >= precharge)
        return true;

    return false;
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
