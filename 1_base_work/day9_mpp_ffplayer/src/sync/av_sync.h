#pragma once

/**
 * @file
 * @brief 音画同步策略：选主时钟、算视频该等多久、算音频该多/少放几个采样
 */

#include "player/video_state.h"

/**
 * @brief 实际生效的主时钟类型
 * @return AV_SYNC_*；想以视频为主但没有视频流时退回音频，以此类推
 */
int get_master_sync_type(VideoState *is);

/**
 * @brief 读主时钟当前值（秒）
 */
double get_master_clock(VideoState *is);

/**
 * @brief 外部时钟模式下，按包队列水位微调外部时钟速度（实时流用）
 */
void check_external_clock_speed(VideoState *is);

/**
 * @brief 视频非主时钟时，根据和主时钟的差值修正"这一帧显示多久"
 * @param[in] delay  帧本身的时长（秒）
 * @param[in] is     播放状态
 * @return 修正后应等待的时间：视频落后 → 变短（甚至 0）；视频超前 → 变长
 */
double compute_target_delay(double delay, VideoState *is);

/**
 * @brief 音频非主时钟时，算出这一帧应该输出多少个采样（交给 swr 做补偿）
 * @param[in] is          播放状态
 * @param[in] nb_samples  这一帧实际的采样数
 * @return 期望的采样数，变化幅度不超过 SAMPLE_CORRECTION_PERCENT_MAX%
 */
int synchronize_audio(VideoState *is, int nb_samples);
