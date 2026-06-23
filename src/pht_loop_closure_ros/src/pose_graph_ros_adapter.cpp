#include "pose_graph_ros_adapter.h"
#include "loop_closure_config.hpp"
#include <cv_bridge/cv_bridge.h>
#include <sensor_msgs/image_encodings.hpp>

PoseGraphRosAdapter::PoseGraphRosAdapter(rclcpp::Node::SharedPtr node)
    : node_(std::move(node))
{
    pub_pg_path_ = node_->create_publisher<nav_msgs::msg::Path>("~/pose_graph_path", 1000);
    pub_base_path_ = node_->create_publisher<nav_msgs::msg::Path>("~/base_path", 1000);
    pub_pose_graph_ = node_->create_publisher<visualization_msgs::msg::MarkerArray>("~/pose_graph", 1000);
    for (int i = 1; i < 10; ++i)
        pub_path_[i] = node_->create_publisher<nav_msgs::msg::Path>("~/path_" + std::to_string(i), 1000);

    pub_match_img_ = node_->create_publisher<sensor_msgs::msg::Image>("~/match_image", 1000);
    loopClosureConfig().match_image_cb = [this](const cv::Mat &img, double t) {
        sensor_msgs::msg::Image::SharedPtr msg =
            cv_bridge::CvImage(std_msgs::msg::Header(), "bgr8", img).toImageMsg();
        msg->header.stamp = rclcpp::Time(static_cast<int64_t>(t * 1e9));
        pub_match_img_->publish(*msg);
    };

    posegraph_visualization_ = std::make_unique<CameraPoseVisualization>(1.0, 0.0, 1.0, 1.0);
    posegraph_visualization_->setScale(0.1);
    posegraph_visualization_->setLineWidth(0.01);
}

void PoseGraphRosAdapter::attach(PoseGraph &posegraph)
{
    posegraph.setPublishCallback([this](const pht::PoseGraphSnapshot &snap) {
        onPublish(snap);
    });
}

nav_msgs::msg::Path PoseGraphRosAdapter::toRosPath(const pht::PathTrajectory &path) const
{
    nav_msgs::msg::Path ros_path;
    ros_path.header.frame_id = path.frame_id;
    for (const auto &ps : path.poses) {
        geometry_msgs::msg::PoseStamped pose_stamped;
        pose_stamped.header.stamp = rclcpp::Time(static_cast<int64_t>(ps.time_sec * 1e9));
        pose_stamped.header.frame_id = path.frame_id;
        pose_stamped.pose.position.x = ps.position.x();
        pose_stamped.pose.position.y = ps.position.y();
        pose_stamped.pose.position.z = ps.position.z();
        pose_stamped.pose.orientation.x = ps.orientation.x();
        pose_stamped.pose.orientation.y = ps.orientation.y();
        pose_stamped.pose.orientation.z = ps.orientation.z();
        pose_stamped.pose.orientation.w = ps.orientation.w();
        ros_path.poses.push_back(pose_stamped);
    }
    if (!ros_path.poses.empty())
        ros_path.header.stamp = ros_path.poses.back().header.stamp;
    return ros_path;
}

void PoseGraphRosAdapter::onPublish(const pht::PoseGraphSnapshot &snap)
{
    posegraph_visualization_->reset();
    for (const auto &edge : snap.debug.edges) {
        if (edge.type == pht::PoseGraphDebugGraph::EdgeType::LOOP)
            posegraph_visualization_->add_loopedge(edge.p0, edge.p1);
        else
            posegraph_visualization_->add_edge(edge.p0, edge.p1);
    }

    for (int i = 1; i <= snap.sequence_cnt; ++i) {
        if (1 || i == snap.base_sequence) {
            pub_pg_path_->publish(toRosPath(snap.paths[i]));
            pub_path_[i]->publish(toRosPath(snap.paths[i]));
        }
    }
    pub_base_path_->publish(toRosPath(snap.base_path));

    std_msgs::msg::Header header;
    header.frame_id = "world";
    if (!snap.paths[snap.sequence_cnt].poses.empty())
        header.stamp = rclcpp::Time(static_cast<int64_t>(
            snap.paths[snap.sequence_cnt].poses.back().time_sec * 1e9));
    posegraph_visualization_->publish_by(pub_pose_graph_, header);
}
