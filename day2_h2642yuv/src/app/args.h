/*
 * 【解析参数】命令行参数 → Args
 *
 * 用法：./day2_h2642yuv [-i in.h264] [-o out.nv12] [-t h264|h265] [-n 帧数]
 */
#pragma once

#include <rockchip/rk_mpi.h>

// 命令行参数。默认值就是不带参数运行时用的值
struct Args {
    const char* streamInputPath = "/userdata/av/aaa.264"; // -i：输入的 H.264 / H.265 裸流
    const char* yuvOutputPath   = nullptr;                // -o：输出的 NV12 文件；不给就不写文件（测纯解码速度）
    // MppCodingType：编码格式。MPP_VIDEO_CodingAVC = H.264，MPP_VIDEO_CodingHEVC = H.265
    MppCodingType type = MPP_VIDEO_CodingAVC;
    int maxFrames      = 0; // -n：最多解多少帧，0 = 解到码流结束（EOS）
};

// 打印用法（参数错误时调用）
void usage(const char* prog);

/**
 * 解析命令行参数。每个参数都是 "-名字 值" 成对出现
 * @param argc  main 的 argc
 * @param argv  main 的 argv
 * @param a     解析结果写到这里（引用，函数会改它）
 * @return      参数都合法返回 true；有错打印原因，返回 false（调用方打印用法后退出）
 */
bool parse_args(int argc, char** argv, Args& a);
