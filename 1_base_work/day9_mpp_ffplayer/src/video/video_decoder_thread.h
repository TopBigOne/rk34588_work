#pragma once

/**
 * @file
 * @brief 视频解码线程：viddec 解出 AVFrame → 早丢帧判断 → （可选）视频滤镜 → 放进 pictq
 */

#include "player/video_state.h"

/**
 * @brief 视频解码线程函数，由 decoder_start 创建
 * @param[in] arg  VideoState*
 * @return 0
 * @note 帧尺寸 / 格式变化或按 w 切换滤镜时会重建视频滤镜图
 */
int video_thread(void *arg);
