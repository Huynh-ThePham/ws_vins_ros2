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

#include <vector>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp/executors/single_threaded_executor.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <nav_msgs/msg/path.hpp>
#include <sensor_msgs/msg/point_cloud.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <sensor_msgs/image_encodings.hpp>
#include <visualization_msgs/msg/marker.hpp>
#include <std_msgs/msg/bool.hpp>
#include <cv_bridge/cv_bridge.h>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>
#include <atomic>
#include <poll.h>
#include <chrono>
#include <csignal>
#include <memory>
#include <eigen3/Eigen/Dense>
#include <opencv2/opencv.hpp>
#include <opencv2/core/eigen.hpp>
#include "keyframe.h"
#include <pht_slam_common/tic_toc.hpp>
#include "pose_graph.h"
#include <pht_slam_ros_common/CameraPoseVisualization.h>
#include "loop_closure_config.hpp"
#include "pose_graph_ros_adapter.h"
#include <ament_index_cpp/get_package_share_directory.hpp>
#define SKIP_FIRST_CNT 10
using namespace std;

queue<sensor_msgs::msg::Image::SharedPtr> image_buf;
queue<sensor_msgs::msg::PointCloud::SharedPtr> point_buf;
queue<nav_msgs::msg::Odometry::SharedPtr> pose_buf;
queue<Eigen::Vector3d> odometry_buf;
std::mutex m_buf;
std::mutex m_process;
int frame_index  = 0;
int sequence = 1;
PoseGraph posegraph;
int skip_first_cnt = 0;
int SKIP_CNT;
int skip_cnt = 0;
bool load_flag = 0;
bool start_flag = 0;
double SKIP_DIS = 0;

CameraPoseVisualization cameraposevisual(1, 0, 0, 1);
Eigen::Vector3d last_t(-100, -100, -100);
double last_image_time = -1;

rclcpp::Publisher<visualization_msgs::msg::MarkerArray>::SharedPtr pub_camera_pose_visual;
rclcpp::Publisher<nav_msgs::msg::Odometry>::SharedPtr pub_odometry_rect;
rclcpp::Publisher<sensor_msgs::msg::PointCloud>::SharedPtr pub_point_cloud, pub_margin_cloud;

// Global node pointer for shutdown
rclcpp::Node::SharedPtr g_node;
std::atomic<bool> g_shutdown{false};

void onShutdownSignal(int)
{
    g_shutdown = true;
}

void new_sequence()
{
    printf("new sequence\n");
    sequence++;
    printf("sequence cnt %d \n", sequence);
    if (sequence > 5)
    {
        RCLCPP_WARN(rclcpp::get_logger("pht_loop_closure_ros"), "only support 5 sequences since it's boring to copy code for more sequences.");
        assert(false);
    }
    posegraph.debug_graph.reset();
    posegraph.publish();
    m_buf.lock();
    while(!image_buf.empty())
        image_buf.pop();
    while(!point_buf.empty())
        point_buf.pop();
    while(!pose_buf.empty())
        pose_buf.pop();
    while(!odometry_buf.empty())
        odometry_buf.pop();
    m_buf.unlock();
}

void image_callback(const sensor_msgs::msg::Image::SharedPtr image_msg)
{
    //RCLCPP_INFO(rclcpp::get_logger("pht_loop_closure_ros"), "image_callback!");
    m_buf.lock();
    image_buf.push(image_msg);
    m_buf.unlock();
    //printf(" image time %f \n", rclcpp::Time(image_msg->header.stamp).seconds());

    // detect unstable camera stream
    if (last_image_time == -1)
        last_image_time = rclcpp::Time(image_msg->header.stamp).seconds();
    else if (rclcpp::Time(image_msg->header.stamp).seconds() - last_image_time > 1.0 || rclcpp::Time(image_msg->header.stamp).seconds() < last_image_time)
    {
        RCLCPP_WARN(rclcpp::get_logger("pht_loop_closure_ros"), "image discontinue! detect a new sequence!");
        new_sequence();
    }
    last_image_time = rclcpp::Time(image_msg->header.stamp).seconds();
}

