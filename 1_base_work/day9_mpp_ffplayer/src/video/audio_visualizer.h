#pragma once

/**
 * @file
 * @brief 音频可视化：没有视频或按 w 切换时，画波形（SHOW_MODE_WAVES）或频谱（SHOW_MODE_RDFT）
 */

#include "player/video_state.h"

/**
 * @brief 把 sample_array 里的采样画成波形或 RDFT 频谱
 */
void video_audio_display(VideoState* s);

/**
 * @brief 把刚输出的采样拷进环形缓冲 sample_array，供 video_audio_display 使用
 * @param[in] is            播放状态
 * @param[in] samples       S16 采样
 * @param[in] samples_size  字节数
 */
void update_sample_display(VideoState* is, short* samples, int samples_size);
