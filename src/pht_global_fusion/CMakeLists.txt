cmake_minimum_required(VERSION 3.8)
project(pht_global_fusion)

set(CMAKE_BUILD_TYPE "Release")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -Wall -g")

find_package(ament_cmake REQUIRED)
find_package(pht_slam_common REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(Ceres REQUIRED)
find_package(Eigen3 REQUIRED)

include_directories(
  src
  ${CERES_INCLUDE_DIRS}
  ${EIGEN3_INCLUDE_DIRS}
  ThirdParty/GeographicLib/include
)

add_library(pht_global_fusion SHARED
  src/globalOpt.cpp
  ThirdParty/GeographicLib/src/LocalCartesian.cpp
  ThirdParty/GeographicLib/src/Geocentric.cpp
  ThirdParty/GeographicLib/src/Math.cpp
)

target_link_libraries(pht_global_fusion
  pht_slam_common::pht_slam_common
  ${CERES_LIBRARIES}
)
ament_target_dependencies(pht_global_fusion nav_msgs)

target_include_directories(pht_global_fusion PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:include>
)

install(TARGETS pht_global_fusion
  EXPORT export_pht_global_fusion
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

install(FILES src/globalOpt.h src/Factors.h
  DESTINATION include/pht_global_fusion
)

ament_export_targets(export_pht_global_fusion HAS_LIBRARY_TARGET)
ament_export_dependencies(pht_slam_common Ceres Eigen3)
ament_package()
