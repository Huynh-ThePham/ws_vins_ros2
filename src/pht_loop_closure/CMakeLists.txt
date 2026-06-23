cmake_minimum_required(VERSION 3.8)
project(pht_loop_closure)

set(CMAKE_BUILD_TYPE "Release")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -Wall -g")

find_package(ament_cmake REQUIRED)
find_package(pht_slam_common REQUIRED)
find_package(pht_camera_models REQUIRED)
find_package(OpenCV REQUIRED)
find_package(Ceres REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(Boost REQUIRED COMPONENTS filesystem program_options system)

include_directories(
  src
  ${EIGEN3_INCLUDE_DIRS}
  ${CERES_INCLUDE_DIRS}
)

add_library(pht_loop_closure SHARED
  src/keyframe.cpp
  src/pose_graph.cpp
  src/loop_closure_config.cpp
  src/ThirdParty/DBoW/BowVector.cpp
  src/ThirdParty/DBoW/FBrief.cpp
  src/ThirdParty/DBoW/FeatureVector.cpp
  src/ThirdParty/DBoW/QueryResults.cpp
  src/ThirdParty/DBoW/ScoringObject.cpp
  src/ThirdParty/DUtils/Random.cpp
  src/ThirdParty/DUtils/Timestamp.cpp
  src/ThirdParty/DVision/BRIEF.cpp
  src/ThirdParty/VocabularyBinary.cpp
)

target_link_libraries(pht_loop_closure
  pht_slam_common::pht_slam_common
  ${OpenCV_LIBS}
  ${CERES_LIBRARIES}
)
ament_target_dependencies(pht_loop_closure pht_camera_models)

target_include_directories(pht_loop_closure PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:include>
)

install(TARGETS pht_loop_closure
  EXPORT export_pht_loop_closure
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

install(FILES src/keyframe.h src/pose_graph.h src/loop_types.hpp src/loop_closure_config.hpp
  DESTINATION include/pht_loop_closure
)

ament_export_targets(export_pht_loop_closure HAS_LIBRARY_TARGET)
ament_export_dependencies(pht_slam_common pht_camera_models)
ament_package()