void point_callback(const sensor_msgs::msg::PointCloud::SharedPtr point_msg)
{
    //RCLCPP_INFO(rclcpp::get_logger("pht_loop_closure_ros"), "point_callback!");
    m_buf.lock();
    point_buf.push(point_msg);
    m_buf.unlock();
    /*
    for (unsigned int i = 0; i < point_msg->points.size(); i++)
    {
        printf("%d, 3D point: %f, %f, %f 2D point %f, %f \n",i , point_msg->points[i].x, 
                                                     point_msg->points[i].y,
                                                     point_msg->points[i].z,
                                                     point_msg->channels[i].values[0],
                                                     point_msg->channels[i].values[1]);
    }
    */
    // for visualization
    sensor_msgs::msg::PointCloud point_cloud;
    point_cloud.header = point_msg->header;
    for (unsigned int i = 0; i < point_msg->points.size(); i++)
    {
        cv::Point3f p_3d;
        p_3d.x = point_msg->points[i].x;
        p_3d.y = point_msg->points[i].y;
        p_3d.z = point_msg->points[i].z;
        Eigen::Vector3d tmp = posegraph.r_drift * Eigen::Vector3d(p_3d.x, p_3d.y, p_3d.z) + posegraph.t_drift;
        geometry_msgs::msg::Point32 p;
        p.x = tmp(0);
        p.y = tmp(1);
        p.z = tmp(2);
        point_cloud.points.push_back(p);
    }
    pub_point_cloud->publish(point_cloud);
}

// only for visualization
void margin_point_callback(const sensor_msgs::msg::PointCloud::SharedPtr point_msg)
{
    sensor_msgs::msg::PointCloud point_cloud;
    point_cloud.header = point_msg->header;
    for (unsigned int i = 0; i < point_msg->points.size(); i++)
    {
        cv::Point3f p_3d;
        p_3d.x = point_msg->points[i].x;
        p_3d.y = point_msg->points[i].y;
        p_3d.z = point_msg->points[i].z;
        Eigen::Vector3d tmp = posegraph.r_drift * Eigen::Vector3d(p_3d.x, p_3d.y, p_3d.z) + posegraph.t_drift;
        geometry_msgs::msg::Point32 p;
        p.x = tmp(0);
        p.y = tmp(1);
        p.z = tmp(2);
        point_cloud.points.push_back(p);
    }
    pub_margin_cloud->publish(point_cloud);
}

void pose_callback(const nav_msgs::msg::Odometry::SharedPtr pose_msg)
{
    //RCLCPP_INFO(rclcpp::get_logger("pht_loop_closure_ros"), "pose_callback!");
    m_buf.lock();
    pose_buf.push(pose_msg);
    m_buf.unlock();
}

void vio_callback(const nav_msgs::msg::Odometry::SharedPtr pose_msg)
{
    //RCLCPP_INFO(rclcpp::get_logger("pht_loop_closure_ros"), "vio_callback!");
    Vector3d vio_t(pose_msg->pose.pose.position.x, pose_msg->pose.pose.position.y, pose_msg->pose.pose.position.z);
    Quaterniond vio_q;
    vio_q.w() = pose_msg->pose.pose.orientation.w;
    vio_q.x() = pose_msg->pose.pose.orientation.x;
    vio_q.y() = pose_msg->pose.pose.orientation.y;
    vio_q.z() = pose_msg->pose.pose.orientation.z;

    vio_t = posegraph.w_r_vio * vio_t + posegraph.w_t_vio;
    vio_q = posegraph.w_r_vio *  vio_q;

    vio_t = posegraph.r_drift * vio_t + posegraph.t_drift;
    vio_q = posegraph.r_drift * vio_q;

    nav_msgs::msg::Odometry odometry;
    odometry.header = pose_msg->header;
    odometry.header.frame_id = "world";
    odometry.pose.pose.position.x = vio_t.x();
    odometry.pose.pose.position.y = vio_t.y();
    odometry.pose.pose.position.z = vio_t.z();
    odometry.pose.pose.orientation.x = vio_q.x();
    odometry.pose.pose.orientation.y = vio_q.y();
    odometry.pose.pose.orientation.z = vio_q.z();
    odometry.pose.pose.orientation.w = vio_q.w();
    pub_odometry_rect->publish(odometry);

    Vector3d vio_t_cam;
    Quaterniond vio_q_cam;
    vio_t_cam = vio_t + vio_q * loopClosureConfig().tic;
    vio_q_cam = vio_q * loopClosureConfig().qic;        

    cameraposevisual.reset();
    cameraposevisual.add_pose(vio_t_cam, vio_q_cam);
    cameraposevisual.publish_by(pub_camera_pose_visual, pose_msg->header);


}

