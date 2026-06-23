cmake_minimum_required(VERSION 3.8)
project(pht_vio_ros)

set(CMAKE_BUILD_TYPE "Release")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -Wall -g")

find_package(ament_cmake REQUIRED)
find_package(pht_vio REQUIRED)
find_package(pht_slam_ros_common REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(visualization_msgs REQUIRED)
find_package(cv_bridge REQUIRED)
find_package(tf2 REQUIRED)
find_package(tf2_ros REQUIRED)
find_package(Ceres REQUIRED)
find_package(Boost REQUIRED COMPONENTS filesystem program_options system)

set(DEPS
  rclcpp std_msgs geometry_msgs nav_msgs sensor_msgs
  visualization_msgs cv_bridge tf2 tf2_ros
)

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/../pht_vio/src
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_library(pht_vio_ros SHARED
  src/ros/visualization.cpp
  src/ros/estimator_ros_adapter.cpp
)
target_link_libraries(pht_vio_ros
  pht_vio::pht_vio
  pht_slam_ros_common::pht_slam_ros_common
)
ament_target_dependencies(pht_vio_ros ${DEPS})

add_executable(pht_vio_node src/rosNodeTest.cpp)
target_link_libraries(pht_vio_node pht_vio_ros)
ament_target_dependencies(pht_vio_node ${DEPS})

add_executable(kitti_odom_test src/KITTIOdomTest.cpp)
target_link_libraries(kitti_odom_test pht_vio_ros)
ament_target_dependencies(kitti_odom_test ${DEPS})

add_executable(kitti_gps_test src/KITTIGPSTest.cpp)
target_link_libraries(kitti_gps_test pht_vio_ros)
ament_target_dependencies(kitti_gps_test ${DEPS})

install(TARGETS pht_vio_node kitti_odom_test kitti_gps_test
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY launch/ DESTINATION share/${PROJECT_NAME}/launch)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../config/
  DESTINATION share/${PROJECT_NAME}/config
)

ament_package()
