cmake_minimum_required(VERSION 3.8)
project(pht_slam_ros_common)

set(CMAKE_CXX_STANDARD 17)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(visualization_msgs REQUIRED)
find_package(Eigen3 REQUIRED)

add_library(pht_slam_ros_common SHARED src/CameraPoseVisualization.cpp)
target_include_directories(pht_slam_ros_common PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:include>
)
ament_target_dependencies(pht_slam_ros_common rclcpp std_msgs visualization_msgs)
target_link_libraries(pht_slam_ros_common Eigen3::Eigen)

install(TARGETS pht_slam_ros_common
  EXPORT export_pht_slam_ros_common
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)
install(FILES src/CameraPoseVisualization.h
  DESTINATION include/pht_slam_ros_common
)

ament_export_targets(export_pht_slam_ros_common HAS_LIBRARY_TARGET)
ament_export_dependencies(rclcpp std_msgs visualization_msgs Eigen3)
ament_package()
