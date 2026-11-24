/*
 * M5 的主时钟：所有通道、所有格子都拿它的“现在几点”来判断一帧该不该显示
 */
#pragma once

#include <cstdint> // int64_t

/**
 * @brief 主时钟：start() 那一刻是 0，之后跟着系统的单调时钟往前走，单位微秒
 * @note 用 CLOCK_MONOTONIC：开机以来的时间，只往前走，不受“改系统时间”影响。
 *       普通播放器常用音频当主时钟；M5 没有音频，主时钟就是系统时间
 */
class MasterClock {
public:
    /**
     * @brief 开始计时：从这一刻起位置是 0
     * @return 无
     * @note 只在拼图线程里调用一次（4 路都拿到第一帧之后）
     */
    void start();

    /**
     * @brief 有没有调用过 start
     * @return true = 已经开始计时；false = 还没开始
     * @note 没开始时 getPositionUs 返回 0
     */
    bool isStarted() const { return isClockStarted; }

    /**
     * @brief 现在的位置：从 start 到现在过了多少微秒
     * @return 微秒数；没 start 时返回 0
     * @note 每次调用都重新读系统时钟，两次调用之间时间在走
     */
    int64_t getPositionUs() const;

    /**
     * @brief 睡到时钟走到 targetPositionUs 为止；已经过了就马上返回
     * @param[in] targetPositionUs  目标位置，单位微秒
     * @return 无
     * @note 这就是“早了就等”。没 start 时直接返回
     */
    void sleepUntilPositionUs(int64_t targetPositionUs) const;

private:
    /**
     * @brief 读系统单调时钟
     * @return 开机以来的微秒数
     * @note clock_gettime(CLOCK_MONOTONIC, ...) 的结果换算成微秒
     */
    static int64_t getMonotonicNowUs();

    bool isClockStarted = false; // 有没有 start 过
    int64_t startMonotonicUs = 0; // start 那一刻的单调时钟读数，单位微秒
};
