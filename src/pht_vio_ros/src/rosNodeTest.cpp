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

#include <stdio.h>
#include <queue>
#include <map>
#include <thread>
#include <mutex>
#include <cmath>
#include <rclcpp/rclcpp.hpp>
#include <cv_bridge/cv_bridge.h>
#include <opencv2/opencv.hpp>
#include "estimator/estimator.h"
#include "estimator/parameters.h"
#include "ros/estimator_ros_adapter.h"
#include "ros/visualization.h"

Estimator estimator;

queue<sensor_msgs::msg::Imu::SharedPtr> imu_buf;
queue<sensor_msgs::msg::PointCloud::SharedPtr> feature_buf;
queue<sensor_msgs::msg::Image::SharedPtr> img0_buf;
queue<sensor_msgs::msg::Image::SharedPtr> img1_buf;
queue<sensor_msgs::msg::Image::SharedPtr> sem_mask_buf;
std::mutex m_buf;


void img0_callback(const sensor_msgs::msg::Image::SharedPtr img_msg)
{
    m_buf.lock();
    img0_buf.push(img_msg);
    m_buf.unlock();
}

void img1_callback(const sensor_msgs::msg::Image::SharedPtr img_msg)
{
    m_buf.lock();
    img1_buf.push(img_msg);
    m_buf.unlock();
}

void sem_mask_callback(const sensor_msgs::msg::Image::SharedPtr mask_msg)
{
    m_buf.lock();
    sem_mask_buf.push(mask_msg);
    m_buf.unlock();
}


cv::Mat getImageFromMsg(const sensor_msgs::msg::Image::SharedPtr &img_msg)
{
    cv_bridge::CvImageConstPtr ptr;
    if (img_msg->encoding == "8UC1")
    {
        sensor_msgs::msg::Image img;
        img.header = img_msg->header;
        img.height = img_msg->height;
        img.width = img_msg->width;
        img.is_bigendian = img_msg->is_bigendian;
        img.step = img_msg->step;
        img.data = img_msg->data;
        img.encoding = "mono8";
        ptr = cv_bridge::toCvCopy(img, sensor_msgs::image_encodings::MONO8);
    }
    else
        ptr = cv_bridge::toCvCopy(*img_msg, sensor_msgs::image_encodings::MONO8);

    cv::Mat img = ptr->image.clone();
    return img;
}

cv::Mat getMaskFromMsg(const sensor_msgs::msg::Image::SharedPtr &mask_msg)
{
    cv_bridge::CvImageConstPtr ptr = cv_bridge::toCvCopy(*mask_msg, sensor_msgs::image_encodings::MONO8);
    return ptr->image.clone();
}

// Attach semantic mask to cam0 timestamp. Returns true if a mask was attached.
// mask_lag_ms = (cam_time - mask_time) in ms; used for staleness gating in fusion.
bool tryAttachSemMask(double time0, cv::Mat &sem_mask, double &mask_lag_ms)
{
    auto &cfg = vinsConfig();
    if (!cfg.sem_enable)
        return false;

    const double sync_tol = 0.003;
    const double max_age_s = cfg.sem_mask_max_age_ms / 1000.0;
    mask_lag_ms = -1.0;

    while (!sem_mask_buf.empty())
    {
        const double timeM = rclcpp::Time(sem_mask_buf.front()->header.stamp).seconds();
        if (time0 - timeM > max_age_s)
            sem_mask_buf.pop();
        else
            break;
    }
    if (sem_mask_buf.empty())
        return false;

    const double timeM = rclcpp::Time(sem_mask_buf.front()->header.stamp).seconds();
    if (std::fabs(time0 - timeM) <= sync_tol)
    {
        sem_mask = getMaskFromMsg(sem_mask_buf.front());
        sem_mask_buf.pop();
        mask_lag_ms = (time0 - timeM) * 1000.0;
        return true;
    }

    if (!cfg.sem_use_latest_mask)
        return false;

    // Best-effort: YOLO lagging behind camera — use freshest mask within max age.
    if (time0 >= timeM && (time0 - timeM) <= max_age_s)
    {
        sem_mask = getMaskFromMsg(sem_mask_buf.front());
        sem_mask_buf.pop();
        mask_lag_ms = (time0 - timeM) * 1000.0;
        return true;
    }
    return false;
}

