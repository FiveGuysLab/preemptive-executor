cmake_minimum_required(VERSION 3.5)

project(preemptive_executor)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable compile commands export for LSP support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic -Wnon-virtual-dtor -Woverloaded-virtual)
endif()

# find dependencies
find_package(ament_cmake_ros REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rcl REQUIRED)
find_package(std_msgs REQUIRED)
find_package(std_srvs REQUIRED)
find_package(Threads REQUIRED)
find_package(yaml-cpp REQUIRED)

set(${PROJECT_NAME}_SRCS
  src/preemptive_executor.cpp
  src/bundled_timer.cpp
  src/bundled_subscription.cpp
  src/yaml_parser.cpp
  src/callback_registry.cpp
  src/worker_group.cpp
)

add_library(${PROJECT_NAME} ${${PROJECT_NAME}_SRCS})
target_include_directories(${PROJECT_NAME} PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/concurrentqueue>"
  "$<INSTALL_INTERFACE:include/${PROJECT_NAME}>")
ament_target_dependencies(${PROJECT_NAME}
  rclcpp
  rcl
  std_msgs
  std_srvs
)
target_link_libraries(${PROJECT_NAME}
  rclcpp::rclcpp
  rcl::rcl
  Threads::Threads
  yaml-cpp
)

install(
  DIRECTORY include/
  DESTINATION include/${PROJECT_NAME}
)

install(
  TARGETS ${PROJECT_NAME}
  EXPORT ${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)
  
# Export old-style CMake variables
ament_export_include_directories("include/${PROJECT_NAME}")
ament_export_libraries(${PROJECT_NAME})

# Export modern CMake targets
ament_export_targets(${PROJECT_NAME})

# Export dependencies
ament_export_dependencies(rclcpp rcl std_msgs std_srvs)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  # the following line skips the linter which checks for copyrights
  # uncomment the line when a copyright and license is not present in all source files
  #set(ament_cmake_copyright_FOUND TRUE)
  # the following line skips cpplint (only works in a git repo)
  # uncomment the line when this package is not in a git repo
  #set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