void extrinsic_callback(const nav_msgs::msg::Odometry::SharedPtr pose_msg)
{
    m_process.lock();
    loopClosureConfig().tic = Vector3d(pose_msg->pose.pose.position.x,
                                       pose_msg->pose.pose.position.y,
                                       pose_msg->pose.pose.position.z);
    loopClosureConfig().qic = Quaterniond(pose_msg->pose.pose.orientation.w,
                                          pose_msg->pose.pose.orientation.x,
                                          pose_msg->pose.pose.orientation.y,
                                          pose_msg->pose.pose.orientation.z).toRotationMatrix();
    m_process.unlock();
}

void process()
{
    while (!g_shutdown.load())
    {
        sensor_msgs::msg::Image::SharedPtr image_msg = nullptr;
        sensor_msgs::msg::PointCloud::SharedPtr point_msg = nullptr;
        nav_msgs::msg::Odometry::SharedPtr pose_msg = nullptr;

        // find out the messages with same time stamp
        m_buf.lock();
        if(!image_buf.empty() && !point_buf.empty() && !pose_buf.empty())
        {
            if (rclcpp::Time(image_buf.front()->header.stamp).seconds() > rclcpp::Time(pose_buf.front()->header.stamp).seconds())
            {
                pose_buf.pop();
                printf("throw pose at beginning\n");
            }
            else if (rclcpp::Time(image_buf.front()->header.stamp).seconds() > rclcpp::Time(point_buf.front()->header.stamp).seconds())
            {
                point_buf.pop();
                printf("throw point at beginning\n");
            }
            else if (rclcpp::Time(image_buf.back()->header.stamp).seconds() >= rclcpp::Time(pose_buf.front()->header.stamp).seconds() 
                && rclcpp::Time(point_buf.back()->header.stamp).seconds() >= rclcpp::Time(pose_buf.front()->header.stamp).seconds())
            {
                pose_msg = pose_buf.front();
                pose_buf.pop();
                while (!pose_buf.empty())
                    pose_buf.pop();
                while (rclcpp::Time(image_buf.front()->header.stamp).seconds() < rclcpp::Time(pose_msg->header.stamp).seconds())
                    image_buf.pop();
                image_msg = image_buf.front();
                image_buf.pop();

                while (rclcpp::Time(point_buf.front()->header.stamp).seconds() < rclcpp::Time(pose_msg->header.stamp).seconds())
                    point_buf.pop();
                point_msg = point_buf.front();
                point_buf.pop();
            }
        }
        m_buf.unlock();

        if (pose_msg != nullptr)
        {
            //printf(" pose time %f \n", rclcpp::Time(pose_msg->header.stamp).seconds());
            //printf(" point time %f \n", rclcpp::Time(point_msg->header.stamp).seconds());
            //printf(" image time %f \n", rclcpp::Time(image_msg->header.stamp).seconds());
            // skip fisrt few
            if (skip_first_cnt < SKIP_FIRST_CNT)
            {
                skip_first_cnt++;
                continue;
            }

            if (skip_cnt < SKIP_CNT)
            {
                skip_cnt++;
                continue;
            }
            else
            {
                skip_cnt = 0;
            }

            cv_bridge::CvImageConstPtr ptr;
            if (image_msg->encoding == "8UC1")
            {
                sensor_msgs::msg::Image img;
                img.header = image_msg->header;
                img.height = image_msg->height;
                img.width = image_msg->width;
                img.is_bigendian = image_msg->is_bigendian;
                img.step = image_msg->step;
                img.data = image_msg->data;
                img.encoding = "mono8";
                ptr = cv_bridge::toCvCopy(img, sensor_msgs::image_encodings::MONO8);
            }
            else
                ptr = cv_bridge::toCvCopy(*image_msg, sensor_msgs::image_encodings::MONO8);
            
            cv::Mat image = ptr->image;
            // build keyframe
            Vector3d T = Vector3d(pose_msg->pose.pose.position.x,
                                  pose_msg->pose.pose.position.y,
                                  pose_msg->pose.pose.position.z);
            Matrix3d R = Quaterniond(pose_msg->pose.pose.orientation.w,
                                     pose_msg->pose.pose.orientation.x,
                                     pose_msg->pose.pose.orientation.y,
                                     pose_msg->pose.pose.orientation.z).toRotationMatrix();
            if((T - last_t).norm() > SKIP_DIS)
            {
                vector<cv::Point3f> point_3d; 
                vector<cv::Point2f> point_2d_uv; 
                vector<cv::Point2f> point_2d_normal;
                vector<double> point_id;

                for (unsigned int i = 0; i < point_msg->points.size(); i++)
                {
                    cv::Point3f p_3d;
                    p_3d.x = point_msg->points[i].x;
                    p_3d.y = point_msg->points[i].y;
                    p_3d.z = point_msg->points[i].z;
                    point_3d.push_back(p_3d);

                    cv::Point2f p_2d_uv, p_2d_normal;
                    double p_id;
                    p_2d_normal.x = point_msg->channels[i].values[0];
                    p_2d_normal.y = point_msg->channels[i].values[1];
                    p_2d_uv.x = point_msg->channels[i].values[2];
                    p_2d_uv.y = point_msg->channels[i].values[3];
                    p_id = point_msg->channels[i].values[4];
                    point_2d_normal.push_back(p_2d_normal);
                    point_2d_uv.push_back(p_2d_uv);
                    point_id.push_back(p_id);

                    //printf("u %f, v %f \n", p_2d_uv.x, p_2d_uv.y);
                }

                KeyFrame* keyframe = new KeyFrame(rclcpp::Time(pose_msg->header.stamp).seconds(), frame_index, T, R, image,
                                   point_3d, point_2d_uv, point_2d_normal, point_id, sequence);   
                m_process.lock();
                start_flag = 1;
                posegraph.addKeyFrame(keyframe, 1);
                m_process.unlock();
                frame_index++;
                last_t = T;
            }
        }
        std::chrono::milliseconds dura(5);
        std::this_thread::sleep_for(dura);
    }
}

