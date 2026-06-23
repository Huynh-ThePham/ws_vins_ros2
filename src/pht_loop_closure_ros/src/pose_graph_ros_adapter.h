#pragma once

#include <functional>
#include <memory>
#include <rclcpp/rclcpp.hpp>
#include <nav_msgs/msg/path.hpp>
#include <visualization_msgs/msg/marker_array.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <std_msgs/msg/header.hpp>
#include <pht_slam_ros_common/CameraPoseVisualization.h>
#include "pose_graph.h"

class PoseGraphRosAdapter
{
public:
    explicit PoseGraphRosAdapter(rclcpp::Node::SharedPtr node);

    void attach(PoseGraph &posegraph);

private:
    nav_msgs::msg::Path toRosPath(const pht::PathTrajectory &path) const;
    void onPublish(const pht::PoseGraphSnapshot &snap);

    rclcpp::Node::SharedPtr node_;
    rclcpp::Publisher<nav_msgs::msg::Path>::SharedPtr pub_pg_path_;
    rclcpp::Publisher<nav_msgs::msg::Path>::SharedPtr pub_base_path_;
    rclcpp::Publisher<visualization_msgs::msg::MarkerArray>::SharedPtr pub_pose_graph_;
    rclcpp::Publisher<nav_msgs::msg::Path>::SharedPtr pub_path_[10];
    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr pub_match_img_;
    std::unique_ptr<CameraPoseVisualization> posegraph_visualization_;
};
