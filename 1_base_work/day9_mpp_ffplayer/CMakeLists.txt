cmake_minimum_required(VERSION 3.20)
project(MppPlayer C)

# MppPlayer：SimpleFFPlay6.0（FFmpeg 6.0 的 ffplay 拆分版）移植到 RK3588，视频改用 MPP 硬解。
# 只支持交叉编译到板子：CMake options 填 -DCMAKE_TOOLCHAIN_FILE=/Users/dev/rk-toolchain/rk3588-toolchain.cmake
# （和 day8 一样，CLion 里用 RK3588 那个 CMake profile）

# gnu11：C11 加 GNU 扩展。ffplay / cmdutils 用到了 POSIX 和 GNU 的一些函数，纯 c11 会缺声明
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)

# ---------------- FFmpeg 6.0（自己交叉编译的，见 build_ffmpeg6_for_board.sh 和 doc/交叉编译FFmpeg6.0.md）----------------
# 板子 /usr/lib 和 sysroot 里的 ffmpeg 是 4.4.1，不能用；这里用自己编的 6.0
set(FFMPEG_PREFIX "/Users/dev/rk-toolchain/ffmpeg6-rk3588" CACHE PATH "交叉编译好的 FFmpeg 6.0 安装目录（含 include/ 和 lib/）")
if (NOT EXISTS "${FFMPEG_PREFIX}/include/libavcodec/avcodec.h")
    message(FATAL_ERROR "FFMPEG_PREFIX=${FFMPEG_PREFIX} 下没找到 FFmpeg 头文件，先运行 ./build_ffmpeg6_for_board.sh")
endif ()

# 逐个在 FFMPEG_PREFIX/lib 里找库，得到完整路径。
# NO_DEFAULT_PATH：不去系统目录找；NO_CMAKE_FIND_ROOT_PATH：工具链文件把 find_library 限制在 sysroot 里
# （CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY 会把路径改成 sysroot/路径），这个目录不在 sysroot 里，要关掉这个改写
set(FFMPEG_LIBRARIES)
foreach (ffmpegLibraryName avcodec avformat avutil swscale swresample avfilter avdevice postproc)
    find_library(FFMPEG_${ffmpegLibraryName}_LIBRARY NAMES ${ffmpegLibraryName}
            PATHS "${FFMPEG_PREFIX}/lib" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    if (NOT FFMPEG_${ffmpegLibraryName}_LIBRARY)
        message(FATAL_ERROR "在 ${FFMPEG_PREFIX}/lib 里没找到 lib${ffmpegLibraryName}")
    endif ()
    list(APPEND FFMPEG_LIBRARIES ${FFMPEG_${ffmpegLibraryName}_LIBRARY})
endforeach ()

# ---------------- SDL2（sysroot 里自带 2.28.5，带 Wayland 显示、GLES2 渲染、ALSA 声音）----------------
# 读 sysroot/usr/lib/pkgconfig/sdl2.pc，生成 PkgConfig::SDL2，链接它就带上 include/SDL2 和 -lSDL2
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

find_package(Threads REQUIRED)

set(SOURCE_FILES
        # FFmpeg fftools 公共代码（命令行解析、banner、-version 等）；config.h 是 aarch64 交叉编译时生成的那份
        src/fftools/config.h
        src/fftools/config_components.h
        src/fftools/cmdutils.h
        src/fftools/cmdutils.c
        src/fftools/opt_common.h
        src/fftools/opt_common.c
        src/fftools/libavutil/getenv_utf8.h
        src/fftools/libavutil/libm.h

        # 公共：头文件、常量、全局选项
        src/common/ffplay_common.h
        src/common/player_options.h
        src/common/player_options.c

        # 管线：包队列 → 解码器 → 帧队列
        src/pipeline/packet_queue.h
        src/pipeline/packet_queue.c
        src/pipeline/frame_queue.h
        src/pipeline/frame_queue.c
        src/pipeline/decoder.h
        src/pipeline/decoder.c

        # 时钟与音画同步
        src/sync/clock.h
        src/sync/clock.c
        src/sync/av_sync.h
        src/sync/av_sync.c

        # 播放器总状态与控制
        src/player/video_state.h
        src/player/player_control.h
        src/player/player_control.c

        # 解复用：读线程、流的打开/关闭
        src/demux/read_thread.h
        src/demux/read_thread.c
        src/demux/stream.h
        src/demux/stream.c

        # 音频：解码线程、SDL 输出
        src/audio/audio_decoder_thread.h
        src/audio/audio_decoder_thread.c
        src/audio/audio_output.h
        src/audio/audio_output.c

        # 视频：MPP 硬解（MppPlayer 新增）、解码线程、刷新调度、渲染、音频可视化
        src/video/mpp_video_decoder.h
        src/video/mpp_video_decoder.c
        src/video/sdl_texture_format.h
        src/video/video_decoder_thread.h
        src/video/video_decoder_thread.c
        src/video/video_refresh.h
        src/video/video_refresh.c
        src/video/video_render.h
        src/video/video_render.c
        src/video/audio_visualizer.h
        src/video/audio_visualizer.c

        # 字幕
        src/subtitle/subtitle_thread.h
        src/subtitle/subtitle_thread.c

        # 滤镜
        src/filter/av_filters.h
        src/filter/av_filters.c

        # 界面：SDL 句柄、事件循环
        src/ui/sdl_context.h
        src/ui/sdl_context.c
        src/ui/event_loop.h
        src/ui/event_loop.c
        src/ui/control_bar.h
        src/ui/control_bar.c

        # 入口：命令行选项、main
        src/app/ffplay_options.h
        src/app/ffplay_options.c
        src/app/main.c
)

add_executable(MppPlayer ${SOURCE_FILES})

# 不让 CMake 自动把 Mac 上的库目录（/Users/dev/rk-toolchain/ffmpeg6-rk3588/lib）写进 rpath：板子上没有这个路径。
# 板子要用的 rpath 在最下面 target_link_options 里手动写
set_target_properties(MppPlayer PROPERTIES SKIP_BUILD_RPATH ON)

# src：模块之间写 #include "pipeline/packet_queue.h" 这种带目录的路径
# src/fftools：cmdutils / opt_common / config.h，以及 FFmpeg 不对外安装的 libavutil/getenv_utf8.h、libm.h
target_include_directories(MppPlayer PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/fftools
        ${FFMPEG_PREFIX}/include)

# 自己新写的文件打开项目规范要求的警告（-Wshadow：参数和成员同名）。
# ffplay 原版代码保持原样，不对它开这些警告，免得满屏都是和移植无关的提示
set_source_files_properties(src/video/mpp_video_decoder.c src/ui/control_bar.c PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wshadow")

# rockchip_mpp → -lrockchip_mpp（sysroot 里有头文件和库，编译时用它；运行时用 rpath 指向的新版）
target_link_libraries(MppPlayer PRIVATE
        ${FFMPEG_LIBRARIES}
        PkgConfig::SDL2
        rockchip_mpp
        Threads::Threads
        m)

# 运行时先去这两个目录找库（效果和 LD_LIBRARY_PATH 一样，但写进了程序文件，直接双击桌面图标也能找到）：
#   /userdata/ffmpeg6/lib     自己编的 FFmpeg 6.0（run 脚本推上去）
#   /userdata/mpp_build/lib   自己编的新版 MPP（板子 /usr/lib 里出厂的太旧）
target_link_options(MppPlayer PRIVATE "-Wl,-rpath,/userdata/ffmpeg6/lib:/userdata/mpp_build/lib")