void command()
{
    while (!g_shutdown.load())
    {
        struct pollfd pfd;
        pfd.fd = STDIN_FILENO;
        pfd.events = POLLIN;
        int ready = poll(&pfd, 1, 50);
        if (ready <= 0 || !(pfd.revents & POLLIN))
            continue;

        char c = getchar();
        if (c == 's')
        {
            m_process.lock();
            posegraph.savePoseGraph();
            m_process.unlock();
            printf("save pose graph finish\nyou can set 'load_previous_pose_graph' to 1 in the config file to reuse it next time\n");
            printf("program shutting down...\n");
            g_shutdown = true;
        }
        if (c == 'n')
            new_sequence();
    }
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("please intput: ros2 run pht_loop_closure_ros pht_loop_closure_node [config file] \n"
               "for example: ros2 run pht_loop_closure_ros pht_loop_closure_node "
               "$(ros2 pkg prefix pht_vio_ros)/share/pht_vio_ros/config/euroc/euroc_stereo_imu_config.yaml \n");
        return 1;
    }

    string config_file = argv[1];

    rclcpp::InitOptions init_options;
    init_options.shutdown_on_signal = false;
    rclcpp::init(argc, argv, init_options);
    auto n = rclcpp::Node::make_shared("pht_loop_closure_ros");
    g_node = n;
    auto ros_adapter = std::make_unique<PoseGraphRosAdapter>(n);
    ros_adapter->attach(posegraph);

    loopClosureConfig().visualization_shift_x = 0;
    loopClosureConfig().visualization_shift_y = 0;
    SKIP_CNT = 0;
    SKIP_DIS = 0;

    printf("config_file: %s\n", config_file.c_str());

    cv::FileStorage fsSettings(config_file, cv::FileStorage::READ);
    if(!fsSettings.isOpened())
    {
        std::cerr << "ERROR: Wrong path to settings" << std::endl;
    }

    cameraposevisual.setScale(0.1);
    cameraposevisual.setLineWidth(0.01);

    std::string IMAGE_TOPIC;
    int LOAD_PREVIOUS_POSE_GRAPH;

    loopClosureConfig().row = fsSettings["image_height"];
    loopClosureConfig().col = fsSettings["image_width"];
    std::string pkg_path;
    try {
        pkg_path = ament_index_cpp::get_package_share_directory("pht_loop_closure_ros");
    } catch (...) {
        // Fallback: use config_file path to find support_files
        int pn2 = config_file.find_last_of('/');
        pkg_path = config_file.substr(0, pn2) + "/..";
    }
    string vocabulary_file = pkg_path + "/support_files/brief_k10L6.bin";
    cout << "vocabulary_file" << vocabulary_file << endl;
    posegraph.loadVocabulary(vocabulary_file);

    loopClosureConfig().brief_pattern_file = pkg_path + "/support_files/brief_pattern.yml";
    cout << "BRIEF_PATTERN_FILE" << loopClosureConfig().brief_pattern_file << endl;

    int pn = config_file.find_last_of('/');
    std::string configPath = config_file.substr(0, pn);
    std::string cam0Calib;
    fsSettings["cam0_calib"] >> cam0Calib;
    std::string cam0Path = configPath + "/" + cam0Calib;
    printf("cam calib path: %s\n", cam0Path.c_str());
    loopClosureConfig().camera = camodocal::CameraFactory::instance()->generateCameraFromYamlFile(cam0Path.c_str());

    fsSettings["image0_topic"] >> IMAGE_TOPIC;
    fsSettings["pose_graph_save_path"] >> loopClosureConfig().pose_graph_save_path;
    std::string output_path;
    fsSettings["output_path"] >> output_path;
    fsSettings["save_image"] >> loopClosureConfig().debug_image;

    LOAD_PREVIOUS_POSE_GRAPH = fsSettings["load_previous_pose_graph"];
    loopClosureConfig().vins_result_path = output_path + "/vio_loop.csv";
    std::ofstream fout(loopClosureConfig().vins_result_path, std::ios::out);
    fout.close();

    int USE_IMU = fsSettings["imu"];
    posegraph.setIMUFlag(USE_IMU);
    fsSettings.release();

    if (LOAD_PREVIOUS_POSE_GRAPH)
    {
        printf("load pose graph\n");
        m_process.lock();
        posegraph.loadPoseGraph();
        m_process.unlock();
        printf("load pose graph finish\n");
        load_flag = 1;
    }
    else
    {
        printf("no previous pose graph\n");
        load_flag = 1;
    }

    auto sub_vio = n->create_subscription<nav_msgs::msg::Odometry>(
        "/pht_vio_estimator/odometry", 2000, vio_callback);
    auto sub_image = n->create_subscription<sensor_msgs::msg::Image>(
        IMAGE_TOPIC, 2000, image_callback);
    auto sub_pose = n->create_subscription<nav_msgs::msg::Odometry>(
        "/pht_vio_estimator/keyframe_pose", 2000, pose_callback);
    auto sub_extrinsic = n->create_subscription<nav_msgs::msg::Odometry>(
        "/pht_vio_estimator/extrinsic", 2000, extrinsic_callback);
    auto sub_point = n->create_subscription<sensor_msgs::msg::PointCloud>(
        "/pht_vio_estimator/keyframe_point", 2000, point_callback);
    auto sub_margin_point = n->create_subscription<sensor_msgs::msg::PointCloud>(
        "/pht_vio_estimator/margin_cloud", 2000, margin_point_callback);

    pub_camera_pose_visual = n->create_publisher<visualization_msgs::msg::MarkerArray>("~/camera_pose_visual", 1000);
    pub_point_cloud = n->create_publisher<sensor_msgs::msg::PointCloud>("~/point_cloud_loop_rect", 1000);
    pub_margin_cloud = n->create_publisher<sensor_msgs::msg::PointCloud>("~/margin_cloud_loop_rect", 1000);
    pub_odometry_rect = n->create_publisher<nav_msgs::msg::Odometry>("~/odometry_rect", 1000);

    std::thread measurement_process;
    std::thread keyboard_command_process;

    measurement_process = std::thread(process);
    keyboard_command_process = std::thread(command);

    std::signal(SIGINT, onShutdownSignal);
    std::signal(SIGTERM, onShutdownSignal);

    rclcpp::executors::SingleThreadedExecutor exec;
    exec.add_node(n);
    while (!g_shutdown.load())
        exec.spin_once(std::chrono::milliseconds(10));

    g_shutdown = true;
    if (measurement_process.joinable())
        measurement_process.join();
    if (keyboard_command_process.joinable())
        keyboard_command_process.join();

    sub_vio.reset();
    sub_image.reset();
    sub_pose.reset();
    sub_extrinsic.reset();
    sub_point.reset();
    sub_margin_point.reset();

    pub_odometry_rect.reset();
    pub_point_cloud.reset();
    pub_margin_cloud.reset();
    pub_camera_pose_visual.reset();

    posegraph.setPublishCallback(nullptr);
    posegraph.shutdown();

    ros_adapter.reset();
    exec.remove_node(n);
    g_node.reset();
    n.reset();

    rclcpp::shutdown();
    return 0;
}
