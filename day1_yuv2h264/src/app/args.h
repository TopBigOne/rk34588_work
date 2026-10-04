/*
 * 【解析参数】命令行参数 → Args → EncoderConfig
 *
 * 用法：./day1_yuv2h264 -i in.nv12 -o out.h264 [-w 1920] [-h 1080] [-t h264|h265]
 *                       [-rc cbr|vbr|avbr] [-bps 4000000] [-fps 30] [-g 60]
 */
#pragma once

#include "encoder/mpp_encoder.h" // EncoderConfig
#include <rockchip/rk_mpi.h>

// 命令行参数。默认值就是不带参数运行时用的值
struct Args {
    const char* nv12InputPath    = "/userdata/av/in_1080p_60f.nv12"; // -i：输入的 NV12 原始图像文件
    const char* streamOutputPath = "/userdata/av/out.h264";          // -o：输出的 H.264 / H.265 码流文件（裸流）
    int width                    = 1920;
    int height                   = 1080;
    // MppCodingType：编码格式。MPP_VIDEO_CodingAVC = H.264，MPP_VIDEO_CodingHEVC = H.265
    MppCodingType type = MPP_VIDEO_CodingAVC;
    // MppEncRcMode：码率控制模式。CBR 恒定码率 / VBR 可变码率 / AVBR 自适应可变码率
    MppEncRcMode rcMode = MPP_ENC_RC_MODE_CBR;
    int bps             = 4 * 1000 * 1000; // 目标码率，单位 bit/s
    int fps             = 30;
    int gop             = 0; // 0 表示用默认值 fps × 2（每 2 秒一个 I 帧）
};

// 打印用法（参数错误时调用）
void usage(const char* prog);

/**
 * 解析命令行参数。每个参数都是 "-名字 值" 成对出现
 * 不用 getopt，因为 -rc、-bps 是多个字母，getopt 只认单字母
 * @param argc  main 的 argc
 * @param argv  main 的 argv
 * @param a     解析结果写到这里（引用，函数会改它）
 * @return      参数都合法返回 true；有错返回 false（调用方打印用法后退出）
 */
bool parse_args(int argc, char** argv, Args& a);

// 命令行参数 → 编码器参数（只取编码器关心的那几项，文件路径不给编码器）
EncoderConfig to_encoder_config(const Args& a);