// extract images with same timestamp from two topics
void sync_process()
{
    while(1)
    {
        if(vinsConfig().stereo)
        {
            cv::Mat image0, image1, sem_mask;
            double time = 0;
            double mask_lag_ms = -1.0;
            m_buf.lock();
            if (!img0_buf.empty() && !img1_buf.empty())
            {
                double time0 = rclcpp::Time(img0_buf.front()->header.stamp).seconds();
                double time1 = rclcpp::Time(img1_buf.front()->header.stamp).seconds();
                // 0.003s sync tolerance
                if(time0 < time1 - 0.003)
                {
                    img0_buf.pop();
                    printf("throw img0\n");
                }
                else if(time0 > time1 + 0.003)
                {
                    img1_buf.pop();
                    printf("throw img1\n");
                }
                else
                {
                    time = time0;
                    image0 = getImageFromMsg(img0_buf.front());
                    img0_buf.pop();
                    image1 = getImageFromMsg(img1_buf.front());
                    img1_buf.pop();

                    if (vinsConfig().sem_enable)
                    {
                        if (vinsConfig().sem_block_on_mask)
                        {
                            bool mask_ready = false;
                            if (!sem_mask_buf.empty())
                            {
                                const double timeM =
                                    rclcpp::Time(sem_mask_buf.front()->header.stamp).seconds();
                                if (timeM < time0 - 0.003)
                                    sem_mask_buf.pop();
                                else if (timeM <= time0 + 0.003)
                                    mask_ready = true;
                            }
                            if (mask_ready && !sem_mask_buf.empty())
                            {
                                sem_mask = getMaskFromMsg(sem_mask_buf.front());
                                mask_lag_ms = (time0 - rclcpp::Time(sem_mask_buf.front()->header.stamp).seconds()) * 1000.0;
                                sem_mask_buf.pop();
                            }
                            else
                            {
                                image0.release();
                                image1.release();
                            }
                        }
                        else
                        {
                            tryAttachSemMask(time0, sem_mask, mask_lag_ms);
                        }
                    }
                }
            }
            m_buf.unlock();
            if(!image0.empty())
                estimator.inputImage(time, image0, image1, sem_mask, mask_lag_ms);
        }
        else
        {
            cv::Mat image, sem_mask;
            double time = 0;
            double mask_lag_ms = -1.0;
            m_buf.lock();
            if (!img0_buf.empty())
            {
                const double time0 = rclcpp::Time(img0_buf.front()->header.stamp).seconds();
                if (vinsConfig().sem_enable && vinsConfig().sem_block_on_mask)
                {
                    if (!sem_mask_buf.empty())
                    {
                        const double timeM = rclcpp::Time(sem_mask_buf.front()->header.stamp).seconds();
                        if (time0 < timeM - 0.003)
                            img0_buf.pop();
                        else if (time0 > timeM + 0.003)
                            sem_mask_buf.pop();
                        else
                        {
                            time = time0;
                            image = getImageFromMsg(img0_buf.front());
                            sem_mask = getMaskFromMsg(sem_mask_buf.front());
                            mask_lag_ms = (time0 - timeM) * 1000.0;
                            img0_buf.pop();
                            sem_mask_buf.pop();
                        }
                    }
                }
                else
                {
                    time = time0;
                    image = getImageFromMsg(img0_buf.front());
                    img0_buf.pop();
                    if (vinsConfig().sem_enable)
                        tryAttachSemMask(time0, sem_mask, mask_lag_ms);
                }
            }
            m_buf.unlock();
            if(!image.empty())
                estimator.inputImage(time, image, cv::Mat(), sem_mask, mask_lag_ms);
        }

        std::chrono::milliseconds dura(2);
        std::this_thread::sleep_for(dura);
    }
}


void imu_callback(const sensor_msgs::msg::Imu::SharedPtr imu_msg)
{
    double t = rclcpp::Time(imu_msg->header.stamp).seconds();
    double dx = imu_msg->linear_acceleration.x;
    double dy = imu_msg->linear_acceleration.y;
    double dz = imu_msg->linear_acceleration.z;
    double rx = imu_msg->angular_velocity.x;
    double ry = imu_msg->angular_velocity.y;
    double rz = imu_msg->angular_velocity.z;
    Vector3d acc(dx, dy, dz);
    Vector3d gyr(rx, ry, rz);
    estimator.inputIMU(t, acc, gyr);
    return;
}


void feature_callback(const sensor_msgs::msg::PointCloud::SharedPtr feature_msg)
{
    map<int, vector<pair<int, FeatureObservation>>> featureFrame;
    for (unsigned int i = 0; i < feature_msg->points.size(); i++)
    {
        int feature_id = feature_msg->channels[0].values[i];
        int camera_id = feature_msg->channels[1].values[i];
        double x = feature_msg->points[i].x;
        double y = feature_msg->points[i].y;
        double z = feature_msg->points[i].z;
        double p_u = feature_msg->channels[2].values[i];
        double p_v = feature_msg->channels[3].values[i];
        double velocity_x = feature_msg->channels[4].values[i];
        double velocity_y = feature_msg->channels[5].values[i];
        if(feature_msg->channels.size() > 5)
        {
            double gx = feature_msg->channels[6].values[i];
            double gy = feature_msg->channels[7].values[i];
            double gz = feature_msg->channels[8].values[i];
            vinsConfig().pts_gt[feature_id] = Eigen::Vector3d(gx, gy, gz);
            //printf("receive pts gt %d %f %f %f\n", feature_id, gx, gy, gz);
        }
        assert(z == 1);
        FeatureObservation xyz_uv_velocity;
        xyz_uv_velocity << x, y, z, p_u, p_v, velocity_x, velocity_y, 1.0;
        featureFrame[feature_id].emplace_back(camera_id,  xyz_uv_velocity);
    }
    double t = rclcpp::Time(feature_msg->header.stamp).seconds();
    estimator.inputFeature(t, featureFrame);
    return;
}

