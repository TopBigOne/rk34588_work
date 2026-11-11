cmake_minimum_required(VERSION 4.3)
project(day5_transcoder)

set(CMAKE_CXX_STANDARD 17)

add_executable(day5_transcoder src/main.cpp
        src/app/app_config.h
        src/app/m1_transcode_single_thread.h
        src/common/time_utils.h
        src/common/mpp_utils.h
        src/common/nv12_picture.h
        src/source/stream_file_reader.cpp
        src/source/stream_file_reader.h
        src/decoder/mpp_decoder.cpp
        src/decoder/mpp_decoder.h
        src/pipeline/decode_stage.cpp
        src/pipeline/decode_stage.h
        src/app/m1_decode_only_check.cpp)

target_include_directories(day5_transcoder PRIVATE src)


if (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    # 链接 MPP 库：rockchip_mpp → 链接器参数 -lrockchip_mpp → 在 sysroot 里找 librockchip_mpp.so
    # V4L2 不用链接任何库：它是内核接口，open + ioctl 就行，头文件 linux/videodev2.h 在 sysroot 里
    # rga → -lrga → sysroot 里的 librga.so（M1 用 RGA 缩放）
    target_link_libraries(day5_transcoder PRIVATE rockchip_mpp rga)

    # 定义宏 RK_TARGET=1，可以在代码里用 #ifdef RK_TARGET 区分"板子上"和"Mac 上"
    target_compile_definitions(day5_transcoder PRIVATE RK_TARGET=1)
    target_compile_options(day5_transcoder PRIVATE -Wall -Wextra)

    # 板子 /usr/lib 里出厂的 MPP 库太旧，运行时优先加载自己编译的那份（/userdata/mpp_build/lib）
    # -Wl,-rpath,目录：把"运行时先去这个目录找库"写进程序文件，效果和 LD_LIBRARY_PATH 一样，
    # 在板子上直接 ./day4_rga 就能跑，CLion 用 GDB 调试也不用额外配置
    target_link_options(day5_transcoder PRIVATE "-Wl,-rpath,/userdata/mpp_build/lib")
endif ()

