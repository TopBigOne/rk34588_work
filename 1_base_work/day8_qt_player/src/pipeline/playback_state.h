/*
 * 几个线程共用的播放状态：停止、暂停、seek 请求、当前 serial、画面要缩放到多大
 *
 * 全部用 std::atomic：一个线程写、别的线程读，不用加锁也不会读到"写了一半"的值。
 * 写的顺序有讲究：seek 时先写 seekTargetUs，再写 serial。别的线程先读 serial、再读 seekTargetUs，
 * 只要看到了新的 serial，读到的 seekTargetUs 就一定是新的（atomic 默认的内存顺序 seq_cst 保证这一点）
 */
#pragma once

#include <atomic>
#include <cstdint> // int64_t

/** 播放状态，PlayerController 拥有，各个线程拿引用 */
struct PlaybackState {
    std::atomic<bool> shouldStop{false};             // 要关闭播放器了，所有线程尽快退出
    std::atomic<bool> isPaused{false};               // 暂停着

    std::atomic<bool> isSeekRequested{false};        // 界面发起了 seek，等解封装线程处理
    std::atomic<int64_t> requestedSeekTargetUs{0};   // 界面要跳到哪里，微秒

    std::atomic<int> serial{0};                      // 第几次 seek（0 = 刚打开文件）；解封装线程处理完 seek 后加 1
    std::atomic<int64_t> seekTargetUs{0};            // 当前 serial 的目标时间，微秒；早于它的帧和声音都丢掉

    std::atomic<bool> hasAudioOutput{false};         // 有声音在播（时钟由音频驱动）；没有音轨或声卡打不开时是 false
    std::atomic<int> volumePercent{100};             // 音量百分比 0～100（界面线程按正点原子桌面的音量写，音频线程读）

    std::atomic<int> videoBoxWidth{0};               // 画面区域有多宽，像素（界面告诉视频线程，RGA 按它缩放）
    std::atomic<int> videoBoxHeight{0};              // 画面区域有多高，像素
};
