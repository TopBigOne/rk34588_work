cmake_minimum_required(VERSION 3.20)
project(n10_ai_event)

set(CMAKE_CXX_STANDARD 20)

# 不依赖 MPP / RGA / RKNN 的纯 CPU 部分：板子程序和 Mac 单测共用
set(PORTABLE_SOURCES
        src/common/detection_types.h
        src/npu/candidate_object.h
        src/npu/tensor_quant_info.h
        src/npu/letterbox.h
        src/npu/letterbox.cpp
        src/npu/nms.h
        src/npu/nms.cpp
        src/npu/yolov5_post_process.h
        src/npu/yolov5_post_process.cpp
        src/app/post_process_comparison.h
        src/app/post_process_comparison.cpp
        src/third_party/rknn_yolov5_demo/postprocess.h
        src/third_party/rknn_yolov5_demo/postprocess.cc)

# 第三方代码原样复制，不为它们修警告
set_source_files_properties(
        src/third_party/stb/stb_image_impl.cpp
        src/third_party/rknn_yolov5_demo/postprocess.cc
        PROPERTIES COMPILE_OPTIONS "-w")

if (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    add_executable(n10_ai_event
            ${PORTABLE_SOURCES}
            src/main.cpp
            src/app/app_config.h
            src/app/image_input.h
            src/app/image_input.cpp
            src/app/m0_detect_image.h
            src/app/m0_detect_image.cpp
            src/app/m1_detect_file.h
            src/app/m1_detect_file.cpp
            src/common/log.h
            src/common/time_utils.h
            src/common/mpp_utils.h
            src/common/nv12_picture.h
            src/common/nv12_picture.cpp
            src/source/stream_file_reader.h
            src/source/stream_file_reader.cpp
            src/decoder/mpp_decoder.h
            src/decoder/mpp_decoder.cpp
            src/pipeline/frame_queue.h
            src/pipeline/frame_queue.cpp
            src/pipeline/decode_stage.h
            src/pipeline/decode_stage.cpp
            src/pipeline/detect_stage.h
            src/pipeline/detect_stage.cpp
            src/rga/rga_preprocessor.h
            src/rga/rga_preprocessor.cpp
            src/npu/rknn_model.h
            src/npu/rknn_model.cpp
            src/npu/object_detector.h
            src/npu/object_detector.cpp
            src/third_party/stb/stb_image.h
            src/third_party/stb/stb_image_impl.cpp)
    target_include_directories(n10_ai_event PRIVATE src)
    find_package(Threads REQUIRED)
    # rknnrt → librknnrt.so（2.0.0b0，头文件 sysroot/usr/include/rknn/）；rockchip_mpp → 解码；rga → 前处理
    target_link_libraries(n10_ai_event PRIVATE rknnrt rockchip_mpp rga Threads::Threads)
    target_compile_options(n10_ai_event PRIVATE -Wall -Wextra -Wshadow)
    # /userdata/mpp_build/lib：新版 MPP；/userdata/n10/lib：板子上没有 librknnrt.so 时 push SDK 那份到这里
    target_link_options(n10_ai_event PRIVATE "-Wl,-rpath,/userdata/mpp_build/lib:/userdata/n10/lib")
else ()
    # Mac：cmake -B build-mac && cmake --build build-mac && ./build-mac/n10_mac_tests
    add_executable(n10_mac_tests ${PORTABLE_SOURCES} src/tests/mac_tests.cpp)
    target_include_directories(n10_mac_tests PRIVATE src)
    target_compile_options(n10_mac_tests PRIVATE -Wall -Wextra -Wshadow)
    # demo 后处理在第一次调用时读标签文件；Mac 上换成仓库里的这份
    target_compile_definitions(n10_mac_tests PRIVATE
            LABEL_NALE_TXT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/src/tests/coco_80_labels_list.txt")
endif ()
