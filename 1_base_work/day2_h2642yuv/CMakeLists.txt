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

# 先写成面条代码，全部在 main.cpp 里；跑通后再拆成 src/ 下的类（和 Day 1 一样）
add_executable(day2_h2642yuv
        src/mpi_dec_light.cpp
        src/mpi_dec_utils.c
)

target_compile_options(day2_h2642yuv PRIVATE -Wall -Wextra)


# 交叉编译到 RK3588 时，链接 MPP 硬件编解码库
if (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    target_link_libraries(day2_h2642yuv PRIVATE rockchip_mpp)
    target_compile_definitions(day2_h2642yuv PRIVATE RK_TARGET=1)
    # 板子 /usr/lib 里出厂的 MPP 库太旧，运行时优先加载 /userdata/mpp_build/lib 里自己编译的那份
    target_link_options(day2_h2642yuv PRIVATE "-Wl,-rpath,/userdata/mpp_build/lib")
endif ()
