#pragma once

/**
 * @file
 * @brief 解码后帧的环形队列：解码线程写，显示 / 音频回调读
 * @note 视频、字幕、音频各一个（pictq / subpq / sampq）。
 *       keep_last = 1 时，读过的最后一帧保留在队列里（rindex_shown），用于暂停重画、计算帧时长
 */

#include "common/ffplay_common.h"
#include "pipeline/packet_queue.h"

#define VIDEO_PICTURE_QUEUE_SIZE 3
#define SUBPICTURE_QUEUE_SIZE    16
#define SAMPLE_QUEUE_SIZE        9
#define FRAME_QUEUE_SIZE         FFMAX(SAMPLE_QUEUE_SIZE, FFMAX(VIDEO_PICTURE_QUEUE_SIZE, SUBPICTURE_QUEUE_SIZE))

/* Common struct for handling all types of decoded data and allocated render buffers. */
typedef struct Frame {
    AVFrame* frame;
    AVSubtitle sub;
    int serial;
    double pts; /* presentation timestamp for the frame */
    double duration; /* estimated duration of the frame */
    int64_t pos; /* byte position of the frame in the input file */
    int width;
    int height;
    int format;
    AVRational sar;
    int uploaded; ///< 1 = 已上传到 SDL 纹理（暂停时不用重复上传）
    int flip_v; ///< 1 = 需要上下翻转（负 linesize）
} Frame;

typedef struct FrameQueue {
    Frame queue[FRAME_QUEUE_SIZE];
    int rindex; ///< 读位置
    int windex; ///< 写位置
    int size; ///< 当前帧数
    int max_size; ///< 容量
    int keep_last; ///< 1 = 保留最后显示的一帧
    int rindex_shown; ///< 1 = rindex 处的帧已经显示过
    SDL_mutex* mutex;
    SDL_cond* cond;
    PacketQueue* pktq; ///< 对应的包队列，用它的 abort_request 判断退出
} FrameQueue;

/**
 * @brief 初始化帧队列
 * @param[in] f          队列
 * @param[in] pktq       对应的包队列
 * @param[in] max_size   容量（不超过 FRAME_QUEUE_SIZE）
 * @param[in] keep_last  是否保留最后显示的一帧
 * @return 0 = 成功；AVERROR(ENOMEM) = 失败
 */
int frame_queue_init(FrameQueue* f, PacketQueue* pktq, int max_size, int keep_last);

/**
 * @brief 释放所有帧和锁（函数名沿用原版拼写 destory）
 */
void frame_queue_destory(FrameQueue* f);

/**
 * @brief 唤醒等待该队列的线程（退出时用）
 */
void frame_queue_signal(FrameQueue* f);

/** @brief 当前要显示的帧（rindex + rindex_shown），不移动读指针 */
Frame* frame_queue_peek(FrameQueue* f);

/** @brief 当前帧的下一帧 */
Frame* frame_queue_peek_next(FrameQueue* f);

/** @brief 上一次显示的帧（rindex 处） */
Frame* frame_queue_peek_last(FrameQueue* f);

/**
 * @brief 等到有空位，返回可写的帧槽位（解码线程用）
 * @return 帧槽位；队列 abort 时返回 NULL
 */
Frame* frame_queue_peek_writable(FrameQueue* f);

/**
 * @brief 等到有可读帧，返回它（音频回调用）
 * @return 帧；队列 abort 时返回 NULL
 */
Frame* frame_queue_peek_readable(FrameQueue* f);

/** @brief 写完 peek_writable 拿到的槽位后提交，windex 前进 */
void frame_queue_push(FrameQueue* f);

/** @brief 读完当前帧后前进，释放旧帧 */
void frame_queue_next(FrameQueue* f);

/** @brief 还没显示的帧数 */
int frame_queue_nb_remaining(FrameQueue* f);

/**
 * @brief 最后显示那帧在文件中的字节位置（按字节 seek 时用）
 * @return 字节位置；没有或已过期时返回 -1
 */
int64_t frame_queue_last_pos(FrameQueue* f);
