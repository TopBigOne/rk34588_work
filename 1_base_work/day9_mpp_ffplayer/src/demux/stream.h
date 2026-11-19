#pragma once

/**
 * @file
 * @brief 流的生命周期：创建 / 销毁 VideoState，打开 / 关闭单个音视频字幕流，切换流
 */

#include "player/video_state.h"

/**
 * @brief 创建 VideoState：初始化队列、时钟、音量，并启动读线程
 * @param[in] filename  输入文件 / URL
 * @param[in] iformat   强制的输入格式，可为 NULL
 * @return VideoState；失败返回 NULL
 */
VideoState *stream_open(const char *filename,
                        const AVInputFormat *iformat);

/**
 * @brief 停止读线程、关闭所有流、释放 VideoState
 */
void stream_close(VideoState *is);

/* open a given stream. Return 0 if OK */
/**
 * @brief 打开一个流：创建并打开解码器，音频还会打开声卡，然后启动对应解码线程
 * @param[in] is            播放状态
 * @param[in] stream_index  ic->streams 下标
 * @return 0 = 成功；<0 = 失败
 */
int stream_component_open(VideoState *is, int stream_index);

/**
 * @brief 关闭一个流：停解码线程、关声卡（音频）、释放资源
 */
void stream_component_close(VideoState *is, int stream_index);

/**
 * @brief 在当前节目里切换到下一个同类型的流（按 a / v / t / c 键）
 * @param[in] is          播放状态
 * @param[in] codec_type  AVMEDIA_TYPE_AUDIO / VIDEO / SUBTITLE
 */
void stream_cycle_channel(VideoState *is, int codec_type);
