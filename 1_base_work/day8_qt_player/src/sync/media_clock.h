#pragma once

#include <cstdint> // int64_t
#include <mutex>

/**
 * @brief 播放时钟：现在"播到哪里了"，单位微秒。视频线程按它决定一帧什么时候显示
 * @note 平时由音频线程每写一次声卡就校准一次（音频时钟）；两次校准之间，按系统时间往前推。
 *       没有音轨时，第一帧画面显示以后就按系统时间往前走。
 *
 *       三种"停住"的情况，时钟都不往前走：
 *       - 暂停
 *       - 刚 seek 完、还在等新位置的第一个数据（防止解码新位置的那段时间里时钟已经跑远了，画面全被判成"晚了"丢掉）
 *       - 刚打开文件、还没有数据
 *
 *       serial（第几次 seek）：每次 seek 加 1。音频线程校准时带上自己处理的数据的 serial，
 *       和时钟当前的对不上（是 seek 之前的旧数据），就不理它
 */
class MediaClock {
public:
    /**
     * @brief 重新设置时钟（打开文件、seek 时调用），设完处于"等第一个数据"的状态，不往前走
     * @param[in] positionUs  新的位置，微秒
     * @param[in] serial      新的 serial
     * @return 无
     * @note 暂停状态保持不变
     */
    void reset(int64_t positionUs, int serial);

    /**
     * @brief 音频线程校准时钟：现在喇叭里响的是 positionUs 这个时刻的声音
     * @param[in] positionUs  位置，微秒
     * @param[in] serial      这段声音属于第几次 seek；和当前 serial 不一样就忽略
     * @return 无
     * @note 校准完，"等第一个数据"的状态解除，时钟开始往前走（暂停时除外）
     */
    void updateFromAudio(int64_t positionUs, int serial);

    /**
     * @brief 解除"等第一个数据"的状态，从当前位置开始往前走
     * @param[in] serial  调用方处理的数据属于第几次 seek；和当前 serial 不一样就忽略
     * @return 无
     * @note 没有音轨（或音轨已经播完）时，由视频线程在显示第一帧以后调用
     */
    void startIfWaiting(int serial);

    /**
     * @brief 暂停 / 继续
     * @param[in] shouldPause  true = 暂停
     * @return 无
     * @note 暂停时把当前位置记下来，继续时从这个位置接着走
     */
    void setPaused(bool shouldPause);

    /**
     * @brief 现在播到哪里了
     * @return 微秒
     * @note 线程安全，任何线程都可以调用
     */
    int64_t getPositionUs() const;

private:
    /**
     * @brief 按当前状态算出位置（调用方已经加锁）
     * @return 微秒
     * @note 停住时返回 basePositionUs；在走时返回 basePositionUs + 从 baseSteadyTimeUs 到现在过了多久
     */
    int64_t calculatePositionLocked() const;

    mutable std::mutex mutex;          // 保护下面所有成员（音频线程写，视频线程和界面线程读）
    int64_t basePositionUs = 0;        // 最近一次校准时的位置，微秒
    int64_t baseSteadyTimeUs = 0;      // 最近一次校准时的系统时间（steady_clock），微秒
    int currentSerial = 0;             // 当前 serial
    bool isPaused = false;             // 暂停着
    bool isWaitingForFirstData = true; // 刚 reset，还在等第一个数据
};
