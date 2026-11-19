#pragma once

/**
 * @file
 * @brief 压缩包队列：读线程往里放 AVPacket，解码线程从里取
 * @note 线程安全（mutex + cond）。serial 是"代"的概念：每次 flush（seek）serial+1，
 *       解码线程据此丢弃 seek 之前的旧包、旧帧
 */

#include "common/ffplay_common.h"

typedef struct MyAVPacketList {
    AVPacket *pkt;
    int serial;           ///< 入队时队列的 serial
} MyAVPacketList;

typedef struct PacketQueue {
    AVFifo *pkt_list;     ///< 存 MyAVPacketList 的 FIFO
    int nb_packets;       ///< 包个数
    int size;             ///< 总字节数（含 MyAVPacketList 自身）
    int64_t duration;     ///< 总时长（流的 time_base）
    int abort_request;    ///< 1 = 退出，所有阻塞 get 立即返回 -1
    int serial;           ///< 当前代号，flush 时 +1
    SDL_mutex *mutex;
    SDL_cond *cond;
} PacketQueue;

/**
 * @brief 放入一个包（转移 pkt 的引用，调用后 pkt 被清空）
 * @param[in] q    队列
 * @param[in] pkt  要放的包
 * @return 0 = 成功；<0 = 队列已 abort 或内存不足（此时 pkt 被 unref）
 */
int packet_queue_put(PacketQueue *q, AVPacket *pkt);

/**
 * @brief 放入一个空包，通知解码器"流结束了，把缓存的帧吐出来"
 * @param[in] q             队列
 * @param[in] pkt           临时包，用来承载空数据
 * @param[in] stream_index  空包所属流
 * @return 同 packet_queue_put
 */
int packet_queue_put_nullpacket(PacketQueue *q, AVPacket *pkt, int stream_index);

/**
 * @brief 初始化队列（初始状态为 abort，需要 packet_queue_start 才能用）
 * @return 0 = 成功；AVERROR(ENOMEM) = 失败
 */
int packet_queue_init(PacketQueue *q);

/**
 * @brief 清空所有包，serial+1（seek 时调用）
 */
void packet_queue_flush(PacketQueue *q);

/**
 * @brief 清空并释放队列
 */
void packet_queue_destroy(PacketQueue *q);

/**
 * @brief 标记 abort 并唤醒所有等待者
 */
void packet_queue_abort(PacketQueue *q);

/**
 * @brief 取消 abort 状态，serial+1，开始可用
 */
void packet_queue_start(PacketQueue *q);

/**
 * @brief 取一个包
 * @param[in]  q       队列
 * @param[out] pkt     取到的包
 * @param[in]  block   1 = 队列空时阻塞等待
 * @param[out] serial  包的 serial，可为 NULL
 * @return 1 = 取到；0 = 非阻塞且队列空；-1 = 队列已 abort
 */
int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block, int *serial);
