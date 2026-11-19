#pragma once

/**
 * @file
 * @brief 字幕解码线程：subdec 解出 AVSubtitle → 放进 subpq
 */

#include "player/video_state.h"

/**
 * @brief 字幕解码线程函数，由 decoder_start 创建
 * @param[in] arg  VideoState*
 * @return 0
 * @note 只处理位图字幕（format == 0），文本字幕被丢弃
 */
int subtitle_thread(void *arg);
