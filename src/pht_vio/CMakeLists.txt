cmake_minimum_required(VERSION 3.8)
project(pht_vio)

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

add_library(pht_vio SHARED
  src/estimator/parameters.cpp
  src/estimator/estimator.cpp
  src/estimator/feature_manager.cpp
  src/factor/pose_local_parameterization.cpp
  src/factor/projectionTwoFrameOneCamFactor.cpp
  src/factor/projectionTwoFrameTwoCamFactor.cpp
  src/factor/projectionOneFrameTwoCamFactor.cpp
  src/factor/marginalization_factor.cpp
  src/initial/solve_5pts.cpp
  src/initial/initial_aligment.cpp
  src/initial/initial_sfm.cpp
  src/initial/initial_ex_rotation.cpp
  src/featureTracker/feature_tracker.cpp
  src/offline/euroc_dataset_reader.cpp
)

target_link_libraries(pht_vio
  pht_slam_common::pht_slam_common
  ${OpenCV_LIBS}
  ${CERES_LIBRARIES}
)
ament_target_dependencies(pht_vio pht_camera_models)

target_include_directories(pht_vio PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:include>
)

add_executable(pht_vio_offline src/offline/vio_offline_main.cpp)
target_link_libraries(pht_vio_offline pht_vio)
ament_target_dependencies(pht_vio_offline pht_camera_models)

if(BUILD_TESTING)
  enable_testing()

  # Header-only tests: no ROS, no GPU, no dataset. Kept fast so CI can gate on them.
  set(PHT_VIO_HEADER_TESTS
    sem_geodf_risk
    sem_geodf_risk_properties
    adaptive_factor_quality
    failure_detection
  )
  foreach(test_name IN LISTS PHT_VIO_HEADER_TESTS)
    add_executable(test_${test_name} test/test_${test_name}.cpp)
    target_include_directories(test_${test_name} PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src
      ${CMAKE_CURRENT_SOURCE_DIR}/test
      ${EIGEN3_INCLUDE_DIRS}
    )
    add_test(NAME ${test_name} COMMAND test_${test_name})
  endforeach()

  # Tests that need the compiled estimator (Ceres factors, marginalization).
  # Still ROS-node-free and dataset-free.
  set(PHT_VIO_LINKED_TESTS
    marginalization_weight_freeze
    projection_two_frame_one_cam_gradient
    projection_two_frame_two_cam_gradient
    projection_one_frame_two_cam_gradient
  )
  foreach(test_name IN LISTS PHT_VIO_LINKED_TESTS)
    add_executable(test_${test_name} test/test_${test_name}.cpp)
    target_include_directories(test_${test_name} PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src
      ${CMAKE_CURRENT_SOURCE_DIR}/test
      ${EIGEN3_INCLUDE_DIRS}
      ${CERES_INCLUDE_DIRS}
    )
    target_link_libraries(test_${test_name} pht_vio)
    add_test(NAME ${test_name} COMMAND test_${test_name})
  endforeach()
endif()

install(TARGETS pht_vio_offline
  DESTINATION lib/${PROJECT_NAME}
)

install(TARGETS pht_vio
  EXPORT export_pht_vio
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

install(DIRECTORY src/estimator src/factor src/initial src/featureTracker src/offline
  DESTINATION include/pht_vio
  FILES_MATCHING PATTERN "*.h"
)

ament_export_targets(export_pht_vio HAS_LIBRARY_TARGET)
ament_export_dependencies(pht_slam_common pht_camera_models Ceres Eigen3 OpenCV)
ament_package()
