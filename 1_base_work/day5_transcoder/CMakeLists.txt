cmake_minimum_required(VERSION 4.3)
project(day5_transcoder)

set(CMAKE_CXX_STANDARD 17)

add_executable(day5_transcoder src/main.cpp
        src/app/app_config.h
        src/app/m1_transcode_single_thread.h
        src/common/time_utils.h
        src/common/mpp_utils.h
        src/common/nv12_picture.h
        src/common/nv12_picture.cpp
        src/source/stream_file_reader.cpp
        src/source/stream_file_reader.h
        src/decoder/mpp_decoder.cpp
        src/decoder/mpp_decoder.h
        src/pipeline/decode_stage.cpp
        src/pipeline/decode_stage.h
        src/app/m1_decode_only_check.cpp
        src/encoder/mpp_encoder.cpp
        src/encoder/mpp_encoder.h
        src/rga/rga_resizer.cpp
        src/rga/rga_resizer.h
        src/sink/stream_file_writer.cpp
        src/sink/stream_file_writer.h
        src/pipeline/resize_encode_stage.cpp
        src/pipeline/resize_encode_stage.h
        src/app/m1_transcode_single_thread.cpp
        src/pipeline/frame_queue.cpp
        src/pipeline/frame_queue.h
        src/app/m2_transcode_two_threads.cpp
        src/app/m2_transcode_two_threads.h)

target_include_directories(day5_transcoder PRIVATE src)


if (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    # 链接 MPP 库：rockchip_mpp → 链接器参数 -lrockchip_mpp → 在 sysroot 里找 librockchip_mpp.so
    # V4L2 不用链接任何库：它是内核接口，open + ioctl 就行，头文件 linux/videodev2.h 在 sysroot 里
    # rga → -lrga → sysroot 里的 librga.so（M1 用 RGA 缩放）
    find_package(Threads REQUIRED)
    target_link_libraries(day5_transcoder PRIVATE rockchip_mpp rga Threads::Threads)

    # 定义宏 RK_TARGET=1，可以在代码里用 #ifdef RK_TARGET 区分"板子上"和"Mac 上"
    target_compile_definitions(day5_transcoder PRIVATE RK_TARGET=1)
    # target_compile_options：给编译器（g++）加参数，编译每个 .cpp 时都带上。下面 3 个都是"打开警告"的开关，
    # -W 开头 = Warning。警告不会让编译失败，只是把"能编译、但很可能写错了"的地方打印出来
    #   -Wall     打开一批常用警告（名字叫 all，其实不是全部）。例：定义了变量没用到；
    #             有符号数和无符号数比较（int i < vector.size()）
    #   -Wextra   在 -Wall 之外再多打开一批。例：函数参数没用到；结构体初始化时漏写了某个字段
    #   -Wshadow  内层的名字和外层同名、把外层"遮住"时警告。例：参数和成员都叫 chunkSizeBytes，
    #             chunkSizeBytes = chunkSizeBytes; 是参数自己赋给自己，成员一直是 0。所以参数统一叫 requestedXxx
    target_compile_options(day5_transcoder PRIVATE -Wall -Wextra -Wshadow)

    # 板子 /usr/lib 里出厂的 MPP 库太旧，运行时优先加载自己编译的那份（/userdata/mpp_build/lib）
    # -Wl,-rpath,目录：把"运行时先去这个目录找库"写进程序文件，效果和 LD_LIBRARY_PATH 一样，
    # 在板子上直接 ./day4_rga 就能跑，CLion 用 GDB 调试也不用额外配置
    target_link_options(day5_transcoder PRIVATE "-Wl,-rpath,/userdata/mpp_build/lib")
endif ()

