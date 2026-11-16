//
// Created by DEV on 2026/11/9.
//
#pragma once

#include <cstddef> // size_t

// inline constexpr：常量写在头文件里，多个 .cpp include 也只有一份

// 输入：Day 2 用过的 H.264 裸码流，1920×1080，1803 帧（2026-11-08 Mac 上 ffprobe）
inline constexpr const char *kInputH264StreamPath = "/userdata/av/aaa.264";

// 输出：H.265 裸码流
inline constexpr const char *kOutputH265StreamPath = "/userdata/av/out_720p.h265";
inline constexpr const char *kM2OutputH265StreamPath = "/userdata/av/out_720p_m2.h265";

// 输出画面的尺寸，单位像素
inline constexpr int kOutputWidth = 1280;
inline constexpr int kOutputHeight = 720;

// 输出码流的编码参数
inline constexpr int kOutputBps = 2 * 1000 * 1000; // 目标码率，单位 bit/s（2 Mbps）
inline constexpr int kOutputFps = 30; // 帧率：aaa.264 约 30 fps（ffprobe 报 3003/100）
inline constexpr int kOutputGop = 60; // 每 60 帧一个 I 帧（30 fps 下 2 秒）

// M2：解码线程和缩放编码线程之间的队列最多排几帧。
// 每排着一帧就占着解码器内存池的一块 buffer（池子一共 24 块，还要留给解码器当参考帧），所以要比 24 小很多；
// 太小（比如 1）两个线程又几乎是轮流干活。2026-11-17 上板实测最多排满过 4 帧
inline constexpr size_t kFrameQueueCapacity = 4;
