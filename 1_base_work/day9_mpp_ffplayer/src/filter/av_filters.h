#pragma once

/**
 * @file
 * @brief libavfilter 滤镜图：视频（-vf、自动旋转、限定输出 SDL 支持的像素格式）和音频（-af、转成设备格式）
 * @note 只在 CONFIG_AVFILTER 时编译
 */

#include "player/video_state.h"

#if CONFIG_AVFILTER

/**
 * @brief 构建视频滤镜图：buffer → [-vf 用户滤镜] → [旋转] → format → buffersink
 * @param[in] graph     空滤镜图
 * @param[in] is        播放状态，结果写到 in_video_filter / out_video_filter
 * @param[in] vfilters  用户滤镜描述，可为 NULL
 * @param[in] frame     第一帧，用来确定输入尺寸和格式
 * @return 0 = 成功；<0 = 失败
 */
int configure_video_filters(AVFilterGraph *graph, VideoState *is, const char *vfilters, AVFrame *frame);

/**
 * @brief 构建音频滤镜图：abuffer → [-af 用户滤镜] → abuffersink
 * @param[in] is                  播放状态，结果写到 agraph / in_audio_filter / out_audio_filter
 * @param[in] afilters            用户滤镜描述，可为 NULL
 * @param[in] force_output_format 1 = 输出强制为 audio_tgt（设备）格式
 * @return 0 = 成功；<0 = 失败
 */
int configure_audio_filters(VideoState *is, const char *afilters, int force_output_format);

#endif  /* CONFIG_AVFILTER */
