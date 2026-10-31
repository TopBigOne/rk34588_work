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
    // argc：命令行有几个词（包括程序名）；argv[0] 是程序名，argv[1] 是第一个参数。
    //   运行 ./day3_mmap m1 时，argc = 2，argv[1] = "m1"；不带参数时 argc = 1，没有 argv[1]，所以先判断 argc > 1
    // "" 这种写在代码里的字符串（字面量）放在程序的只读段（.rodata），只能读不能写，所以类型带 const。
    //   const char* 的意思是"不能通过它改字符"，指针本身可以改指向
    const char* experimentName = argc > 1 ? argv[1] : "";
    // strlen：数到 '\0' 为止有几个字符，在运行时数。不能用 sizeof：sizeof(指针) 永远是 8
    if (strlen(experimentName) == 0) {
        // 不写那块内存，改指针的指向：
        experimentName = "m2";
    }

    // strcmp：两个字符串一样时返回 0（不是 true）。main 的返回值就是程序的退出码，shell 里 echo $? 能看到
    if (strcmp(experimentName, "m1") == 0) {
        return run_m1_maps();
    }
    if (strcmp(experimentName, "m2") == 0) {
        return run_m2_two_pointers();
    }
    // LOGE(kTag, "用法：%s m1 | m2", argv[0]);
    return 1;
}
