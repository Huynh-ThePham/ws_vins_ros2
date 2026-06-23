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

    cv::FileNode sem_enable_node = fsSettings["sem_enable"];
    if (!sem_enable_node.empty())
        sem_enable = static_cast<int>(sem_enable_node);
    cv::FileNode sem_mask_topic_node = fsSettings["sem_mask_topic"];
    if (!sem_mask_topic_node.empty())
        sem_mask_topic = static_cast<std::string>(sem_mask_topic_node);
    cv::FileNode sem_static_value_node = fsSettings["sem_static_value"];
    if (!sem_static_value_node.empty())
        sem_static_value = static_cast<int>(sem_static_value_node);

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
                     "mask_available,dynamic_pixel_ratio\n";
        sem_stats.close();
        ROS_INFO("SAD-VINS semantic mask enabled, topic: %s", sem_mask_topic.c_str());
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
