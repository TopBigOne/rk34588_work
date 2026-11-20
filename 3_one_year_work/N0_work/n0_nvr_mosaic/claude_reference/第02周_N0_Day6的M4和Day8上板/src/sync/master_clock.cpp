#include "sync/master_clock.h"

#include <ctime>    // clock_gettime、timespec
#include <unistd.h> // usleep

/**
 * @brief 开始计时
 * @return 无
 * @note 记下此刻的单调时钟读数，以后的位置都是“现在 - 这个读数”
 */
void MasterClock::start() {
    startMonotonicUs = getMonotonicNowUs();
    isClockStarted = true;
}

/**
 * @brief 现在的位置
 * @return 微秒数；没 start 时返回 0
 * @note 无
 */
int64_t MasterClock::getPositionUs() const {
    if (!isClockStarted) {
        return 0;
    }
    return getMonotonicNowUs() - startMonotonicUs;
}

/**
 * @brief 睡到目标位置
 * @param[in] targetPositionUs  目标位置，单位微秒
 * @return 无
 * @note 用循环而不是只睡一次：usleep 可能被信号提前叫醒，醒来要再算一次还差多少
 */
void MasterClock::sleepUntilPositionUs(int64_t targetPositionUs) const {
    if (!isClockStarted) {
        return;
    }
    while (true) {
        const int64_t remainingUs = targetPositionUs - getPositionUs(); // 正数 = 还早，要等
        if (remainingUs <= 0) {
            return;
        }
        usleep(static_cast<useconds_t>(remainingUs)); // usleep 的单位是微秒
    }
}

/**
 * @brief 读系统单调时钟
 * @return 开机以来的微秒数
 * @note timespec 有两个字段：tv_sec 秒、tv_nsec 纳秒（0～999999999），合起来才是完整的时间
 */
int64_t MasterClock::getMonotonicNowUs() {
    timespec now{};
    // CLOCK_MONOTONIC：只往前走的时钟；返回 0 = 成功（这个时钟在 Linux 上总是可用，不检查返回值）
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<int64_t>(now.tv_sec) * 1000000 + now.tv_nsec / 1000;
}
