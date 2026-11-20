cmake_minimum_required(VERSION 3.20)
project(n3_recorder CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 板子：工具链文件把 PKG_CONFIG_LIBDIR 指到 sysroot（FFmpeg 4.4.1）。
# Mac：用本机 /usr/local/ffmpeg/4.4 的 .pc（构建前 export PKG_CONFIG_PATH=/usr/local/ffmpeg/4.4/lib/pkgconfig）
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil)

set(N3_CORE_SOURCES
        src/common/annexb_utils.cpp
        src/source/ffmpeg_frame_source.cpp
        src/storage/mp4_file_writer.cpp
        src/storage/record_path_builder.cpp
        src/storage/segment_recorder.cpp)

add_library(n3_core STATIC ${N3_CORE_SOURCES})
target_include_directories(n3_core PUBLIC src)
target_link_libraries(n3_core PUBLIC PkgConfig::FFMPEG)
target_compile_options(n3_core PUBLIC -Wall -Wextra -Wshadow)

add_executable(n3_recorder
        src/main.cpp
        src/app/m1_remux_to_mp4.cpp
        src/app/m2_segmented_record.cpp)
target_link_libraries(n3_recorder PRIVATE n3_core)

if (NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64" OR APPLE)
    # Mac 上的测试程序：只测不依赖板子的逻辑
    add_executable(n3_mac_tests tests/mac_tests.cpp)
    target_link_libraries(n3_mac_tests PRIVATE n3_core)
endif ()

# 读 MP4 box 的小工具（只用标准库，Mac、板子都能编）
add_executable(mp4_box_walker tools/mp4_box_walker.cpp)
target_compile_options(mp4_box_walker PRIVATE -Wall -Wextra -Wshadow)
