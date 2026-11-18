/*
 * PacketQueue：解封装线程把压缩包传给视频线程 / 音频线程的队列（生产者-消费者）
 *
 *   解封装线程 ──push──→ [ 包 | 包 | 包 | ... ] ──pop──→ 视频线程（或音频线程）
 *
 * 有容量上限：满了 push 就等着（背压），空了 pop 就等着。
 * 每个包带着 serial（第几次 seek），消费者靠它认出 seek 之前的旧包
 */
#pragma once

#include <condition_variable>
#include <cstddef> // size_t
#include <deque>
#include <mutex>

#include "common/ffmpeg_utils.h"

/** 队列里的一项 */
struct QueuedPacket {
    AvPacketPtr packetPtr;      // 压缩包；isEndOfStream 为 true 时是空的
    int serial = 0;             // 这个包属于第几次 seek
    bool isEndOfStream = false; // true = 文件读完了，后面没有包了（直到下一次 seek）
};

class PacketQueue {
public:
    /**
     * @brief 构造一个空队列
     * @param[in] requestedCapacityPacketCount  最多排几个包；排满了 push 就会等
     * @note 视频一个包一帧（30 fps 时 1 秒 30 个），AAC 一个包 1024 个采样（48 kHz 时 1 秒约 47 个）
     */
    explicit PacketQueue(size_t requestedCapacityPacketCount) : capacityPacketCount(requestedCapacityPacketCount) {}

    // 禁止拷贝：里面有 mutex（不能拷贝），而且包只能有一个主人
    PacketQueue(const PacketQueue &) = delete;
    PacketQueue &operator=(const PacketQueue &) = delete;

    /**
     * @brief 放一个包进去；队列满了就等，直到有空位或者被叫停
     * @param[in] queuedPacket  要放进去的项，按值传入，所有权交给队列（调用方写 std::move）
     * @return true = 放进去了；false = 被叫停（abort），包没放进去，随参数一起释放
     * @note 会阻塞（背压）：视频、音频线程处理得慢时，解封装线程停在这里等
     */
    bool push(QueuedPacket queuedPacket);

    /**
     * @brief 取一个包；队列空了就等，直到有包或者被叫停
     * @param[out] queuedPacket  取到的项，所有权交给调用方；只有返回 true 时有效
     * @return true = 取到了；false = 被叫停，线程该退出了
     * @note 会阻塞
     */
    bool pop(QueuedPacket &queuedPacket);

    /**
     * @brief 扔掉队列里所有的包（seek 时调用），唤醒在 push 里等空位的线程
     * @return 无
     * @note 被扔掉的包在这里释放
     */
    void clear();

    /**
     * @brief 叫停：把在 push / pop 里等的线程都叫醒，让它们返回 false 退出
     * @return 无
     * @note 关闭播放器时调用；叫停以后队列不能再用
     */
    void abort();

private:
    const size_t capacityPacketCount;  // 最多排几个包
    std::mutex mutex;                  // 保护下面所有成员
    std::condition_variable notFull;   // "有空位了"：pop / clear 之后通知，push 在它上面等
    std::condition_variable notEmpty;  // "有包了"：push 之后通知，pop 在它上面等
    std::deque<QueuedPacket> packets;  // 排着的包，先进先出
    bool isAborted = false;            // 有人调用过 abort
};
