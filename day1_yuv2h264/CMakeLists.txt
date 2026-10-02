cmake_minimum_required(VERSION 4.3)
project(day1_yuv2h264)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(day1_yuv2h264 main.cpp)


# 交叉编译到 RK3588 时（用了 rk3588-toolchain.cmake），链接 MPP 硬件编解码库
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    target_link_libraries(day1_yuv2h264 PRIVATE rockchip_mpp)
    target_compile_definitions(day1_yuv2h264 PRIVATE RK_TARGET=1)
endif()
