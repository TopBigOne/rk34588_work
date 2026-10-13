# day2_h2642yuv 的构建配置（和 day1_yuv2h264 一样，详细注释见 ../day1_yuv2h264/CMakeLists.txt）
#
# ⚠️ CLion 里要新建 RK3588-Debug 这个 CMake profile，否则编不过（找不到 rockchip/rk_mpi.h）：
#   Settings → Build, Execution, Deployment → CMake → + →
#     Name：RK3588-Debug
#     CMake options：-DCMAKE_TOOLCHAIN_FILE=/Users/dev/rk-toolchain/rk3588-toolchain.cmake
#     Build directory：cmake-build-rk3588-debug（run_on_board_with_mac.sh 从这里找程序）
#
# 编译命令（run_on_board_with_mac.sh 里也是这么调的）：
#   /Applications/CLion.app/Contents/bin/cmake/mac/aarch64/bin/cmake --build cmake-build-rk3588-debug --target day2_h2642yuv

cmake_minimum_required(VERSION 4.3)
project(day2_h2642yuv)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 生成一个可执行文件：目标名 day2_h2642yuv，源文件都在 src/ 下，按功能分目录（和 day1_yuv2h264 一样）：
#   src/main.cpp     只负责"组装"
#   src/app/         解析命令行参数
#   src/decoder/     MppDecoder：MPP 硬件解码器（含 info change 和内存池）
#   src/source/      输入：StreamSource 接口 + ReadStream（码流文件）；以后的 RTSP 拉流也放这里
#   src/sink/        输出：FrameSink 接口 + WriteYuv（NV12 文件）；以后的送显示也放这里
#   src/pipeline/    DecodePipeline（流程骨架）+ DecodeStats（统计）
#   src/common/      公共工具：MPP_CHECK、RAII 句柄（只有头文件）
# 设计见 guide/03_重构成类.md
add_executable(day2_h2642yuv
        src/main.cpp
        src/app/args.cpp
        src/decoder/mpp_decoder.cpp
        src/source/read_stream.cpp
        src/sink/write_yuv.cpp
        src/pipeline/decode_pipeline.cpp
        src/pipeline/decode_stats.cpp
)

# 头文件搜索目录加上 src/：所有 #include 都写成相对 src/ 的路径，比如 #include "decoder/mpp_decoder.h"
target_include_directories(day2_h2642yuv PRIVATE src)

target_compile_options(day2_h2642yuv PRIVATE -Wall -Wextra)


# 交叉编译到 RK3588 时，链接 MPP 硬件编解码库
if (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    target_link_libraries(day2_h2642yuv PRIVATE rockchip_mpp)
    target_compile_definitions(day2_h2642yuv PRIVATE RK_TARGET=1)
    # 板子 /usr/lib 里出厂的 MPP 库太旧，运行时优先加载 /userdata/mpp_build/lib 里自己编译的那份
    target_link_options(day2_h2642yuv PRIVATE "-Wl,-rpath,/userdata/mpp_build/lib")
endif ()
