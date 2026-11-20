cmake_minimum_required(VERSION 3.20)
project(n0_nvr_mosaic)

# C++20：要用指定初始化（EncoderConfig{.width = 1920, ...}）
set(CMAKE_CXX_STANDARD 20)

add_executable(n0_nvr_mosaic
        src/main.cpp
        src/app/app_config.h
        src/app/m1_decode_channels.h
        src/app/m1_decode_channels.cpp
        src/common/log.h
        src/common/mpp_utils.h
        src/common/time_utils.h
        src/common/nv12_picture.h
        src/common/nv12_picture.cpp
        src/source/stream_file_reader.h
        src/source/stream_file_reader.cpp
        src/decoder/mpp_decoder.h
        src/decoder/mpp_decoder.cpp
        src/pipeline/decode_stage.h
        src/pipeline/decode_stage.cpp
        src/app/channel_helpers.h
        src/app/channel_helpers.cpp
        src/app/m2_compose_mosaic.h
        src/app/m2_compose_mosaic.cpp
        src/rga/rga_compositor.h
        src/rga/rga_compositor.cpp
        src/sink/nv12_file_writer.h
        src/sink/nv12_file_writer.cpp
        src/pipeline/frame_queue.h
        src/pipeline/frame_queue.cpp
        src/pipeline/decode_channel.h
        src/pipeline/decode_channel.cpp
        src/pipeline/mosaic_compose_stage.h
        src/pipeline/mosaic_compose_stage.cpp
        src/app/m3_encode_mosaic.h
        src/app/m3_encode_mosaic.cpp
        src/encoder/mpp_encoder.h
        src/encoder/mpp_encoder.cpp
        src/sink/stream_file_writer.h
        src/sink/stream_file_writer.cpp
        src/pipeline/encode_stage.h
        src/pipeline/encode_stage.cpp
        src/app/m4_show_mosaic.h
        src/app/m4_show_mosaic.cpp
        src/sync/master_clock.h
        src/sync/master_clock.cpp
        src/app/m5_self_test.h
        src/app/m5_self_test.cpp
        src/app/m5_aligned_mosaic.h
        src/app/m5_aligned_mosaic.cpp)

# 头文件搜索目录加上 src：任何文件里都写 #include "common/log.h"，不用数 ../ 有几层
target_include_directories(n0_nvr_mosaic PRIVATE src)

if (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    # rockchip_mpp → 链接器参数 -lrockchip_mpp → 在 sysroot 里找 librockchip_mpp.so（解码、编码）
    # rga → -lrga → sysroot 里的 librga.so（缩放、贴图、填色）
    # Threads::Threads → 多线程要的库（std::thread）
    find_package(Threads REQUIRED)
    target_link_libraries(n0_nvr_mosaic PRIVATE rockchip_mpp rga Threads::Threads)

    # -Wall -Wextra：打开常用警告；-Wshadow：内层名字遮住外层（参数和成员同名）时警告
    target_compile_options(n0_nvr_mosaic PRIVATE -Wall -Wextra -Wshadow)

    # 板子 /usr/lib 里出厂的 MPP 库太旧，运行时优先加载自己编译的那份（/userdata/mpp_build/lib）
    # -Wl,-rpath,目录：把"运行时先去这个目录找库"写进程序文件，效果和 LD_LIBRARY_PATH 一样
    target_link_options(n0_nvr_mosaic PRIVATE "-Wl,-rpath,/userdata/mpp_build/lib")
endif ()
