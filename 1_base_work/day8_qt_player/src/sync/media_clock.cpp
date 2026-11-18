#include "sync/media_clock.h"

#include "common/time_utils.h"

/**
 * @brief 重新设置时钟
 * @param[in] positionUs  新位置
 * @param[in] serial      新 serial
 * @return 无
 * @note lock_guard：构造时加锁，离开作用域时自动解锁，中途 return 也不会忘了解锁
 */
void MediaClock::reset(int64_t positionUs, int serial) {
    std::lock_guard<std::mutex> lock(mutex);
    basePositionUs = positionUs;
    baseSteadyTimeUs = getSteadyTimeUs();
    currentSerial = serial;
    isWaitingForFirstData = true;
}

/**
 * @brief 音频校准
 * @param[in] positionUs  位置
 * @param[in] serial      serial
 * @return 无
 * @note 旧 serial 的数据直接忽略
 */
void MediaClock::updateFromAudio(int64_t positionUs, int serial) {
    std::lock_guard<std::mutex> lock(mutex);
    if (serial != currentSerial) {
        return;
    }
    basePositionUs = positionUs;
    baseSteadyTimeUs = getSteadyTimeUs();
    isWaitingForFirstData = false;
}

/**
 * @brief 解除等待
 * @param[in] serial  serial
 * @return 无
 * @note 从停住的位置开始走：baseSteadyTimeUs 设成现在，位置不变
 */
void MediaClock::startIfWaiting(int serial) {
    std::lock_guard<std::mutex> lock(mutex);
    if (serial != currentSerial || !isWaitingForFirstData) {
        return;
    }
    baseSteadyTimeUs = getSteadyTimeUs();
    isWaitingForFirstData = false;
}

/**
 * @brief 暂停 / 继续
 * @param[in] shouldPause  true = 暂停
 * @return 无
 * @note 先用旧状态算出现在的位置存起来，再切换状态
 */
void MediaClock::setPaused(bool shouldPause) {
    std::lock_guard<std::mutex> lock(mutex);
    if (shouldPause == isPaused) {
        return;
    }
    basePositionUs = calculatePositionLocked();
    baseSteadyTimeUs = getSteadyTimeUs();
    isPaused = shouldPause;
}

/**
 * @brief 现在的位置
 * @return 微秒
 * @note 加锁后计算
 */
int64_t MediaClock::getPositionUs() const {
    std::lock_guard<std::mutex> lock(mutex);
    return calculatePositionLocked();
}

/**
 * @brief 按状态算位置
 * @return 微秒
 * @note 调用方已经加锁
 */
int64_t MediaClock::calculatePositionLocked() const {
    if (isPaused || isWaitingForFirstData) {
        return basePositionUs;
    }
    return basePositionUs + (getSteadyTimeUs() - baseSteadyTimeUs);
}
