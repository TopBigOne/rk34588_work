# day1_yuv2h264 的构建配置
#
# 同一份 CMakeLists.txt 有两种用法，区别在于 CLion 的 CMake profile 有没有指定工具链文件：
#   ① RK3588-Debug（交叉编译，日常用这个）
#        CMake options：-DCMAKE_TOOLCHAIN_FILE=/Users/dev/rk-toolchain/rk3588-toolchain.cmake
#        编译目录：cmake-build-rk3588-debug/，编出来的是 aarch64 的 Linux 程序，只能在板子上跑
#   ② 不指定工具链（Mac 本地编译）
#        用的是 Mac 自带的编译器，找不到 rockchip/rk_mpi.h，main.cpp 编不过 —— 正常现象
#        （之前遇到的 "'rockchip/rk_mpi.h' file not found" 就是 profile 没配工具链）
#
# 工具链文件 rk3588-toolchain.cmake 做了三件事：
#   - 指定交叉编译器 aarch64-buildroot-linux-gnu-gcc / g++
#   - 设置 CMAKE_SYSTEM_PROCESSOR = aarch64（下面的 if 就靠它判断是不是在交叉编译）
#   - 设置 sysroot：头文件（rockchip/rk_mpi.h）和库（librockchip_mpp.so）都从 sysroot 里找
#
# 编译命令（run_on_board_with_mac.sh 里也是这么调的）：
#   /Applications/CLion.app/Contents/bin/cmake/mac/aarch64/bin/cmake --build cmake-build-rk3588-debug --target day1_yuv2h264

# 要求的最低 CMake 版本。CLion 自带的是 4.3.1
cmake_minimum_required(VERSION 4.3)

# 项目名。也是 CLion 里显示的名字
project(day1_yuv2h264)

# 用 C++17 编译（main.cpp 里用到了 C++17 的 "if (初始化; 条件)" 写法）
# REQUIRED ON：编译器不支持 C++17 就直接报错，而不是悄悄退回到旧标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 生成一个可执行文件：目标名 day1_yuv2h264，源文件都在 src/ 下，按功能分目录：
#   src/main.cpp     只负责"组装"
#   src/app/         解析命令行参数
#   src/encoder/     MppEncoder：MPP 硬件编码器
#   src/source/      输入：FrameSource 接口 + ReadYUV（NV12 文件）；Day 3 的摄像头输入也放这里
#   src/sink/        输出：StreamSink 接口 + WriteStream（码流文件）；以后的推流也放这里
#   src/pipeline/    EncodePipeline（流程骨架）+ EncodeStats（统计）
#   src/common/      公共工具：ALIGN、MPP_CHECK、RAII 句柄（只有头文件）
# 编译结果：cmake-build-rk3588-debug/day1_yuv2h264
add_executable(day1_yuv2h264
    src/main.cpp
    src/app/args.cpp
    src/encoder/mpp_encoder.cpp
    src/source/read_yuv.cpp
    src/sink/write_stream.cpp
    src/pipeline/encode_pipeline.cpp
    src/pipeline/encode_stats.cpp)

# 头文件搜索目录加上 src/：所有 #include 都写成相对 src/ 的路径，比如 #include "encoder/mpp_encoder.h"，
# 一眼就能看出头文件在哪个目录，不同目录里有同名文件也不会冲突
# PRIVATE：只有这个目标自己用
target_include_directories(day1_yuv2h264 PRIVATE src)


# 交叉编译到 RK3588 时（用了 rk3588-toolchain.cmake），链接 MPP 硬件编解码库
# Mac 本地编译时 CMAKE_SYSTEM_PROCESSOR 是 arm64，不会进这个 if
if (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    # 链接 MPP 库：rockchip_mpp → 链接器参数 -lrockchip_mpp → 在 sysroot 里找 librockchip_mpp.so
    # 链接器会读出这个库的 SONAME（librockchip_mpp.so.1），记进程序里；板子上运行时按这个名字找库
    # PRIVATE：只有这个目标自己用，不会传递给依赖它的其他目标
    # ⚠️ 这里的目标名必须和 add_executable 里的一致，否则报 "Cannot specify link libraries for target"
    target_link_libraries(day1_yuv2h264 PRIVATE rockchip_mpp)

    # 定义宏 RK_TARGET=1，相当于在代码最前面写了 #define RK_TARGET 1
    # 可以在代码里用 #ifdef RK_TARGET 区分"板子上"和"Mac 上"。目前 main.cpp 还没有用到
    target_compile_definitions(day1_yuv2h264 PRIVATE RK_TARGET=1)

    # 板子 /usr/lib 里出厂的 MPP 库太旧，运行时优先加载自己编译的那份
    # -Wl,xxx：把 xxx 传给链接器；-rpath,目录：把"运行时先去这个目录找库"写进程序文件
    # （用 readelf -d 能看到：(RPATH) Library rpath: [/userdata/mpp_build/lib]）
    # 效果和 LD_LIBRARY_PATH=/userdata/mpp_build/lib 一样，但不用每次手动设：
    # 在板子上直接 ./day1_yuv2h264 就能跑，CLion 用 GDB 调试也不用额外配置
    # 缺点：路径写死在程序里，自己编译的库换了位置要改这里并重新编译
    target_link_options(day1_yuv2h264 PRIVATE "-Wl,-rpath,/userdata/mpp_build/lib")
endif ()
