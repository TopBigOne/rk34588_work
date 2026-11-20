// 来源：3_one_year_work/N0_work/n0_nvr_mosaic/claude_reference/第02周_N0_Day6的M4和Day8上板/src/pipeline/frame_queue.cpp
#include "pipeline/frame_queue.h"

#include <utility> // std::move

/**
 * @brief 放一帧进去，满了就等
 * @param[in] framePtr  要放进去的帧，所有权交给队列
 * @return true = 放进去了；false = 被叫停，帧没放进去
 * @note wait(锁, 条件) 等价于下面这个循环：
 *         while (!条件) {
 *             notFull.wait(lock); // 解锁 → 睡着 → 被叫醒 → 重新加锁 → 回到 while 再检查一次
 *         }
 *       条件一进来就成立：不睡，直接往下走。只有队列满了、又没被叫停时才真的停住。
 *       必须带条件：线程可能被"无缘无故"叫醒（虚假唤醒），醒来要再检查一次
 */
bool FrameQueue::push(MppFramePtr framePtr) {
    std::unique_lock<std::mutex> lock(mutex);
    notFull.wait(lock, [this]() { return frames.size() < capacityFrameCount || isAborted; });
    if (isAborted) {
        return false;
    }
    frames.push_back(std::move(framePtr));
    if (frames.size() > maxQueuedFrameCount) {
        maxQueuedFrameCount = frames.size();
    }
    lock.unlock();         // 先解锁再通知：被叫醒的线程马上就能拿到锁
    notEmpty.notify_one(); // 叫醒一个在 pop 里等帧的线程（只有一个消费者）
    return true;
}

/**
 * @brief 取一帧，空了就等
 * @param[out] framePtr  取到的帧，所有权交给调用方
 * @return Frame = 取到一帧；EndOfStream = 生产者已 closeInput 并且帧取完了；Aborted = 被叫停
 * @note 生产者 closeInput 以后，队列里剩下的帧照样取，取完才返回 EndOfStream
 */
PopResult FrameQueue::pop(MppFramePtr &framePtr) {
    std::unique_lock<std::mutex> lock(mutex);
    notEmpty.wait(lock, [this]() { return !frames.empty() || isInputClosed || isAborted; });
    if (isAborted) {
        return PopResult::Aborted;
    }
    if (frames.empty()) {
        return PopResult::EndOfStream; // 走到这里说明 isInputClosed，而且没有帧了
    }
    framePtr = std::move(frames.front());
    frames.pop_front();
    lock.unlock();
    notFull.notify_one(); // 叫醒在 push 里等空位的生产者
    return PopResult::Frame;
}

/**
 * @brief 不阻塞地取一帧，队首到点了才取
 * @param[in]  nowUs     主时钟现在的位置，单位微秒
 * @param[out] framePtr  取到的帧
 * @return Frame / NotDue / Empty / EndOfStream / Aborted
 * @note 只“看”队首：帧是按时间戳从小到大排进来的，队首没到点，后面的更不会到点
 */
TryPopResult FrameQueue::tryPopIfDue(int64_t nowUs, MppFramePtr &framePtr) {
    std::unique_lock<std::mutex> lock(mutex); // 不 wait，只是为了安全地读写 frames
    if (isAborted) {
        return TryPopResult::Aborted;
    }
    if (frames.empty()) {
        return isInputClosed ? TryPopResult::EndOfStream : TryPopResult::Empty;
    }
    const int64_t frontPtsUs = mpp_frame_get_pts(frames.front().get());
    if (frontPtsUs > nowUs) {
        return TryPopResult::NotDue;
    }
    framePtr = std::move(frames.front());
    frames.pop_front(); // 和 pop 一样：move 只转走了所有权，格子要删掉
    lock.unlock();
    notFull.notify_one(); // 腾出了一个空位，叫醒可能在 push 里等的解码线程
    return TryPopResult::Frame;
}

/**
 * @brief 生产者说"不会再 push 了"
 * @return 无
 * @note 改完状态要 notify：消费者可能正睡在 pop 里，不叫醒它就永远等下去
 */
void FrameQueue::closeInput() {
    {
        std::lock_guard<std::mutex> lock(mutex); // lock_guard：只加锁、离开大括号解锁，不能中途解锁
        isInputClosed = true;
    }
    notEmpty.notify_all();
}

/**
 * @brief 叫停，叫醒所有在等的线程
 * @return 无
 * @note 两个条件变量都要 notify：生产者可能在 push 里等空位，消费者可能在 pop 里等帧
 */
void FrameQueue::abort() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        isAborted = true;
    }
    notEmpty.notify_all();
    notFull.notify_all();
}

/**
 * @brief 最多同时排过几帧
 * @return 帧数
 * @note 两个线程都可能在用队列，读成员也要加锁
 */
size_t FrameQueue::getMaxQueuedFrameCount() const {
    std::lock_guard<std::mutex> lock(mutex);
    return maxQueuedFrameCount;
}
