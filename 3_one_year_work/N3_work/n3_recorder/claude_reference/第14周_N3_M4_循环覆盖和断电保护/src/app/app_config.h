#pragma once

#include <cstdint>

// 默认输入：板子上的裸流文件；也可以在命令行给 rtsp://（见 run_on_board_with_mac.sh 的用法）
inline constexpr const char *kDefaultInputUrl = "/userdata/av/aaa.264";
inline constexpr int kRawStreamFps = 30; // aaa.264 是 29.97 fps，按 30 造时间戳（计划 M1 验收写的 30/1）

inline constexpr const char *kRecordRootDir = "/userdata/n3_rec";
inline constexpr const char *kM1OutputPath = "/userdata/n3_rec/m1_ch0.mp4";
inline constexpr int kM1MaxDurationSeconds = 60; // RTSP 不会自己结束，录够这么久就停；文件输入读完也停

// StreamInfo 里给不出宽高时用（libavformat 一般能给出）
inline constexpr int kFallbackWidth = 1920;
inline constexpr int kFallbackHeight = 1080;

// M2：分段录像。aaa.264 只有 60 秒，循环读 10 遍凑 10 分钟（按码流时间算，读文件比实时快得多）
inline constexpr int kM2ChannelIndex = 0;
inline constexpr int kM2SegmentDurationSeconds = 60;
inline constexpr int kM2TotalDurationSeconds = 600;

// M3：录像索引，放在录像根目录下
inline constexpr const char *kRecordIndexFileName = "record_index.db";

// M4：循环覆盖 + fMP4。默认按实时速度录 1 小时，配额 200 MB（按 58 MB/分钟只能留 3 段左右）
inline constexpr int kM4TotalDurationSeconds = 3600;
inline constexpr int kM4QuotaMegabytes = 200;
inline constexpr int64_t kM4MinFreeBytes = 64LL * 1024 * 1024; // 分区至少留 64 MB（推断够用，板子上 df 看过再定）
