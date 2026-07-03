/*******************************************************
 * Copyright (C) 2019, Aerial Robotics Group, Hong Kong University of Science and Technology
 *
 * VINS configuration loader.
 *******************************************************/

#include "parameters.h"

static VinsConfig g_vins_config;

VinsConfig &vinsConfig()
{
    return g_vins_config;
}

void VinsConfig::reset()
{
    *this = VinsConfig{};
}

bool VinsConfig::loadFromYaml(const std::string &config_file)
{
    reset();

    FILE *fh = fopen(config_file.c_str(), "r");
    if (fh == NULL) {
        ROS_WARN("config_file dosen't exist; wrong config_file path");
        return false;
    }
    fclose(fh);

    cv::FileStorage fsSettings(config_file, cv::FileStorage::READ);
    if (!fsSettings.isOpened()) {
        std::cerr << "ERROR: Wrong path to settings" << std::endl;
        return false;
    }

    fsSettings["image0_topic"] >> image0_topic;
    fsSettings["image1_topic"] >> image1_topic;
    max_cnt = fsSettings["max_cnt"];
    min_dist = fsSettings["min_dist"];
    f_threshold = fsSettings["F_threshold"];
    show_track = fsSettings["show_track"];
    flow_back = fsSettings["flow_back"];

    multiple_thread = fsSettings["multiple_thread"];

    use_imu = fsSettings["imu"];
    printf("USE_IMU: %d\n", use_imu);
    if (use_imu) {
        fsSettings["imu_topic"] >> imu_topic;
        printf("IMU_TOPIC: %s\n", imu_topic.c_str());
        acc_n = fsSettings["acc_n"];
        acc_w = fsSettings["acc_w"];
        gyr_n = fsSettings["gyr_n"];
        gyr_w = fsSettings["gyr_w"];
        g.z() = fsSettings["g_norm"];
    }

    solver_time = fsSettings["max_solver_time"];
    num_iterations = fsSettings["max_num_iterations"];
    min_parallax = fsSettings["keyframe_parallax"];
    min_parallax = min_parallax / FOCAL_LENGTH;

    fsSettings["output_path"] >> output_folder;
    if (!output_folder.empty() && output_folder[0] == '~') {
        const char *home = getenv("HOME");
        if (home)
            output_folder = std::string(home) + output_folder.substr(1);
    }
    vins_result_path = output_folder + "/vio.csv";
    std::cout << "result path " << vins_result_path << std::endl;
    std::ofstream fout(vins_result_path, std::ios::out);
    fout.close();

    estimate_extrinsic = fsSettings["estimate_extrinsic"];
    if (estimate_extrinsic == 2) {
        ROS_WARN("have no prior about extrinsic param, calibrate extrinsic param");
        ric.push_back(Eigen::Matrix3d::Identity());
        tic.push_back(Eigen::Vector3d::Zero());
        ex_calib_result_path = output_folder + "/extrinsic_parameter.csv";
    } else {
        if (estimate_extrinsic == 1) {
            ROS_WARN(" Optimize extrinsic param around initial guess!");
            ex_calib_result_path = output_folder + "/extrinsic_parameter.csv";
        }
        if (estimate_extrinsic == 0)
            ROS_WARN(" fix extrinsic param ");

        cv::Mat cv_T;
        fsSettings["body_T_cam0"] >> cv_T;
        Eigen::Matrix4d T;
        cv::cv2eigen(cv_T, T);
        ric.push_back(T.block<3, 3>(0, 0));
        tic.push_back(T.block<3, 1>(0, 3));
    }

    num_of_cam = fsSettings["num_of_cam"];
    printf("camera number %d\n", num_of_cam);

    if (num_of_cam != 1 && num_of_cam != 2) {
        printf("num_of_cam should be 1 or 2\n");
        assert(0);
    }

    int pn = config_file.find_last_of('/');
    std::string configPath = config_file.substr(0, pn);

    std::string cam0Calib;
    fsSettings["cam0_calib"] >> cam0Calib;
    std::string cam0Path = configPath + "/" + cam0Calib;
    cam_names.push_back(cam0Path);

    if (num_of_cam == 2) {
        stereo = 1;
        std::string cam1Calib;
        fsSettings["cam1_calib"] >> cam1Calib;
        std::string cam1Path = configPath + "/" + cam1Calib;
        cam_names.push_back(cam1Path);

        cv::Mat cv_T;
        fsSettings["body_T_cam1"] >> cv_T;
        Eigen::Matrix4d T;
        cv::cv2eigen(cv_T, T);
        ric.push_back(T.block<3, 3>(0, 0));
        tic.push_back(T.block<3, 1>(0, 3));
    }

    init_depth = 5.0;
    bias_acc_threshold = 0.1;
    bias_gyr_threshold = 0.1;

    td = fsSettings["td"];
    estimate_td = fsSettings["estimate_td"];
    if (estimate_td)
        ROS_INFO("Unsynchronized sensors, online estimate time offset, initial td: %f", td);
    else
        ROS_INFO("Synchronized sensors, fix time offset: %f", td);

    row = fsSettings["image_height"];
    col = fsSettings["image_width"];
    ROS_INFO("ROW: %d COL: %d ", row, col);

    if (!use_imu) {
        estimate_extrinsic = 0;
        estimate_td = 0;
        printf("no imu, fix extrinsic param; no time offset calibration\n");
    }

    geodf_enable = 0;
    geodf_hard_reject = 1;
    geodf_ransac_th_px = 1.0;
    geodf_sampson_th = 3.0;
    geodf_min_track_cnt = 2;
    geodf_min_feature_num = 40;
    geodf_max_reject_ratio = 0.4;
    geodf_ratio_guard = 1;
    geodf_max_reject_per_frame = 0;
    geodf_debug = 0;
    geodf_dump_features = 0;
    geodf_adaptive = 0;
    geodf_activate_ratio = 0.12;
    geodf_activate_ema = 0.15;
    geodf_deactivate_frac = 0.6;
    geodf_auto_rho = 0;
    geodf_auto_mult = 1.8;
    geodf_auto_margin = 0.05;
    geodf_activate_ratio_min = 0.08;
    geodf_activate_ratio_max = 0.40;
    geodf_auto_floor_down = 0.02;
    geodf_auto_floor_up = 0.004;
    geodf_quality_gate = 0;
    geodf_quality_ema = 0.15;
    geodf_quality_min = 0.5;
    geodf_min_candidate_ratio = 0.01;
    geodf_min_residual_lift = 2.0;
    geodf_vote_frames = 1;
    geodf_warmup_frames = 0;
    geodf_stereo_check = 0;
    geodf_stereo_sampson_th = 3.0;
    geodf_stereo_floor_max = 0.0;
    geodf_motion3d_enable = 0;
    geodf_motion3d_min_points = 25;
    geodf_motion3d_min_depth = 0.2;
    geodf_motion3d_max_depth = 40.0;
    geodf_motion3d_residual_th = 3.0;
    geodf_motion3d_ransac_iters = 96;
    geodf_motion3d_min_2d_ratio = 0.0;
    geodf_motion3d_arm_2d_ratio = 0.0;
    if (!fsSettings["geodf_enable"].empty())
        geodf_enable = (int)fsSettings["geodf_enable"];
    if (!fsSettings["geodf_hard_reject"].empty())
        geodf_hard_reject = (int)fsSettings["geodf_hard_reject"];
    if (!fsSettings["geodf_ransac_th_px"].empty())
        geodf_ransac_th_px = (double)fsSettings["geodf_ransac_th_px"];
    if (!fsSettings["geodf_sampson_th"].empty())
        geodf_sampson_th = (double)fsSettings["geodf_sampson_th"];
    else if (!fsSettings["geodf_tau"].empty())
        geodf_sampson_th = (double)fsSettings["geodf_tau"];
    if (!fsSettings["geodf_min_track_cnt"].empty())
        geodf_min_track_cnt = (int)fsSettings["geodf_min_track_cnt"];
    if (!fsSettings["geodf_min_feature_num"].empty())
        geodf_min_feature_num = (int)fsSettings["geodf_min_feature_num"];
    if (!fsSettings["geodf_reject_ratio_max"].empty())
        geodf_max_reject_ratio = (double)fsSettings["geodf_reject_ratio_max"];
    else if (!fsSettings["geodf_max_reject_ratio"].empty())
        geodf_max_reject_ratio = (double)fsSettings["geodf_max_reject_ratio"];
    if (!fsSettings["geodf_ratio_guard"].empty())
        geodf_ratio_guard = (int)fsSettings["geodf_ratio_guard"];
    if (!fsSettings["geodf_max_reject_per_frame"].empty())
        geodf_max_reject_per_frame = (int)fsSettings["geodf_max_reject_per_frame"];
    if (!fsSettings["geodf_debug"].empty())
        geodf_debug = (int)fsSettings["geodf_debug"];
    if (!fsSettings["geodf_dump_features"].empty())
        geodf_dump_features = (int)fsSettings["geodf_dump_features"];
    if (!fsSettings["geodf_adaptive"].empty())
        geodf_adaptive = (int)fsSettings["geodf_adaptive"];
    if (!fsSettings["geodf_activate_ratio"].empty())
        geodf_activate_ratio = (double)fsSettings["geodf_activate_ratio"];
    if (!fsSettings["geodf_activate_ema"].empty())
        geodf_activate_ema = (double)fsSettings["geodf_activate_ema"];
    if (!fsSettings["geodf_deactivate_frac"].empty())
        geodf_deactivate_frac = (double)fsSettings["geodf_deactivate_frac"];
    if (!fsSettings["geodf_auto_rho"].empty())
        geodf_auto_rho = (int)fsSettings["geodf_auto_rho"];
    if (!fsSettings["geodf_auto_mult"].empty())
        geodf_auto_mult = (double)fsSettings["geodf_auto_mult"];
    if (!fsSettings["geodf_auto_margin"].empty())
        geodf_auto_margin = (double)fsSettings["geodf_auto_margin"];
    if (!fsSettings["geodf_activate_ratio_min"].empty())
        geodf_activate_ratio_min = (double)fsSettings["geodf_activate_ratio_min"];
    if (!fsSettings["geodf_activate_ratio_max"].empty())
        geodf_activate_ratio_max = (double)fsSettings["geodf_activate_ratio_max"];
    if (!fsSettings["geodf_auto_floor_down"].empty())
        geodf_auto_floor_down = (double)fsSettings["geodf_auto_floor_down"];
    if (!fsSettings["geodf_auto_floor_up"].empty())
        geodf_auto_floor_up = (double)fsSettings["geodf_auto_floor_up"];
    if (!fsSettings["geodf_quality_gate"].empty())
        geodf_quality_gate = (int)fsSettings["geodf_quality_gate"];
    if (!fsSettings["geodf_quality_ema"].empty())
        geodf_quality_ema = (double)fsSettings["geodf_quality_ema"];
    if (!fsSettings["geodf_quality_min"].empty())
        geodf_quality_min = (double)fsSettings["geodf_quality_min"];
    if (!fsSettings["geodf_min_candidate_ratio"].empty())
        geodf_min_candidate_ratio = (double)fsSettings["geodf_min_candidate_ratio"];
    if (!fsSettings["geodf_min_residual_lift"].empty())
        geodf_min_residual_lift = (double)fsSettings["geodf_min_residual_lift"];
    if (!fsSettings["geodf_vote_frames"].empty())
        geodf_vote_frames = (int)fsSettings["geodf_vote_frames"];
    if (!fsSettings["geodf_warmup_frames"].empty())
        geodf_warmup_frames = (int)fsSettings["geodf_warmup_frames"];
    if (!fsSettings["geodf_stereo_check"].empty())
        geodf_stereo_check = (int)fsSettings["geodf_stereo_check"];
    if (!fsSettings["geodf_stereo_sampson_th"].empty())
        geodf_stereo_sampson_th = (double)fsSettings["geodf_stereo_sampson_th"];
    if (!fsSettings["geodf_stereo_floor_max"].empty())
        geodf_stereo_floor_max = (double)fsSettings["geodf_stereo_floor_max"];
    if (!fsSettings["geodf_motion3d_enable"].empty())
        geodf_motion3d_enable = (int)fsSettings["geodf_motion3d_enable"];
    if (!fsSettings["geodf_motion3d_min_points"].empty())
        geodf_motion3d_min_points = (int)fsSettings["geodf_motion3d_min_points"];
    if (!fsSettings["geodf_motion3d_min_depth"].empty())
        geodf_motion3d_min_depth = (double)fsSettings["geodf_motion3d_min_depth"];
    if (!fsSettings["geodf_motion3d_max_depth"].empty())
        geodf_motion3d_max_depth = (double)fsSettings["geodf_motion3d_max_depth"];
    if (!fsSettings["geodf_motion3d_residual_th"].empty())
        geodf_motion3d_residual_th = (double)fsSettings["geodf_motion3d_residual_th"];
    if (!fsSettings["geodf_motion3d_ransac_iters"].empty())
        geodf_motion3d_ransac_iters = (int)fsSettings["geodf_motion3d_ransac_iters"];
    if (!fsSettings["geodf_motion3d_min_2d_ratio"].empty())
        geodf_motion3d_min_2d_ratio = (double)fsSettings["geodf_motion3d_min_2d_ratio"];
    if (!fsSettings["geodf_motion3d_arm_2d_ratio"].empty())
        geodf_motion3d_arm_2d_ratio = (double)fsSettings["geodf_motion3d_arm_2d_ratio"];

    if (geodf_enable) {
        geodf_stats_path = output_folder + "/geo_df_stats.csv";
        std::ofstream geo_stats(geodf_stats_path, std::ios::out);
        geo_stats << "timestamp_ns,tracks_before,scored,ransac_outliers,sampson_above_th,"
                     "candidates,rejected,reject_ratio,tracks_after,"
                     "mean_sampson,median_sampson,max_sampson,guard_triggered,guard_capped,"
                     "activation_signal,frame_active,geo_ms,rho_on,outlier_floor,stereo_added,confirmed,"
                     "outlier_ratio,candidate_ratio,quality_score,quality_ema,residual_lift,"
                     "median_candidate_sampson,median_background_sampson,reject_limit,"
                     "motion3d_valid,motion3d_outliers,motion3d_median_residual,motion3d_used\n";
        geo_stats.close();
        if (geodf_dump_features) {
            geodf_feat_path = output_folder + "/geo_df_features.csv";
            std::ofstream feat(geodf_feat_path, std::ios::out);
            feat << "timestamp_ns,feature_id,u,v,sampson,ransac_outlier,rejected\n";
            feat.close();
        }
        ROS_INFO_STREAM("GeoDF-VINS-Hard enabled: sampson_th=" << geodf_sampson_th
                        << " ransac_th_px=" << geodf_ransac_th_px
                        << " max_reject_ratio=" << geodf_max_reject_ratio
                        << " ratio_guard=" << geodf_ratio_guard
                        << " max_reject_per_frame=" << geodf_max_reject_per_frame
                        << " hard_reject=" << geodf_hard_reject
                        << " dump_features=" << geodf_dump_features
                        << " adaptive=" << geodf_adaptive
                        << " activate_ratio=" << geodf_activate_ratio
                        << " activate_ema=" << geodf_activate_ema
                        << " deactivate_frac=" << geodf_deactivate_frac
                        << " auto_rho=" << geodf_auto_rho
                        << " auto_mult=" << geodf_auto_mult
                        << " auto_margin=" << geodf_auto_margin
                        << " quality_gate=" << geodf_quality_gate
                        << " quality_ema=" << geodf_quality_ema
                        << " quality_min=" << geodf_quality_min
                        << " min_candidate_ratio=" << geodf_min_candidate_ratio
                        << " min_residual_lift=" << geodf_min_residual_lift
                        << " vote_frames=" << geodf_vote_frames
                        << " warmup_frames=" << geodf_warmup_frames
                        << " stereo_check=" << geodf_stereo_check
                        << " stereo_sampson_th=" << geodf_stereo_sampson_th
                        << " stereo_floor_max=" << geodf_stereo_floor_max
                        << " motion3d_enable=" << geodf_motion3d_enable
                        << " motion3d_min_points=" << geodf_motion3d_min_points
                        << " motion3d_residual_th=" << geodf_motion3d_residual_th
                        << " motion3d_min_2d_ratio=" << geodf_motion3d_min_2d_ratio
                        << " motion3d_arm_2d_ratio=" << geodf_motion3d_arm_2d_ratio);
    }

    fsSettings.release();
    return true;
}
