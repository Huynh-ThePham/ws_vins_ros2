/*******************************************************
 * Loop-closure runtime configuration (algorithm layer, no ROS).
 *******************************************************/

#pragma once

#include "camodocal/camera_models/CameraFactory.h"
#include "camodocal/camera_models/CataCamera.h"
#include "camodocal/camera_models/PinholeCamera.h"
#include <eigen3/Eigen/Dense>
#include <functional>
#include <opencv2/opencv.hpp>
#include <string>
#include "loop_types.hpp"

struct LoopClosureConfig
{
    camodocal::CameraPtr camera;
    Eigen::Vector3d tic = Eigen::Vector3d::Zero();
    Eigen::Matrix3d qic = Eigen::Matrix3d::Identity();

    int visualization_shift_x = 0;
    int visualization_shift_y = 0;
    std::string brief_pattern_file;
    std::string pose_graph_save_path;
    int row = 0;
    int col = 0;
    std::string vins_result_path;
    int debug_image = 0;

    pht::MatchImageCallback match_image_cb;
};

LoopClosureConfig &loopClosureConfig();
