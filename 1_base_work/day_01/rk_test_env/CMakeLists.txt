cmake_minimum_required(VERSION 3.16)
project(rk_test_env CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(rk_test_env main.cpp)

# 交叉编译到 RK3588 时（用了 rk3588-toolchain.cmake），链接 MPP 硬件编解码库
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    target_link_libraries(rk_test_env PRIVATE rockchip_mpp)
    target_compile_definitions(rk_test_env PRIVATE RK_TARGET=1)
endif()
