/*
 * 本文件拆自 FFmpeg 6.0 fftools/ffplay.c（Copyright (c) 2003 Fabrice Bellard），
 * 遵循 GNU LGPL 2.1 或更高版本。
 */
#pragma once

/**
 * @file
 * @brief 全工程公共头：FFmpeg / SDL 头文件、调优常量、同步方式和显示模式枚举
 * @note 每个模块的 .c 都先包含它。常量全部保持 ffplay.c 原值，方便和原版对照
 */

#include "config.h"
#include "config_components.h"
#include <inttypes.h>
#include <math.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>

#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/eval.h"
#include "libavutil/mathematics.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "libavutil/dict.h"
#include "libavutil/fifo.h"
#include "libavutil/parseutils.h"
#include "libavutil/samplefmt.h"
#include "libavutil/time.h"
#include "libavutil/bprint.h"
#include "libavformat/avformat.h"
#include "libavdevice/avdevice.h"
#include "libswscale/swscale.h"
#include "libavutil/opt.h"
#include "libavcodec/avfft.h"
#include "libswresample/swresample.h"

#if CONFIG_AVFILTER
# include "libavfilter/avfilter.h"
# include "libavfilter/buffersink.h"
# include "libavfilter/buffersrc.h"
#endif

#include <SDL.h>
#include <SDL_thread.h>

#include "cmdutils.h"
#include "opt_common.h"

/* ---------------- 队列容量 ---------------- */

/** 三个 PacketQueue 合计最多缓存的字节数，超过就不再读包（infinite_buffer 除外） */
#define MAX_QUEUE_SIZE (15 * 1024 * 1024)
/** 每个流至少缓存这么多个包、且时长 > 1s 才算"够了" */
#define MIN_FRAMES 25
/** 外部时钟模式下，包数低于它就把外部时钟调慢 */
#define EXTERNAL_CLOCK_MIN_FRAMES 2
/** 外部时钟模式下，包数高于它就把外部时钟调快 */
#define EXTERNAL_CLOCK_MAX_FRAMES 10

/* ---------------- 音频输出 ---------------- */

/* Minimum SDL audio buffer size, in samples. */
#define SDL_AUDIO_MIN_BUFFER_SIZE 512
/* Calculate actual buffer size keeping in mind not cause too frequent audio callbacks */
#define SDL_AUDIO_MAX_CALLBACKS_PER_SEC 30

/* Step size for volume control in dB */
#define SDL_VOLUME_STEP (0.75)

/* ---------------- 音画同步 ---------------- */

/* no AV sync correction is done if below the minimum AV sync threshold */
#define AV_SYNC_THRESHOLD_MIN 0.04
/* AV sync correction is done if above the maximum AV sync threshold */
#define AV_SYNC_THRESHOLD_MAX 0.1
/* If a frame duration is longer than this, it will not be duplicated to compensate AV sync */
#define AV_SYNC_FRAMEDUP_THRESHOLD 0.1
/* no AV correction is done if too big error */
#define AV_NOSYNC_THRESHOLD 10.0

/* maximum audio speed change to get correct sync */
#define SAMPLE_CORRECTION_PERCENT_MAX 10

/* external clock speed adjustment constants for realtime sources based on buffer fullness */
#define EXTERNAL_CLOCK_SPEED_MIN  0.900
#define EXTERNAL_CLOCK_SPEED_MAX  1.010
#define EXTERNAL_CLOCK_SPEED_STEP 0.001

/* we use about AUDIO_DIFF_AVG_NB A-V differences to make the average */
#define AUDIO_DIFF_AVG_NB   20

/* ---------------- 显示 / 事件 ---------------- */

/* polls for possible required screen refresh at least this often, should be less than 1/fps */
#define REFRESH_RATE 0.01

/* NOTE: the size must be big enough to compensate the hardware audio buffersize size */
/* TODO: We assume that a decoded and resampled frame fits into this buffer */
#define SAMPLE_ARRAY_SIZE (8 * 65536)

/** 鼠标静止这么久（微秒）后隐藏光标 */
#define CURSOR_HIDE_DELAY 1000000

/** 字幕一次性整块渲染（1）还是逐个矩形渲染（0） */
#define USE_ONEPASS_SUBTITLE_RENDER 1

/** 自定义 SDL 事件：读线程出错 / 播放结束时投递，主线程收到后退出 */
#define FF_QUIT_EVENT    (SDL_USEREVENT + 2)

/**
 * @brief 音画同步以哪个时钟为主
 */
enum {
    AV_SYNC_AUDIO_MASTER,   /* default choice */
    AV_SYNC_VIDEO_MASTER,
    AV_SYNC_EXTERNAL_CLOCK, /* synchronize to an external clock */
};

/**
 * @brief 画面区域显示什么：视频 / 音频波形 / 音频频谱
 * @note 原版定义在 VideoState 内部，这里提出来，方便全局选项 show_mode 引用
 */
enum ShowMode {
    SHOW_MODE_NONE = -1, SHOW_MODE_VIDEO = 0, SHOW_MODE_WAVES, SHOW_MODE_RDFT, SHOW_MODE_NB
};
