//
// Created by DEV on 2026/11/9.
//
#pragma once
/**
 * @brief M1：单线程转码。aaa.264（1080p H.264）→ 解码 → RGA 缩到 720p → 编码 H.265 → out_720p.h265
 * @return 0 = 成功；1 = 任何一步失败（已打印原因）
 * @note 实现在 app/m1_transcode_single_thread.cpp
 */

int runM1TranscodeSingleThread();


