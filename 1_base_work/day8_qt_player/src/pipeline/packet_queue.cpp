#include "pipeline/packet_queue.h"

#include <utility> // std::move

/**
 * @brief 放一个包
 * @param[in] queuedPacket  要放的项
 * @return true = 放进去了；false = 被叫停
 * @note unique_lock：和 lock_guard 一样自动解锁，但可以交给 wait 临时解锁（wait 期间别的线程才能进来）。
 *       wait(锁, 条件)：条件不满足就睡；被唤醒后重新加锁再检查条件，满足了才返回（防"假唤醒"）
 */
bool PacketQueue::push(QueuedPacket queuedPacket) {
    std::unique_lock<std::mutex> lock(mutex);
    notFull.wait(lock, [this] { return isAborted || packets.size() < capacityPacketCount; });
    if (isAborted) {
        return false;
    }
    packets.push_back(std::move(queuedPacket));
    notEmpty.notify_one(); // 叫醒一个在 pop 里等的线程（每个队列只有一个消费者）
    return true;
}

/**
 * @brief 取一个包
 * @param[out] queuedPacket  取到的项
 * @return true = 取到；false = 被叫停
 * @note 取走一个就通知 notFull，在 push 里等空位的解封装线程可以接着放
 */
bool PacketQueue::pop(QueuedPacket &queuedPacket) {
    std::unique_lock<std::mutex> lock(mutex);
    notEmpty.wait(lock, [this] { return isAborted || !packets.empty(); });
    if (isAborted) {
        return false;
    }
    queuedPacket = std::move(packets.front());
    packets.pop_front();
    notFull.notify_one();
    return true;
}

/**
 * @brief 扔掉所有包
 * @return 无
 * @note deque::clear 会析构每一项，里面的 AvPacketPtr 自动 av_packet_free
 */
void PacketQueue::clear() {
    std::lock_guard<std::mutex> lock(mutex);
    packets.clear();
    notFull.notify_all();
}

/**
 * @brief 叫停
 * @return 无
 * @note notify_all：两边等着的线程全叫醒，它们看到 isAborted 就返回 false
 */
void PacketQueue::abort() {
    std::lock_guard<std::mutex> lock(mutex);
    isAborted = true;
    notFull.notify_all();
    notEmpty.notify_all();
}
