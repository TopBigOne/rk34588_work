cmake_minimum_required(VERSION 3.20)
project(n10_ai_event)

set(CMAKE_CXX_STANDARD 20)

# FFmpeg（libavformat 读码流、写 MP4，N3 的代码）和 SQLite（事件表）
# 板子：工具链文件把 PKG_CONFIG_LIBDIR 指到 sysroot（FFmpeg 4.4.1、SQLite 3.39.2）
# Mac：先 export PKG_CONFIG_PATH=/usr/local/ffmpeg/4.4/lib/pkgconfig:/opt/homebrew/opt/sqlite/lib/pkgconfig
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil)
pkg_check_modules(SQLITE REQUIRED IMPORTED_TARGET sqlite3)

# 不依赖 MPP / RGA / RKNN 的部分：板子程序和 Mac 单测共用
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
        src/npu/yolov8_post_process.h
        src/npu/yolov8_post_process.cpp
        src/third_party/rknn_model_zoo_yolov8/yolov8_demo_decode.h
        src/third_party/rknn_model_zoo_yolov8/yolov8_demo_decode.cc
        src/app/post_process_comparison.h
        src/app/post_process_comparison.cpp
        src/third_party/rknn_yolov5_demo/postprocess.h
        src/third_party/rknn_yolov5_demo/postprocess.cc
        src/common/encoded_frame.h
        src/common/video_codec.h
        src/common/annexb_utils.h
        src/common/annexb_utils.cpp
        src/common/ffmpeg_utils.h
        src/common/log.h
        src/source/ffmpeg_frame_source.h
        src/source/ffmpeg_frame_source.cpp
        src/storage/mp4_file_writer.h
        src/storage/mp4_file_writer.cpp
        src/storage/mp4_probe.h
        src/storage/mp4_probe.cpp
        src/overlay/nv12_box_drawer.h
        src/overlay/nv12_box_drawer.cpp
        src/event/pre_record_buffer.h
        src/event/pre_record_buffer.cpp
        src/event/event_state_machine.h
        src/event/event_state_machine.cpp
        src/event/event_index.h
        src/event/event_index.cpp
        src/event/event_recorder.h
        src/event/event_recorder.cpp
        src/pipeline/latest_slot.h)

# 第三方代码原样复制，不为它们修警告
set_source_files_properties(
        src/third_party/stb/stb_image_impl.cpp
        src/third_party/rknn_yolov5_demo/postprocess.cc
        src/third_party/rknn_model_zoo_yolov8/yolov8_demo_decode.cc
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
            src/app/m2_event_record.h
            src/app/m2_event_record.cpp
            src/app/m3_benchmark.h
            src/app/m3_benchmark.cpp
            src/pipeline/detect_worker_pool.h
            src/pipeline/detect_worker_pool.cpp
            src/overlay/nv12_frame_overlay.h
            src/overlay/nv12_frame_overlay.cpp
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
    target_link_libraries(n10_ai_event PRIVATE rknnrt rockchip_mpp rga PkgConfig::FFMPEG PkgConfig::SQLITE Threads::Threads)
    target_compile_options(n10_ai_event PRIVATE -Wall -Wextra -Wshadow)
    # /userdata/mpp_build/lib：新版 MPP；/userdata/n10/lib：板子上没有 librknnrt.so 时 push SDK 那份到这里
    target_link_options(n10_ai_event PRIVATE "-Wl,-rpath,/userdata/mpp_build/lib:/userdata/n10/lib")
else ()
    # Mac：（先设 PKG_CONFIG_PATH，见上）cmake -B build-mac && cmake --build build-mac && ./build-mac/n10_mac_tests
    add_executable(n10_mac_tests ${PORTABLE_SOURCES} src/tests/check.h src/tests/mac_tests.cpp src/tests/event_mac_tests.cpp
            src/tests/yolov8_mac_tests.cpp)
    target_link_libraries(n10_mac_tests PRIVATE PkgConfig::FFMPEG PkgConfig::SQLITE)
    target_include_directories(n10_mac_tests PRIVATE src)
    target_compile_options(n10_mac_tests PRIVATE -Wall -Wextra -Wshadow)
    # demo 后处理在第一次调用时读标签文件；Mac 上换成仓库里的这份
    target_compile_definitions(n10_mac_tests PRIVATE
            LABEL_NALE_TXT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/src/tests/coco_80_labels_list.txt")
endif ()
