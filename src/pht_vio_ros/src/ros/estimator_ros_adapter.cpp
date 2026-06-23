#include "ros/estimator_ros_adapter.h"
#include "ros/visualization.h"
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/header.hpp>

void registerEstimatorRosOutputs(Estimator &estimator)
{
    estimator.setTrackImageCallback([](const cv::Mat &img, double t) {
        pubTrackImage(img, t);
    });

    estimator.setPropagatedStateCallback([](double t, const Eigen::Vector3d &P,
            const Eigen::Quaterniond &Q, const Eigen::Vector3d &V) {
        pubLatestOdometry(P, Q, V, t);
    });

    estimator.setFrameOutputCallback([](const Estimator &e, double timestamp) {
        std_msgs::msg::Header header;
        header.frame_id = "world";
        header.stamp = rclcpp::Time(static_cast<int64_t>(timestamp * 1e9));
        pubOdometry(e, header);
        pubKeyPoses(e, header);
        pubCameraPose(e, header);
        pubPointCloud(e, header);
        pubKeyframe(e);
        pubTF(e, header);
    });

    estimator.setStatisticsCallback([](const Estimator &e, double solver_time_ms) {
        printStatistics(e, solver_time_ms);
    });
}
