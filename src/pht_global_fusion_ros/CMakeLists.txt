cmake_minimum_required(VERSION 3.8)
project(pht_global_fusion_ros)

set(CMAKE_BUILD_TYPE "Release")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -Wall -g")

find_package(ament_cmake REQUIRED)
find_package(pht_global_fusion REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(visualization_msgs REQUIRED)

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/../pht_global_fusion/src
  ${CMAKE_CURRENT_SOURCE_DIR}/../pht_global_fusion/ThirdParty/GeographicLib/include
)

add_executable(pht_global_fusion_node src/globalOptNode.cpp)
target_link_libraries(pht_global_fusion_node pht_global_fusion::pht_global_fusion)
ament_target_dependencies(pht_global_fusion_node
  rclcpp std_msgs nav_msgs sensor_msgs visualization_msgs
)

install(TARGETS pht_global_fusion_node DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY models/ DESTINATION share/${PROJECT_NAME}/models)
install(DIRECTORY launch/ DESTINATION share/${PROJECT_NAME}/launch)

ament_package()
