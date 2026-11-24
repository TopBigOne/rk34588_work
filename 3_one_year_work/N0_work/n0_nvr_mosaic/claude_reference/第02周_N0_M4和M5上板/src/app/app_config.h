/*
 * n0_nvr_mosaic 的所有常量：每一路的输入、大图尺寸、编码参数、输出路径。不做命令行参数解析，都写死在这里
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
//   ch0 aaa.264          H.264 1920×1080  1803 帧
//   ch1 out_720p.h265    H.265 1280×720   1803 帧
//   ch2 main_1080p.h264  H.264 1920×1080    60 帧
//   ch3 out.h265         H.265 1920×1080    60 帧
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

// M4：编码输出写进这个命名管道（FIFO），板子上的 gst-launch-1.0 从另一头读出来解码、显示到屏幕。
// 放 /tmp：FIFO 只是一个“通道”，不占磁盘空间；路径必须和 run_m4_on_board_with_mac.sh 里的 FIFO_PATH 一样
inline constexpr const char *kM4FifoPath = "/tmp/n0_mosaic.fifo";

// ===== M5：4 路不同帧率，按一个主时钟对齐拼图 =====

/** M5 的一路输入：码流 + 格式 + 帧率 + 人为停顿（测试用） */
struct AlignedChannelInput {
    const char *streamPath;   // 码流文件路径
    MppCodingType codingType; // 码流格式
    int framesPerSecond;      // 这一路的帧率，用来给每一帧算时间戳（裸码流里没有可靠的时间戳）
    int stallAtFrameIndex;    // 解到第几帧（从 0 开始）之前人为停顿；-1 = 不停
    int stallDurationMs;      // 停多久，单位毫秒
};

// 4 路内容都来自 aaa.264（约 60 秒），帧率不同：
//   ch0 30 fps 原文件：和主时钟同速，每一拍都换新帧
//   ch1 15 fps，902 帧（make_m5_inputs.sh 转出，隔一帧取一帧）：每帧显示两拍，另一拍是“重复”
//   ch2 60 fps，3606 帧（make_m5_inputs.sh 转出，每帧复制一次）：每一拍有两帧到点，只显示新的，旧的算“丢弃”
//   ch3 30 fps 原文件，解到第 300 帧时人为停 2 秒：停的时候格子不动（重复），恢复后积压的帧丢掉追上时钟
inline constexpr AlignedChannelInput kM5ChannelInputs[kChannelCount] = {
    {"/userdata/av/aaa.264", MPP_VIDEO_CodingAVC, 30, -1, 0},
    {"/userdata/av/n0_ch1_15fps.h264", MPP_VIDEO_CodingAVC, 15, -1, 0},
    {"/userdata/av/n0_ch2_60fps.h264", MPP_VIDEO_CodingAVC, 60, -1, 0},
    {"/userdata/av/aaa.264", MPP_VIDEO_CodingAVC, 30, 300, 2000},
};
inline constexpr int kM5MosaicFps = 30; // 拼图的节拍：每秒拼 30 张大图
inline constexpr const char *kM5OutputPath = "/userdata/av/mosaic_m5.h265";
