cmake_minimum_required(VERSION 3.16)
project(analytics LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(ZMQ REQUIRED libzmq)

add_executable(analytics
    src/cpp/analytics/main.cpp
    src/cpp/common/config.cpp
)

target_include_directories(analytics PRIVATE
    src/cpp
    external/tomlplusplus/include
    external/cppzmq
    external/rapidjson/include
    ${ZMQ_INCLUDE_DIRS}
)

target_link_directories(analytics PRIVATE
    ${ZMQ_LIBRARY_DIRS}
)

target_link_libraries(analytics PRIVATE
    ${ZMQ_LIBRARIES}
)
