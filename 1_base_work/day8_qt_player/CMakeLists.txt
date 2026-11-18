cmake_minimum_required(VERSION 3.20)
project(day8_qt_player)

set(CMAKE_CXX_STANDARD 17)

# AUTOMOC：CMake 自动找出带 Q_OBJECT 的头文件，先用 moc 生成信号槽需要的 moc_xxx.cpp，再一起编译。
# 用板子工具链编译时，moc 是 rk3588-toolchain.cmake 里定义的 Qt5::moc（转发到 rk-dev 里执行）
set(CMAKE_AUTOMOC ON)

add_executable(day8_qt_player
        src/main.cpp
        src/common/log.h
        src/common/mpp_utils.h
        src/common/ffmpeg_utils.h
        src/common/time_utils.h
        src/demux/mp4_demuxer.h
        src/demux/mp4_demuxer.cpp
        src/video/annexb_converter.h
        src/video/annexb_converter.cpp
        src/video/mpp_video_decoder.h
        src/video/mpp_video_decoder.cpp
        src/video/rgbx_picture.h
        src/video/rga_color_converter.h
        src/video/rga_color_converter.cpp
        src/audio/software_audio_decoder.h
        src/audio/software_audio_decoder.cpp
        src/audio/audio_resampler.h
        src/audio/audio_resampler.cpp
        src/audio/audio_output_format.h
        src/audio/alsa_audio_output.h
        src/audio/alsa_audio_output.cpp
        src/sync/media_clock.h
        src/sync/media_clock.cpp
        src/pipeline/playback_state.h
        src/pipeline/packet_queue.h
        src/pipeline/packet_queue.cpp
        src/pipeline/demux_worker.h
        src/pipeline/demux_worker.cpp
        src/pipeline/video_worker.h
        src/pipeline/video_worker.cpp
        src/pipeline/audio_worker.h
        src/pipeline/audio_worker.cpp
        src/player/player_controller.h
        src/player/player_controller.cpp
        src/ui/video_widget.h
        src/ui/video_widget.cpp
        src/ui/player_window.h
        src/ui/player_window.cpp
        src/app/m1_decode_video_to_nv12.h
        src/app/m1_decode_video_to_nv12.cpp
        src/app/m3_play_audio_only.h
        src/app/m3_play_audio_only.cpp)

# 头文件搜索目录加上 src：任何文件里都写 #include "common/log.h"，不用数 ../ 有几层
target_include_directories(day8_qt_player PRIVATE src)

# -Wall -Wextra：打开常用警告；-Wshadow：参数和成员同名时警告（项目代码规范要求）
target_compile_options(day8_qt_player PRIVATE -Wall -Wextra -Wshadow)

# Qt5：在 sysroot 里找 usr/lib/cmake/Qt5Widgets/Qt5WidgetsConfig.cmake，Widgets 会顺带找到 Core、Gui
find_package(Qt5 REQUIRED COMPONENTS Widgets)

# ffmpeg 和 ALSA 没有 CMake 配置文件，只有 pkg-config 的 .pc 文件（sysroot/usr/lib/pkgconfig/libavformat.pc 等）。
# pkg_check_modules(变量前缀 REQUIRED IMPORTED_TARGET 模块...)：读这些 .pc，生成一个导入的 target
# PkgConfig::变量前缀，链接它就同时得到头文件目录和 -lavformat -lavcodec ... 这些链接参数。
# 工具链文件里已经把 PKG_CONFIG_LIBDIR 指到 sysroot，所以找到的是板子的库，不是 Mac 的
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswresample)
pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)

# Threads::Threads → 多线程要的库（std::thread）
find_package(Threads REQUIRED)

# rockchip_mpp → -lrockchip_mpp（视频硬解）；rga → -lrga（颜色转换 + 缩放）
target_link_libraries(day8_qt_player PRIVATE
        Qt5::Widgets
        PkgConfig::FFMPEG
        PkgConfig::ALSA
        rockchip_mpp
        rga
        Threads::Threads)

# 板子 /usr/lib 里出厂的 MPP 库太旧，运行时优先加载自己编译的那份（/userdata/mpp_build/lib）
# -Wl,-rpath,目录：把"运行时先去这个目录找库"写进程序文件，效果和 LD_LIBRARY_PATH 一样
target_link_options(day8_qt_player PRIVATE "-Wl,-rpath,/userdata/mpp_build/lib")
