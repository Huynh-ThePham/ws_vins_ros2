cmake_minimum_required(VERSION 3.8)
project(pht_slam_common)

set(CMAKE_CXX_STANDARD 17)

find_package(ament_cmake REQUIRED)
find_package(Eigen3 REQUIRED)

add_library(pht_slam_common SHARED src/utility.cpp)
target_include_directories(pht_slam_common PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
if(TARGET Eigen3::Eigen)
  target_link_libraries(pht_slam_common Eigen3::Eigen)
else()
  target_include_directories(pht_slam_common PUBLIC ${EIGEN3_INCLUDE_DIRS})
endif()

install(TARGETS pht_slam_common
  EXPORT export_pht_slam_common
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)
install(DIRECTORY include/ DESTINATION include)

ament_export_targets(export_pht_slam_common HAS_LIBRARY_TARGET)
ament_export_dependencies(Eigen3)
ament_export_include_directories(include)
ament_package()
