//
// Day 4 的三个里程碑，main() 按命令行参数 m1 / m2 / m3 调用其中一个
//
#pragma once

/**
 * @brief M1：从文件读一帧 1080p NV12，用 RGA 缩成 720p，写成 NV12 文件
 * @return 0 = 成功；1 = 失败（已打印原因）
 * @note 实现在 m1_resize_one_frame.cpp
 */
int run_m1_resize_one_frame();

/**
 * @brief M2：文件双码流。每帧原图 → 编码器 #1（1080p 主码流）；RGA 缩成 720p → 编码器 #2（子码流）
 * @return 0 = 成功；1 = 失败（已打印原因）
 * @note 实现在 m2_dual_stream_from_file.cpp
 */
int run_m2_dual_stream_from_file();

/**
 * @brief M3：摄像头双码流。和 M2 一样，只是输入换成摄像头的 buffer（零拷贝）
 * @return 0 = 成功；1 = 失败（已打印原因）
 * @note 实现在 m3_dual_stream_from_camera.cpp
 */
int run_m3_dual_stream_from_camera();