void restart_callback(const std_msgs::msg::Bool::SharedPtr restart_msg)
{
    if (restart_msg->data == true)
    {
        RCLCPP_WARN(rclcpp::get_logger("vins"), "restart the estimator!");
        estimator.clearState();
        estimator.setParameter();
    }
    return;
}

void imu_switch_callback(const std_msgs::msg::Bool::SharedPtr switch_msg)
{
    if (switch_msg->data == true)
    {
        //RCLCPP_WARN(rclcpp::get_logger("vins"), "use IMU!");
        estimator.changeSensorType(1, vinsConfig().stereo);
    }
    else
    {
        //RCLCPP_WARN(rclcpp::get_logger("vins"), "disable IMU!");
        estimator.changeSensorType(0, vinsConfig().stereo);
    }
    return;
}

void cam_switch_callback(const std_msgs::msg::Bool::SharedPtr switch_msg)
{
    if (switch_msg->data == true)
    {
        //RCLCPP_WARN(rclcpp::get_logger("vins"), "use stereo!");
        estimator.changeSensorType(vinsConfig().use_imu, 1);
    }
    else
    {
        //RCLCPP_WARN(rclcpp::get_logger("vins"), "use mono camera (left)!");
        estimator.changeSensorType(vinsConfig().use_imu, 0);
    }
    return;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("please intput: ros2 run pht_vio_ros pht_vio_node [config file] \n"
               "for example: ros2 run pht_vio_ros pht_vio_node "
               "$(ros2 pkg prefix pht_vio_ros)/share/pht_vio_ros/config/euroc/euroc_stereo_imu_config.yaml \n");
        return 1;
    }

    string config_file = argv[1];

    rclcpp::init(argc, argv);
    auto n = rclcpp::Node::make_shared("pht_vio_estimator");

    printf("config_file: %s\n", config_file.c_str());

    readParameters(config_file);
    estimator.setParameter();

#ifdef EIGEN_DONT_PARALLELIZE
    RCLCPP_DEBUG(n->get_logger(), "EIGEN_DONT_PARALLELIZE");
#endif

    RCLCPP_WARN(n->get_logger(), "waiting for image and imu...");

    registerPub(n);
    registerEstimatorRosOutputs(estimator);

    rclcpp::Subscription<sensor_msgs::msg::Imu>::SharedPtr sub_imu;
    if(vinsConfig().use_imu)
    {
        sub_imu = n->create_subscription<sensor_msgs::msg::Imu>(
            vinsConfig().imu_topic, 2000, imu_callback);
    }
    auto sub_feature = n->create_subscription<sensor_msgs::msg::PointCloud>(
        "/feature_tracker/feature", 2000, feature_callback);
    auto sub_img0 = n->create_subscription<sensor_msgs::msg::Image>(
        vinsConfig().image0_topic, 100, img0_callback);
    rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr sub_img1;
    if(vinsConfig().stereo)
    {
        sub_img1 = n->create_subscription<sensor_msgs::msg::Image>(
            vinsConfig().image1_topic, 100, img1_callback);
    }
    rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr sub_sem_mask;
    if (vinsConfig().sem_enable)
    {
        sub_sem_mask = n->create_subscription<sensor_msgs::msg::Image>(
            vinsConfig().sem_mask_topic, 100, sem_mask_callback);
        RCLCPP_INFO(n->get_logger(), "SAD-VINS: subscribing semantic mask on %s",
                    vinsConfig().sem_mask_topic.c_str());
    }
    auto sub_restart = n->create_subscription<std_msgs::msg::Bool>(
        "/vins_restart", 100, restart_callback);
    auto sub_imu_switch = n->create_subscription<std_msgs::msg::Bool>(
        "/vins_imu_switch", 100, imu_switch_callback);
    auto sub_cam_switch = n->create_subscription<std_msgs::msg::Bool>(
        "/vins_cam_switch", 100, cam_switch_callback);

    std::thread sync_thread{sync_process};
    rclcpp::spin(n);

    return 0;
}
