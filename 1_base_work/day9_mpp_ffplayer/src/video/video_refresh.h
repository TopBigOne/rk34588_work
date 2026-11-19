#pragma once

/**
 * @file
 * @brief 视频刷新：主线程每轮事件循环调用一次，决定显示 / 丢弃哪一帧，并打印状态
 */

#include "player/video_state.h"

/**
 * @brief 视频显示的核心调度
 * @param[in]     opaque          VideoState*
 * @param[in,out] remaining_time  距下次需要刷新的时间（秒），只会被改小
 * @note 根据主时钟和 frame_timer 判断当前帧是否该显示；落后太多时丢帧；同时显示字幕和更新 vidclk
 */
void video_refresh(void *opaque, double *remaining_time);
