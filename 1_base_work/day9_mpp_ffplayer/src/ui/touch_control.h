#pragma once

/**
 * @file
 * @brief MppPlayer 新增：触摸屏操作。板子上没有键盘，ffplay 原来的按键（q 退出、空格暂停、方向键 seek）用不了
 * @note 只认“轻点”（按下到抬起手指移动不超过屏幕的 5%、时间不超过 600 ms），按点的位置分三块：
 *
 *         ┌──────────────────────┬─────┐
 *         │                      │退出 │  右上角：宽的右 15%、高的上 10%
 *         │                      └─────┤
 *         │      暂停 / 继续              │  中间：其余地方
 *         │                             │
 *         ├─────────────────────────────┤
 *         │  跳到这个位置（按横坐标比例）    │  底部：高的下 15%，点在屏幕横向的 30% 处就跳到 30% 处
 *         └─────────────────────────────┘
 *
 *       坐标用 SDL 触摸事件里的归一化坐标（0～1，相对窗口），和屏幕分辨率、横竖屏无关
 */

#include "player/video_state.h"

/**
 * @brief 处理一个 SDL 触摸事件
 * @param[in] is     播放器状态
 * @param[in] event  SDL 事件；只处理 SDL_FINGERDOWN / SDL_FINGERUP，别的类型直接返回 0
 * @return 1 = 用户点了退出，调用方要 do_exit；0 = 其他情况
 * @note 在主线程（event_loop）里调用。暂停、seek 直接调用 player_control / stream_seek
 */
int handleTouchEvent(VideoState *is, const SDL_Event *event);
