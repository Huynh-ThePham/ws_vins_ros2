cmake_minimum_required(VERSION 3.8)
project(pht_loop_closure_ros)

set(CMAKE_BUILD_TYPE "Release")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -Wall -g")

find_package(ament_cmake REQUIRED)
find_package(ament_index_cpp REQUIRED)
find_package(pht_loop_closure REQUIRED)
find_package(pht_slam_ros_common REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(visualization_msgs REQUIRED)
find_package(cv_bridge REQUIRED)
find_package(pht_camera_models REQUIRED)
find_package(Ceres REQUIRED)
find_package(Boost REQUIRED COMPONENTS filesystem program_options system)

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/../pht_loop_closure/src
)

add_executable(pht_loop_closure_node
  src/pose_graph_node.cpp
  src/pose_graph_ros_adapter.cpp
)
target_link_libraries(pht_loop_closure_node
  pht_loop_closure::pht_loop_closure
  pht_slam_ros_common::pht_slam_ros_common
)
ament_target_dependencies(pht_loop_closure_node
  ament_index_cpp rclcpp std_msgs nav_msgs sensor_msgs
  geometry_msgs visualization_msgs cv_bridge pht_camera_models
)

set(SUPPORT_FILES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../support_files)
set(BRIEF_VOCAB ${SUPPORT_FILES_DIR}/brief_k10L6.bin)
if(NOT EXISTS ${BRIEF_VOCAB})
  message(STATUS "Downloading brief_k10L6.bin loop-closure vocabulary...")
  file(DOWNLOAD
    "https://github.com/HKUST-Aerial-Robotics/VINS-Fusion/raw/master/support_files/brief_k10L6.bin"
    ${BRIEF_VOCAB}
    SHOW_PROGRESS
    TLS_VERIFY ON
  )
endif()

install(TARGETS pht_loop_closure_node DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../support_files/
  DESTINATION share/${PROJECT_NAME}/support_files
)

ament_package()
