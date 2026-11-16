//
// day5_transcoder：1080p H.264 → 解码 → RGA 缩到 720p → 编码 H.265
//
// 用法：./day5_transcoder decode | m1 | m2（不写参数默认 m2，当前在做的里程碑）
//   decode  只解码、数帧数 → app/m1_decode_only_check.cpp
//   m1      单线程转码     → app/m1_transcode_single_thread.cpp
//   m2      双线程转码     → app/m2_transcode_two_threads.cpp
//
#include <cstring> // strcmp

#include "app/m1_transcode_single_thread.h"
#include "app/m2_transcode_two_threads.h"
#include "app/milestones.h" // runM1DecodeOnlyCheck
#include "common/log.h"

static const char *const kTag = "Main";

/**
 * @brief 程序入口：按第一个参数选择跑哪一项
 * @param[in] argc  参数个数（包括程序名）
 * @param[in] argv  参数；argv[1] 是要跑的那一项的名字："decode"、"m1" 或 "m2"，不写默认 "m2"
 * @return 0 = 成功；1 = 失败或参数不认识
 * @note main 只做分发，不放业务逻辑
 */
int main(int argc, char **argv) {
    const char *milestoneName = argc > 1 ? argv[1] : "m2";
    // strcmp：两个字符串一样返回 0。不能写 milestoneName == "m1"，那比的是两个指针（地址）
    if (strcmp(milestoneName, "decode") == 0) {
        return runM1DecodeOnlyCheck();
    }
    if (strcmp(milestoneName, "m1") == 0) {
        return runM1TranscodeSingleThread();
    }
    if (strcmp(milestoneName, "m2") == 0) {
        return runM2TranscodeTwoThreads();
    }
    LOGE(kTag, "用法：%s decode | m1 | m2", argv[0]);
    return 1;
}
