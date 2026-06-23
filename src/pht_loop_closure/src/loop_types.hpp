#pragma once

#include <opencv2/opencv.hpp>
#include <Eigen/Dense>
#include <Eigen/Geometry>
#include <functional>
#include <string>
#include <vector>

namespace pht
{

struct PoseStamped
{
    double time_sec = 0.0;
    Eigen::Vector3d position = Eigen::Vector3d::Zero();
    Eigen::Quaterniond orientation = Eigen::Quaterniond::Identity();
};

struct PathTrajectory
{
    std::string frame_id = "world";
    std::vector<PoseStamped> poses;
};

struct PoseGraphDebugGraph
{
    enum class EdgeType { SEQUENCE, LOOP };

    struct Edge
    {
        Eigen::Vector3d p0;
        Eigen::Vector3d p1;
        EdgeType type = EdgeType::SEQUENCE;
    };

    std::vector<Edge> edges;

    void reset() { edges.clear(); }

    void add_edge(const Eigen::Vector3d &p0, const Eigen::Vector3d &p1)
    {
        edges.push_back({p0, p1, EdgeType::SEQUENCE});
    }

    void add_loopedge(const Eigen::Vector3d &p0, const Eigen::Vector3d &p1)
    {
        edges.push_back({p0, p1, EdgeType::LOOP});
    }
};

struct PoseGraphSnapshot
{
    PathTrajectory base_path;
    PathTrajectory paths[10];
    int sequence_cnt = 0;
    int base_sequence = 0;
    PoseGraphDebugGraph debug;
};

using MatchImageCallback = std::function<void(const cv::Mat &, double)>;
using PoseGraphPublishCallback = std::function<void(const PoseGraphSnapshot &)>;

}  // namespace pht
