#pragma once

/**
 * @file
 * @brief 播放控制：seek、暂停、静音、音量、逐帧、全屏、显示模式、按章节跳转
 * @note 都在主线程（事件循环）里调用
 */

#include "player/video_state.h"

/* seek in the stream */
/**
 * @brief 发起 seek 请求（实际 seek 在读线程里做）
 * @param[in] is        播放状态
 * @param[in] pos       目标位置（微秒或字节）
 * @param[in] rel       相对偏移，用来确定 seek 的最小/最大范围
 * @param[in] by_bytes  1 = 按字节 seek
 */
void stream_seek(VideoState *is, int64_t pos, int64_t rel, int by_bytes);

/**
 * @brief 切换暂停状态，恢复时修正 frame_timer 和时钟
 */
void stream_toggle_pause(VideoState *is);

/**
 * @brief 暂停 / 继续，并退出逐帧模式
 */
void toggle_pause(VideoState *is);

/**
 * @brief 静音 / 取消静音
 */
void toggle_mute(VideoState *is);

/**
 * @brief 按分贝步长调音量
 * @param[in] is    播放状态
 * @param[in] sign  +1 = 增大；-1 = 减小
 * @param[in] step  步长（dB）
 */
void update_volume(VideoState *is, int sign, double step);

/**
 * @brief 逐帧：如在暂停就先恢复，显示下一帧后再暂停
 */
void step_to_next_frame(VideoState *is);

/**
 * @brief 全屏 / 窗口切换
 */
void toggle_full_screen(VideoState *is);

/**
 * @brief 循环切换显示模式：视频 → 波形 → 频谱
 */
void toggle_audio_display(VideoState *is);

/**
 * @brief 跳到上 / 下一章节
 * @param[in] is    播放状态
 * @param[in] incr  +1 = 下一章；-1 = 上一章
 */
void seek_chapter(VideoState *is, int incr);
