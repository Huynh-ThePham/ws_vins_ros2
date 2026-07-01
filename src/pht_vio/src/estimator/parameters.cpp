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

static void readOptionalInt(const cv::FileStorage &fs, const std::string &key, int &value)
{
    cv::FileNode node = fs[key];
    if (!node.empty())
        value = static_cast<int>(node);
}

static void readOptionalDouble(const cv::FileStorage &fs, const std::string &key, double &value)
{
    cv::FileNode node = fs[key];
    if (!node.empty())
        value = static_cast<double>(node);
}

static void readOptionalString(const cv::FileStorage &fs, const std::string &key, std::string &value)
{
    cv::FileNode node = fs[key];
    if (!node.empty())
        value = static_cast<std::string>(node);
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

    readOptionalInt(fsSettings, "sem_enable", sem_enable);
    readOptionalString(fsSettings, "sem_mask_topic", sem_mask_topic);
    readOptionalInt(fsSettings, "sem_static_value", sem_static_value);
    readOptionalDouble(fsSettings, "sem_mask_max_age_ms", sem_mask_max_age_ms);
    readOptionalInt(fsSettings, "sem_use_latest_mask", sem_use_latest_mask);
    readOptionalInt(fsSettings, "sem_block_on_mask", sem_block_on_mask);
    readOptionalInt(fsSettings, "sem_mask_gated", sem_mask_gated);

    readOptionalInt(fsSettings, "geodf_enable", geodf_enable);
    readOptionalInt(fsSettings, "geodf_hard_reject", geodf_hard_reject);
    readOptionalDouble(fsSettings, "geodf_ransac_th_px", geodf_ransac_th_px);
    readOptionalDouble(fsSettings, "geodf_sampson_th", geodf_sampson_th);
    readOptionalInt(fsSettings, "geodf_min_track_cnt", geodf_min_track_cnt);
    readOptionalInt(fsSettings, "geodf_min_feature_num", geodf_min_feature_num);
    readOptionalDouble(fsSettings, "geodf_reject_ratio_max", geodf_reject_ratio_max);
    readOptionalInt(fsSettings, "geodf_ratio_guard", geodf_ratio_guard);
    readOptionalInt(fsSettings, "geodf_debug", geodf_debug);
    readOptionalInt(fsSettings, "geodf_dump_features", geodf_dump_features);
    readOptionalInt(fsSettings, "geodf_adaptive", geodf_adaptive);
    readOptionalDouble(fsSettings, "geodf_activate_ratio", geodf_activate_ratio);
    readOptionalDouble(fsSettings, "geodf_activate_ema", geodf_activate_ema);
    readOptionalDouble(fsSettings, "geodf_deactivate_frac", geodf_deactivate_frac);
    readOptionalInt(fsSettings, "geodf_auto_rho", geodf_auto_rho);
    readOptionalDouble(fsSettings, "geodf_auto_mult", geodf_auto_mult);
    readOptionalDouble(fsSettings, "geodf_auto_margin", geodf_auto_margin);
    readOptionalDouble(fsSettings, "geodf_activate_ratio_min", geodf_activate_ratio_min);
    readOptionalDouble(fsSettings, "geodf_activate_ratio_max", geodf_activate_ratio_max);
    readOptionalDouble(fsSettings, "geodf_auto_floor_down", geodf_auto_floor_down);
    readOptionalDouble(fsSettings, "geodf_auto_floor_up", geodf_auto_floor_up);
    readOptionalInt(fsSettings, "geodf_vote_frames", geodf_vote_frames);
    readOptionalInt(fsSettings, "geodf_warmup_frames", geodf_warmup_frames);
    readOptionalDouble(fsSettings, "geodf_temporal_alpha", geodf_temporal_alpha);
    readOptionalDouble(fsSettings, "geodf_dynamic_prob_th", geodf_dynamic_prob_th);
    readOptionalInt(fsSettings, "sgta_policy_enable", sgta_policy_enable);
    readOptionalDouble(fsSettings, "sgta_policy_ema_alpha", sgta_policy_ema_alpha);
    readOptionalDouble(fsSettings, "sgta_policy_decay_alpha", sgta_policy_decay_alpha);
    readOptionalDouble(fsSettings, "sgta_aggressive_sem_on", sgta_aggressive_sem_on);
    readOptionalDouble(fsSettings, "sgta_aggressive_sem_off", sgta_aggressive_sem_off);
    readOptionalInt(fsSettings, "sgta_aggressive_hold_frames", sgta_aggressive_hold_frames);
    readOptionalDouble(fsSettings, "sgta_aggressive_activate_ratio", sgta_aggressive_activate_ratio);
    readOptionalDouble(fsSettings, "sgta_aggressive_dynamic_prob_th", sgta_aggressive_dynamic_prob_th);
    readOptionalInt(fsSettings, "sgta_aggressive_vote_frames", sgta_aggressive_vote_frames);
    readOptionalInt(fsSettings, "sgta_aggressive_warmup_frames", sgta_aggressive_warmup_frames);
    readOptionalInt(fsSettings, "sgta_soft_weight_enable", sgta_soft_weight_enable);
    readOptionalDouble(fsSettings, "sgta_soft_weight_min", sgta_soft_weight_min);
    readOptionalDouble(fsSettings, "sgta_soft_weight_power", sgta_soft_weight_power);
    readOptionalInt(fsSettings, "sgta_imu_gate_enable", sgta_imu_gate_enable);
    readOptionalDouble(fsSettings, "sgta_imu_flow_th_px", sgta_imu_flow_th_px);
    readOptionalDouble(fsSettings, "sgta_imu_dynamic_obs", sgta_imu_dynamic_obs);

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

    if (sem_enable) {
        sem_stats_path = output_folder + "/sem_stats.csv";
        std::ofstream sem_stats(sem_stats_path, std::ios::out);
        sem_stats << "timestamp_ns,tracks_before,rejected,reject_ratio,tracks_after,"
                     "mask_available,dynamic_pixel_ratio,sem_candidates,sem_confirmed,"
                     "mask_trusted,mask_lag_ms\n";
        sem_stats.close();
        ROS_INFO("SAD-VINS semantic mask enabled, topic: %s", sem_mask_topic.c_str());
    }
    if (geodf_enable) {
        geodf_stats_path = output_folder + "/geo_df_stats.csv";
        std::ofstream geodf_stats(geodf_stats_path, std::ios::out);
        geodf_stats << "timestamp_ns,tracks_before,scored,ransac_outliers,"
                       "sampson_above_th,candidates,rejected,reject_ratio,"
                       "tracks_after,mean_sampson,median_sampson,max_sampson,"
                       "guard_triggered,guard_capped,activation_signal,frame_active,geo_ms,"
                       "rho_on,outlier_floor,sem_candidates,sem_confirmed,imu_outliers,"
                       "mask_available,mask_trusted,mask_lag_ms,sgta_policy_signal,"
                       "sgta_aggressive\n";
        geodf_stats.close();

        if (geodf_dump_features) {
            geodf_features_path = output_folder + "/geo_df_features.csv";
            std::ofstream geodf_features(geodf_features_path, std::ios::out);
            geodf_features << "timestamp_ns,feature_id,track_cnt,semantic_dynamic,"
                              "ransac_outlier,sampson,p_dyn,rejected\n";
            geodf_features.close();
        }

        ROS_INFO("GeoDF/SGTA dynamic gating enabled: adaptive=%d sampson_th=%.3f reject_cap=%.2f auto_rho=%d vote=%d warmup=%d policy=%d soft_weight=%d imu_gate=%d",
                 geodf_adaptive, geodf_sampson_th, geodf_reject_ratio_max,
                 geodf_auto_rho, geodf_vote_frames, geodf_warmup_frames,
                 sgta_policy_enable, sgta_soft_weight_enable, sgta_imu_gate_enable);
    }

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

    fsSettings.release();
    return true;
}
