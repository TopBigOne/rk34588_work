cmake_minimum_required(VERSION 4.3)
project(day4_rga)

set(CMAKE_CXX_STANDARD 20)

add_executable(day4_rga
        src/main.cpp
        src/common/log.h
        src/common/mpp_utils.h
        src/encoder/mpp_encoder.cpp
        src/encoder/mpp_encoder.h
        src/rga/rga_resizer.cpp
        src/rga/rga_resizer.h)

# 头文件搜索目录加上 src：任何文件里都可以写 #include "common/log.h"，不用数 ../ 有几层
target_include_directories(day4_rga PRIVATE src)


if (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    # 链接 MPP 库：rockchip_mpp → 链接器参数 -lrockchip_mpp → 在 sysroot 里找 librockchip_mpp.so
    # V4L2 不用链接任何库：它是内核接口，open + ioctl 就行，头文件 linux/videodev2.h 在 sysroot 里
    # rga → -lrga → sysroot 里的 librga.so（M1 用 RGA 缩放）
    target_link_libraries(day4_rga PRIVATE rockchip_mpp rga)

    # 定义宏 RK_TARGET=1，可以在代码里用 #ifdef RK_TARGET 区分"板子上"和"Mac 上"
    target_compile_definitions(day4_rga PRIVATE RK_TARGET=1)
    target_compile_options(day4_rga PRIVATE -Wall -Wextra)

    # 板子 /usr/lib 里出厂的 MPP 库太旧，运行时优先加载自己编译的那份（/userdata/mpp_build/lib）
    # -Wl,-rpath,目录：把"运行时先去这个目录找库"写进程序文件，效果和 LD_LIBRARY_PATH 一样，
    # 在板子上直接 ./day4_rga 就能跑，CLion 用 GDB 调试也不用额外配置
    target_link_options(day4_rga PRIVATE "-Wl,-rpath,/userdata/mpp_build/lib")
endif ()
