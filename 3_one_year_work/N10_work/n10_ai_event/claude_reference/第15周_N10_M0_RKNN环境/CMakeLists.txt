cmake_minimum_required(VERSION 3.20)
project(n10_ai_event)

set(CMAKE_CXX_STANDARD 20)

add_executable(n10_ai_event
        src/main.cpp
        src/app/app_config.h
        src/app/m0_detect_image.h
        src/app/m0_detect_image.cpp
        src/common/log.h
        src/common/time_utils.h
        src/common/detection_types.h
        src/npu/rknn_model.h
        src/npu/rknn_model.cpp
        src/third_party/stb/stb_image.h
        src/third_party/stb/stb_image_impl.cpp
        src/third_party/rknn_yolov5_demo/postprocess.h
        src/third_party/rknn_yolov5_demo/postprocess.cc)

target_include_directories(n10_ai_event PRIVATE src)

# 第三方代码原样复制，不为它们修警告
set_source_files_properties(
        src/third_party/stb/stb_image_impl.cpp
        src/third_party/rknn_yolov5_demo/postprocess.cc
        PROPERTIES COMPILE_OPTIONS "-w")

if (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    find_package(Threads REQUIRED)
    # rknnrt → sysroot 里的 librknnrt.so（2.0.0b0）；头文件在 sysroot/usr/include/rknn/，代码里写 <rknn/rknn_api.h>
    target_link_libraries(n10_ai_event PRIVATE rknnrt Threads::Threads)
    target_compile_options(n10_ai_event PRIVATE -Wall -Wextra -Wshadow)
    # /userdata/mpp_build/lib：M1 起要用的新版 MPP；/userdata/n10/lib：板子上没有 librknnrt.so 时 push SDK 那份到这里
    target_link_options(n10_ai_event PRIVATE "-Wl,-rpath,/userdata/mpp_build/lib:/userdata/n10/lib")
endif ()
