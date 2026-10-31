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

# 代码目录（main.cpp 只负责按参数选实验、调用）：
#   src/main.cpp          ./day3_mmap m1 | m2
#   src/experiments/      每个实验一个文件：m1_maps、m2_two_pointers
#   src/camera/           打开摄像头、申请并 mmap 4 块 buffer（M1 M2 用）
#   src/dma_heap/         从 dma heap 申请 dma_buf（M1 第 3 步、M3 用）
#   src/common/           日志 log.h、到 /proc/self/maps 里查指针 maps_printer
add_executable(day3_mmap
        src/main.cpp
        src/common/log.h
        src/common/maps_printer.cpp
        src/camera/camera_buffers.cpp
        src/dma_heap/dma_heap_buffer.cpp
        src/experiments/m1_maps.cpp
        src/experiments/m2_two_pointers.cpp
        src/experiments/run_m3_sync.cpp
        src/experiments/run_m3_sync.h
)

# 头文件搜索目录加上 src/：所有 #include 都写成相对 src/ 的路径，比如 #include "camera/camera_buffers.h"
target_include_directories(day3_mmap PRIVATE src)

target_compile_options(day3_mmap PRIVATE -Wall -Wextra)
