/*
 * Day 6 的所有常量：每一路的输入、大图尺寸、编码参数、输出路径。不做命令行参数解析，都写死在这里
 */
#pragma once

#include <cstddef> // size_t
#include <rockchip/rk_mpi.h>

/** 一路输入：码流文件 + 它的格式 */
struct ChannelInput {
    const char *streamPath;   // 码流文件路径
    MppCodingType codingType; // MPP_VIDEO_CodingAVC = H.264，MPP_VIDEO_CodingHEVC = H.265
};

// 4 路输入（2026-11-17 在板子上用 ffprobe 数过帧数）：
//   ch0 aaa.264          H.264 1920×1080  1803 帧（Day 2 的输入）
//   ch1 out_720p.h265    H.265 1280×720   1803 帧（Day 5 M1 的输出）
//   ch2 main_1080p.h264  H.264 1920×1080    60 帧（Day 4 M2 的主码流）
//   ch3 out.h265         H.265 1920×1080    60 帧（Day 1 的输出）
inline constexpr int kChannelCount = 4;
inline constexpr ChannelInput kChannelInputs[kChannelCount] = {
    {"/userdata/av/aaa.264", MPP_VIDEO_CodingAVC},
    {"/userdata/av/out_720p.h265", MPP_VIDEO_CodingHEVC},
    {"/userdata/av/main_1080p.h264", MPP_VIDEO_CodingAVC},
    {"/userdata/av/out.h265", MPP_VIDEO_CodingHEVC},
};

// 每一路的帧队列最多排几帧。每排着一帧就占着那一路解码器内存池（24 块）里的一块
inline constexpr size_t kFrameQueueCapacity = 4;

// 大图：1920×1080，切成 2 列 × 2 行，每格 960×540
inline constexpr int kMosaicWidth = 1920;
inline constexpr int kMosaicHeight = 1080;
inline constexpr int kMosaicColumnCount = 2;
inline constexpr int kMosaicRowCount = 2;

// M2：抽两张大图存成 NV12 原始数据。第 30 张时 4 路都在动；第 300 张时 ch2、ch3 早就播完了（只有 60 帧）
inline constexpr int kM2SnapshotMosaicIndexes[] = {30, 300};
inline constexpr const char *kM2SnapshotPath = "/userdata/av/mosaic_m2_snapshots.yuv";

// M3：大图编码成 H.265 文件
inline constexpr const char *kM3OutputPath = "/userdata/av/mosaic.h265";
inline constexpr int kMosaicBps = 4 * 1000 * 1000; // 目标码率，单位 bit/s（4 Mbps）
inline constexpr int kMosaicFps = 30;              // 帧率：4 路输入都是 30 fps
inline constexpr int kMosaicGop = 60;              // 每 60 帧一个 I 帧（30 fps 下 2 秒）
