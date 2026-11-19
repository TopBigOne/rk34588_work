#pragma once

/**
 * @file
 * @brief 进程内唯一的 SDL 窗口、渲染器、音频设备句柄
 * @note main() 创建窗口和渲染器，audio_open() 打开音频设备；渲染、事件、退出等模块共用
 */

#include "common/ffplay_common.h"

/* current context */
extern int is_full_screen;               ///< 当前是否全屏（-fs 也写它）

extern SDL_Window *window;               ///< 播放窗口；-nodisp 时为 NULL
extern SDL_Renderer *renderer;           ///< 窗口的渲染器
extern SDL_RendererInfo renderer_info;   ///< 渲染器能力，主要用 texture_formats 决定滤镜输出哪些像素格式
extern SDL_AudioDeviceID audio_dev;      ///< audio_open() 打开的音频设备
