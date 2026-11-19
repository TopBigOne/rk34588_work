#pragma once

/**
 * @file
 * @brief 读线程（解复用）：打开输入、选流、循环 av_read_frame 把包分发到三个 PacketQueue，处理 seek / 暂停 / 循环播放
 */

#include "player/video_state.h"

/* this thread gets the stream from the disk or the network */
/**
 * @brief 读线程函数，由 stream_open 创建
 * @param[in] arg  VideoState*
 * @return 0
 * @note 出错时投递 FF_QUIT_EVENT 让主线程退出
 */
int read_thread(void *arg);
