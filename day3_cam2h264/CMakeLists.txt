# day3_cam2h264 的构建配置（照 day1_yuv2h264/CMakeLists.txt）
#
# 同一份 CMakeLists.txt 有两种用法，区别在于 CLion 的 CMake profile 有没有指定工具链文件：
#   ① RK3588-Debug（交叉编译，日常用这个）
#        CMake options：-DCMAKE_TOOLCHAIN_FILE=/Users/dev/rk-toolchain/rk3588-toolchain.cmake
#        编译目录：cmake-build-rk3588-debug/，编出来的是 aarch64 的 Linux 程序，只能在板子上跑
#   ② 不指定工具链（Mac 本地编译）
#        用的是 Mac 自带的编译器，找不到 rockchip/rk_mpi.h、linux/videodev2.h —— 正常现象
#
# 编译命令（run_on_board_with_mac.sh 里也是这么调的）：
#   /Applications/CLion.app/Contents/bin/cmake/mac/aarch64/bin/cmake --build cmake-build-rk3588-debug --target day3_cam2h264

# 要求的最低 CMake 版本。CLion 自带的是 4.3.1
cmake_minimum_required(VERSION 4.3)

# 项目名。也是 CLion 里显示的名字
project(day3_cam2h264)

# 用 C++17 编译；REQUIRED ON：编译器不支持 C++17 就直接报错，而不是悄悄退回到旧标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 生成一个可执行文件：目标名 day3_cam2h264，源文件都在 src/ 下
# 现在只有 src/main.cpp（先写面条代码跑通 V4L2 采集）；写到编码那一步时，
# 再把 Day 1 的 MppEncoder 加进来，并按 Day 1 的目录结构拆分（app / common / encoder / source / sink / pipeline）
# 编译结果：cmake-build-rk3588-debug/day3_cam2h264
add_executable(day3_cam2h264
    src/main.cpp)

# 头文件搜索目录加上 src/：所有 #include 都写成相对 src/ 的路径，比如 #include "source/v4l2_camera.h"
# PRIVATE：只有这个目标自己用
target_include_directories(day3_cam2h264 PRIVATE src)

# 交叉编译到 RK3588 时（用了 rk3588-toolchain.cmake），链接 MPP 硬件编解码库
# Mac 本地编译时 CMAKE_SYSTEM_PROCESSOR 是 arm64，不会进这个 if
if (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    # 链接 MPP 库：rockchip_mpp → 链接器参数 -lrockchip_mpp → 在 sysroot 里找 librockchip_mpp.so
    # V4L2 不用链接任何库：它是内核接口，open + ioctl 就行，头文件 linux/videodev2.h 在 sysroot 里
    target_link_libraries(day3_cam2h264 PRIVATE rockchip_mpp)

    # 定义宏 RK_TARGET=1，可以在代码里用 #ifdef RK_TARGET 区分"板子上"和"Mac 上"
    target_compile_definitions(day3_cam2h264 PRIVATE RK_TARGET=1)

    # 板子 /usr/lib 里出厂的 MPP 库太旧，运行时优先加载自己编译的那份（/userdata/mpp_build/lib）
    # -Wl,-rpath,目录：把"运行时先去这个目录找库"写进程序文件，效果和 LD_LIBRARY_PATH 一样，
    # 在板子上直接 ./day3_cam2h264 就能跑，CLion 用 GDB 调试也不用额外配置
    target_link_options(day3_cam2h264 PRIVATE "-Wl,-rpath,/userdata/mpp_build/lib")
endif ()
