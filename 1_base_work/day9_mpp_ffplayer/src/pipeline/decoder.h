#pragma once

/**
 * @file
 * @brief 解码器封装：从 PacketQueue 取包，用 avcodec send/receive 解出 AVFrame / AVSubtitle
 * @note 音频、视频、字幕各一个 Decoder（auddec / viddec / subdec），各自跑在自己的线程里
 */

#include "common/ffplay_common.h"
#include "pipeline/frame_queue.h"
#include "pipeline/packet_queue.h"
#include "video/mpp_video_decoder.h"

typedef struct Decoder {
    AVPacket *pkt;              ///< 正在送给解码器的包
    PacketQueue *queue;         ///< 取包的队列
    AVCodecContext *avctx;
    int pkt_serial;             ///< 当前包的 serial
    int finished;               ///< 解码到 EOF 时等于 pkt_serial
    int packet_pending;         ///< 1 = pkt 上次 send 返回 EAGAIN，下次重送
    SDL_cond *empty_queue_cond; ///< 队列取空时通知读线程继续读
    int64_t start_pts;          ///< 流的起始 pts（音频用来推算无 pts 的帧）
    AVRational start_pts_tb;
    int64_t next_pts;           ///< 预测的下一帧 pts
    AVRational next_pts_tb;
    SDL_Thread *decoder_tid;
    /// MppPlayer 新增：不为 NULL 时视频用 MPP 硬解，avctx 只用来带流参数（没有 avcodec_open2）；
    /// 为 NULL 时（音频、字幕、MPP 不支持的视频格式）照 ffplay 原样走 avcodec
    MppVideoDecoder *mppDecoder;
} Decoder;

/**
 * @brief 初始化 Decoder（不启动线程）
 * @param[in] d                 解码器
 * @param[in] avctx             已打开的解码上下文，所有权交给 d
 * @param[in] queue             取包的队列
 * @param[in] empty_queue_cond  队列空时要 signal 的条件变量（is->continue_read_thread）
 * @return 0 = 成功；AVERROR(ENOMEM) = 失败
 */
int decoder_init(Decoder *d, AVCodecContext *avctx, PacketQueue *queue, SDL_cond *empty_queue_cond);

/**
 * @brief 解出一帧
 * @param[in]  d      解码器
 * @param[out] frame  音视频帧（字幕流传 NULL）
 * @param[out] sub    字幕（音视频流传 NULL）
 * @return 1 = 拿到一帧；0 = 解码结束（EOF）；-1 = 队列 abort
 * @note 遇到 serial 变化（seek）会 flush 解码器
 */
int decoder_decode_frame(Decoder *d, AVFrame *frame, AVSubtitle *sub);

/**
 * @brief 释放包和解码上下文（MppPlayer：还有 MPP 解码器的引用）
 */
void decoder_destroy(Decoder *d);

/**
 * @brief 停止解码线程：abort 包队列、唤醒帧队列、等线程退出、清空包队列
 * @param[in] d   解码器
 * @param[in] fq  解码器写入的帧队列
 */
void decoder_abort(Decoder *d, FrameQueue *fq);

/**
 * @brief 启动包队列并创建解码线程
 * @param[in] d            解码器
 * @param[in] fn           线程函数（audio_thread / video_thread / subtitle_thread）
 * @param[in] thread_name  线程名
 * @param[in] arg          线程参数（VideoState*）
 * @return 0 = 成功；AVERROR(ENOMEM) = 创建线程失败
 */
int decoder_start(Decoder *d, int (*fn)(void *), const char *thread_name, void* arg);
