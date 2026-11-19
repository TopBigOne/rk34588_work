#pragma once

/**
 * @file
 * @brief 主线程事件循环：等 SDL 事件的同时定时调用 video_refresh；处理键盘、鼠标、窗口事件；退出清理
 */

#include "player/video_state.h"

/**
 * @brief 释放所有资源并 exit(0)
 * @param[in] is  播放状态，可为 NULL
 */
void do_exit(VideoState *is);

/**
 * @brief SIGINT / SIGTERM 处理：exit(123)
 */
void sigterm_handler(int sig);

/* handle an event sent by the GUI */
/**
 * @brief 主事件循环，永不返回（退出走 do_exit）
 * @param[in] cur_stream  播放状态
 */
void event_loop(VideoState *cur_stream);
