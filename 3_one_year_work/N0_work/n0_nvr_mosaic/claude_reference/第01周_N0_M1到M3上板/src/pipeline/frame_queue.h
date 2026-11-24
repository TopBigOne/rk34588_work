//
// Created by DEV on 2026/11/15.
//
// FrameQueue：两个线程之间传解码帧的队列（生产者-消费者）
//
//   解码线程 ──push──→ [ 帧 | 帧 | 帧 | 帧 ] ──pop──→ 缩放编码线程
//
// 有容量上限：满了 push 就等着（背压），空了 pop 就等着。
// 队列里放的是 MppFramePtr：帧的所有权跟着走，谁手里拿着谁负责释放，不会漏、不会重复
//
#pragma once

#include <condition_variable>
#include <cstddef> // size_t
#include <deque>
#include <mutex>

#include "common/mpp_utils.h" // MppFramePtr

/** pop() 的结果 */
enum class PopResult {
    Frame,       // 取到一帧
    EndOfStream, // 生产者调用过 closeInput（不会再放帧了），而且队列也空了
    Aborted,     // 有一方出错调用了 abort，叫停整个流程
};

class FrameQueue {
public:
    /**
     * @brief 构造一个空队列
     * @param[in] requestedCapacityFrameCount  最多排几帧；排满了 push 就会等
     * @note 每排着一帧，就占着解码器内存池里的一块 buffer（池子一共 24 块），所以容量要比 24 小很多
     */
    explicit FrameQueue(size_t requestedCapacityFrameCount) : capacityFrameCount(requestedCapacityFrameCount) {}

    // 禁止拷贝：里面有 mutex（不能拷贝），而且帧只能有一个主人
    FrameQueue(const FrameQueue &) = delete;
    FrameQueue &operator=(const FrameQueue &) = delete;

    /**
     * @brief 生产者放一帧进去；队列满了就等，直到有空位或者被叫停
     * @param[in] framePtr  要放进去的帧，按值传入，所有权交给队列（调用方要写 std::move）
     * @return true = 放进去了；false = 流程被叫停（abort），帧没放进去，函数返回时随参数一起释放
     * @note 会阻塞：这就是"背压"，解码比编码快时，解码线程停在这里等
     */
    bool push(MppFramePtr framePtr);

    /**
     * @brief 消费者取一帧；队列空了就等，直到有帧、或者生产者说"没有了"、或者被叫停
     * @param[out] framePtr  取到的帧，所有权交给调用方；只有返回 PopResult::Frame 时才有效
     * @return PopResult::Frame = 取到一帧；EndOfStream = 帧都取完了；Aborted = 被叫停
     * @note 会阻塞
     */
    PopResult pop(MppFramePtr &framePtr);

    /**
     * @brief 生产者说"不会再 push 了"
     * @return 无
     * @note 消费者把队列里剩下的帧取完以后，pop 才返回 EndOfStream
     */
    void closeInput();

    /**
     * @brief 叫停：任何一方出错时调用，把正在 push / pop 里等的线程都叫醒，让两个线程都尽快退出
     * @return 无
     * @note 叫停以后队列里剩下的帧不取了，在队列析构时随 deque 一起释放
     */
    void abort();

    /**
     * @brief 队列里最多同时排过几帧
     * @return 帧数；等于容量说明队列满过，解码线程被背压挡住过
     * @note 两个线程都在改队列，读的时候也要加锁
     */
    size_t getMaxQueuedFrameCount() const;

private:
    const size_t capacityFrameCount;  // 最多排几帧
    mutable std::mutex mutex;         // 保护下面所有成员；mutable：const 函数里也要能加锁
    std::condition_variable notFull;  // "有空位了"：pop 取走一帧后通知，push 在它上面等
    std::condition_variable notEmpty; // "有帧了"：push 放进一帧后通知，pop 在它上面等
    std::deque<MppFramePtr> frames;   // 排着的帧，先进先出
    bool isInputClosed = false;       // 生产者调用过 closeInput
    bool isAborted = false;           // 有人调用过 abort
    size_t maxQueuedFrameCount = 0;   // 最多同时排过几帧
};
