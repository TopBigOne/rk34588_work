/*
 * 时间相关的小工具
 */
#pragma once

#include <chrono>  // steady_clock
#include <cstdint> // int64_t
#include <thread>  // std::this_thread::sleep_for

/**
 * @brief 取"单调时钟"的当前时间，单位微秒
 * @return 从某个固定起点（一般是开机）到现在的微秒数
 * @note steady_clock 只会往前走，不受改系统时间、网络对时的影响，适合算"过了多久"。
 *       不能拿它当年月日用
 */
inline int64_t getSteadyTimeUs() {
    const auto sinceEpoch = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(sinceEpoch).count();
}

/**
 * @brief 让当前线程睡一会儿
 * @param[in] durationUs  睡多少微秒；≤ 0 时不睡直接返回
 * @return 无
 * @note 实际睡的时间会比要求的长一点（线程被唤醒要排队），Linux 上一般多几十到一百多微秒（推断，未实测）
 */
inline void sleepForUs(int64_t durationUs) {
    if (durationUs > 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(durationUs));
    }
}
