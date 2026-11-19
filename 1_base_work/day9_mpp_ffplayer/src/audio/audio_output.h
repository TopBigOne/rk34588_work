#pragma once

/**
 * @file
 * @brief 音频输出：打开 SDL 音频设备；SDL 回调里从 sampq 取帧、重采样、调音量、填给声卡，并更新音频时钟
 */

#include "player/video_state.h"

/**
 * @brief 最近一次 SDL 音频回调的时间（av_gettime_relative，微秒）
 * @note 回调线程写，主线程的波形显示读（估算声卡里还剩多少没播）
 */
extern int64_t audio_callback_time;

/**
 * @brief 从 sampq 取一帧，必要时重采样到 audio_tgt 格式，结果放在 is->audio_buf
 * @return 输出数据的字节数；暂停或出错时返回 -1
 * @note 同时更新 is->audio_clock 为这帧播完时的 pts
 */
int audio_decode_frame(VideoState *is);

/**
 * @brief SDL 音频回调：把 len 字节填进 stream
 * @param[in]  opaque  VideoState*
 * @param[out] stream  声卡要的缓冲区
 * @param[in]  len     需要的字节数
 * @note 跑在 SDL 的音频线程。填完后用"已解码到的位置 - 声卡缓冲里还没播的部分"更新 audclk
 */
void sdl_audio_callback(void *opaque, Uint8 *stream, int len);

/**
 * @brief 打开 SDL 音频设备，打不开时逐步降低声道数 / 采样率重试
 * @param[in]  opaque                VideoState*，作为回调的 userdata
 * @param[in]  wanted_channel_layout 期望的声道布局
 * @param[in]  wanted_sample_rate    期望的采样率
 * @param[out] audio_hw_params       实际得到的设备参数（固定 S16）
 * @return 设备缓冲区字节数；<0 = 失败
 */
int audio_open(void *opaque, AVChannelLayout *wanted_channel_layout, int wanted_sample_rate, struct AudioParams *audio_hw_params);
