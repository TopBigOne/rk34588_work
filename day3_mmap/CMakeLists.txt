# day3_mmap：在 RK3588 上动手学 mmap（摄像头 buffer、dma_buf、DMA_BUF_IOCTL_SYNC、DRM dumb buffer）
#
# 和 day3_cam2h264 一样交叉编译到 RK3588：
#   CLion 的 RK3588-Debug profile → CMake options：-DCMAKE_TOOLCHAIN_FILE=/Users/dev/rk-toolchain/rk3588-toolchain.cmake
#   编译目录：cmake-build-rk3588-debug/
# 不用 MPP：只用 V4L2、dma-buf、DRM 这些内核接口（open + ioctl + mmap），不用链接任何库

cmake_minimum_required(VERSION 4.3)
project(day3_mmap)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(day3_mmap
        main.cpp
        log.h
)

target_compile_options(day3_mmap PRIVATE -Wall -Wextra)
