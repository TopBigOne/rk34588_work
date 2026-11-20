// 按 3_one_year_work/0_跨阶段接口.md 第 3 节（N2 跳过，由 N3 自己定义；字段保持一致）
#pragma once

#include <cstdint>
#include <vector>

#include "common/video_codec.h"

struct StreamInfo {
    VideoCodec codec = VideoCodec::H264;
    int clockRateHz = 90000;
    std::vector<uint8_t> vps, sps, pps; // 不带起始码
    int width = 0;                      // 像素；接口文档里没有，N3 加的（libavformat 能直接给出）
    int height = 0;
};

struct EncodedFrame {
    std::vector<uint8_t> annexBData;  // Annex-B：每个 NAL 前面有起始码（3 或 4 字节）
    uint32_t rtpTimestamp = 0;        // extendedRtpTimestamp 的低 32 位
    int64_t extendedRtpTimestamp = 0; // 64 位、单调递增，time_base 1/90000
    bool isKeyFrame = false;          // 含 IDR（H.264 type 5；H.265 type 16～21）
    int64_t frameIndex = 0;           // 这次打开以来的第几帧
};
