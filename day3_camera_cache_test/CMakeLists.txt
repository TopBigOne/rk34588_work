# day3_camera_cache_test：探针程序，测"摄像头的 buffer 对 CPU 来说是 cached 还是 uncached"
#
# 和 day3_cam2h264 一样交叉编译到 RK3588：
#   CLion 的 RK3588-Debug profile → CMake options：-DCMAKE_TOOLCHAIN_FILE=/Users/dev/rk-toolchain/rk3588-toolchain.cmake
#   编译目录：cmake-build-rk3588-debug/
# 不用 MPP：只用 V4L2（摄像头）和 dma_heap（申请两块已知是 cached / uncached 的内存当参照），都是内核接口，不用链接任何库

cmake_minimum_required(VERSION 4.3)
project(day3_camera_cache_test)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(day3_camera_cache_test main.cpp)

# 测速度的程序要开优化，否则 memcpy 以外的循环（逐 8 字节读）会被 -O0 拖慢，结果不准
target_compile_options(day3_camera_cache_test PRIVATE -O2 -Wall -Wextra)
