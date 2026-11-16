//
// Created by DEV on 2026/11/16.
//
// M2 的入口函数，main.cpp 按参数 m2 调用
//
#pragma once

/**
 * @brief M2：双线程转码。解码线程把帧放进队列，缩放编码线程从队列取帧；输出 out_720p_m2.h265
 * @return 0 = 成功；1 = 任何一步失败（已打印原因）
 * @note 实现在 app/m2_transcode_two_threads.cpp。2026-11-17 上板：1803 帧，225.1 fps，md5 和 M1 的输出相同
 */
int runM2TranscodeTwoThreads();
