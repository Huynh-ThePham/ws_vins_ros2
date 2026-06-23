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

#include <iostream>
#include <stdio.h>
#include <opencv2/opencv.hpp>
#include <cmath>
#include <string>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <cv_bridge/cv_bridge.h>
#include "estimator/estimator.h"
#include "ros/estimator_ros_adapter.h"
#include "ros/visualization.h"

using namespace std;
using namespace Eigen;

Estimator estimator;

Eigen::Matrix3d c1Rc0, c0Rc1;
Eigen::Vector3d c1Tc0, c0Tc1;

int main(int argc, char** argv)
{
	rclcpp::init(argc, argv);
	auto n = rclcpp::Node::make_shared("pht_vio_estimator");

	auto pubLeftImage = n->create_publisher<sensor_msgs::msg::Image>("/leftImage", 1000);
	auto pubRightImage = n->create_publisher<sensor_msgs::msg::Image>("/rightImage", 1000);

	if(argc != 3)
	{
		printf("please intput: ros2 run pht_vio_ros kitti_odom_test [config file] [data folder] \n"
			   "for example: ros2 run pht_vio_ros kitti_odom_test "
			   "$(ros2 pkg prefix pht_vio_ros)/share/pht_vio_ros/config/kitti_odom/kitti_config00-02.yaml "
			   "/path/to/kitti/odometry/sequences/00/ \n");
		return 1;
	}

	string config_file = argv[1];
	printf("config_file: %s\n", argv[1]);
	string sequence = argv[2];
	printf("read sequence: %s\n", argv[2]);
	string dataPath = sequence + "/";

	readParameters(config_file);
	estimator.setParameter();
	registerPub(n);
	registerEstimatorRosOutputs(estimator);

	// load image list
	FILE* file;
	file = std::fopen((dataPath + "times.txt").c_str() , "r");
	if(file == NULL){
	    printf("cannot find file: %stimes.txt\n", dataPath.c_str());
	    assert(false);
	    return 0;          
	}
	double imageTime;
	vector<double> imageTimeList;
	while ( fscanf(file, "%lf", &imageTime) != EOF)
	{
	    imageTimeList.push_back(imageTime);
	}
	std::fclose(file);

	string leftImagePath, rightImagePath;
	cv::Mat imLeft, imRight;
	FILE* outFile;
	outFile = fopen((vinsConfig().output_folder + "/vio.txt").c_str(),"w");
	if(outFile == NULL)
		printf("Output path dosen't exist: %s\n", vinsConfig().output_folder.c_str());

	for (size_t i = 0; i < imageTimeList.size(); i++)
	{	
		if(rclcpp::ok())
		{
			printf("\nprocess image %d\n", (int)i);
			stringstream ss;
			ss << setfill('0') << setw(6) << i;
			leftImagePath = dataPath + "image_0/" + ss.str() + ".png";
			rightImagePath = dataPath + "image_1/" + ss.str() + ".png";

			imLeft = cv::imread(leftImagePath, cv::IMREAD_GRAYSCALE);
			std_msgs::msg::Header header;
			header.stamp = rclcpp::Time(static_cast<int64_t>(imageTimeList[i] * 1e9));
			sensor_msgs::msg::Image::SharedPtr imLeftMsg = cv_bridge::CvImage(header, "mono8", imLeft).toImageMsg();
			pubLeftImage->publish(*imLeftMsg);

			imRight = cv::imread(rightImagePath, cv::IMREAD_GRAYSCALE);
			sensor_msgs::msg::Image::SharedPtr imRightMsg = cv_bridge::CvImage(header, "mono8", imRight).toImageMsg();
			pubRightImage->publish(*imRightMsg);


			estimator.inputImage(imageTimeList[i], imLeft, imRight);
			
			Eigen::Matrix<double, 4, 4> pose;
			estimator.getPoseInWorldFrame(pose);
			if(outFile != NULL)
				fprintf (outFile, "%f %f %f %f %f %f %f %f %f %f %f %f \n",pose(0,0), pose(0,1), pose(0,2),pose(0,3),
																		       pose(1,0), pose(1,1), pose(1,2),pose(1,3),
																		       pose(2,0), pose(2,1), pose(2,2),pose(2,3));
		}
		else
			break;
	}
	if(outFile != NULL)
		fclose (outFile);
	return 0;
}
