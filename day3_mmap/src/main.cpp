/*
 * day3_mmap：在 RK3588 上动手学 mmap，一个程序装 4 个实验，用命令行参数选一个运行
 *
 *   ./day3_mmap m1   M1：到 /proc/self/maps 里查指针是谁        → src/experiments/m1_maps.cpp
 *   ./day3_mmap m2   M2：同一块摄像头 buffer，两个指针          → src/experiments/m2_two_pointers.cpp
 *
 * main 只负责选实验、调用；每个实验的代码在 src/experiments/ 里，
 * 几个实验共用的东西在 src/common/（日志、查 maps）、src/camera/、src/dma_heap/
 */
#include "common/log.h"
#include "experiments/m1_maps.h"
#include "experiments/m2_two_pointers.h"
#include <cstring>

static const char* kTag = "Main";

/**
 * @brief 按命令行参数选一个实验运行
 * @param[in] argc 参数个数
 * @param[in] argv argv[1] 是 m1 / m2
 * @return 实验的返回值（0 = 通过）；参数不对返回 1
 * @note 比较字符串用 strcmp，不用 atoi（atoi 会链接到 GLIBC_2.38，板子上跑不起来）
 */
int main(int argc, char** argv) {
    const char* experimentName = argc > 1 ? argv[1] : "";
    if (strcmp(experimentName, "m1") == 0) {
        return run_m1_maps();
    }
    if (strcmp(experimentName, "m2") == 0) {
        return run_m2_two_pointers();
    }
    run_m2_two_pointers();
    // LOGE(kTag, "用法：%s m1 | m2", argv[0]);
    return 1;
}
