#pragma once

/**
 * @file
 * @brief 音频解码线程：auddec 解出 AVFrame → （可选）音频滤镜 → 放进 sampq
 */

#include "player/video_state.h"

/**
 * @brief 音频解码线程函数，由 decoder_start 创建
 * @param[in] arg  VideoState*
 * @return 0 = 正常退出；<0 = 出错
 * @note 输入格式变化时会重建音频滤镜图
 */
int audio_thread(void *arg);
