/**
 * @file
 * @brief 触摸屏操作的实现，区域划分见 touch_control.h
 */
#include "ui/touch_control.h"

#include "common/player_options.h" // seek_by_bytes
#include "player/player_control.h"

/** 手指移动超过窗口的这个比例（横、纵分别算），就不算“轻点”（是在滑动） */
static const float kTapMaxMoveRatio = 0.05f;
/** 按下到抬起超过这么久，就不算“轻点”（是长按） */
static const Uint32 kTapMaxDurationMs = 600;
/** 右上角“退出”区域：横坐标大于它（右 15%） */
static const float kExitAreaMinX = 0.85f;
/** 右上角“退出”区域：纵坐标小于它（上 10%） */
static const float kExitAreaMaxY = 0.10f;
/** 底部“跳转”区域：纵坐标大于它（下 15%） */
static const float kSeekAreaMinY = 0.85f;

/** 按下时的位置和时间，抬起时拿来判断是不是轻点。只跟踪第一根手指 */
static SDL_FingerID g_trackedFingerId;
static int g_isTrackingFinger = 0;
static float g_fingerDownX;
static float g_fingerDownY;
static Uint32 g_fingerDownTimestampMs;

/**
 * @brief 跳到整个片子的某个比例处
 * @param[in] is             播放器状态
 * @param[in] targetRatio    目标位置占总时长的比例，0～1
 * @return 无
 * @note 和 ffplay 鼠标右键点窗口跳转的算法一样（event_loop.c 里 SDL_MOUSEBUTTONDOWN 那段）：
 *       目标时间 = 比例 × 总时长 + 文件起始时间；总时长未知时（直播流等）按文件字节数的比例跳
 */
static void seekToDurationRatio(VideoState *is, double targetRatio) {
    if (targetRatio < 0.0) {
        targetRatio = 0.0;
    }
    if (targetRatio > 1.0) {
        targetRatio = 1.0;
    }
    if (seek_by_bytes || is->ic->duration <= 0) {
        // avio_size：文件总字节数；最后一个参数 1 = 按字节 seek
        const int64_t fileSizeBytes = avio_size(is->ic->pb);
        stream_seek(is, (int64_t)(fileSizeBytes * targetRatio), 0, 1);
        return;
    }
    // ic->duration 的单位是 AV_TIME_BASE（微秒）
    int64_t targetTimestampUs = (int64_t)(targetRatio * is->ic->duration);
    if (is->ic->start_time != AV_NOPTS_VALUE) {
        targetTimestampUs += is->ic->start_time;
    }
    av_log(NULL, AV_LOG_INFO, "触摸跳转到 %.0f%%（%.1f 秒）\n", targetRatio * 100.0, targetTimestampUs / 1000000.0);
    stream_seek(is, targetTimestampUs, 0, 0);
}

int handleTouchEvent(VideoState *is, const SDL_Event *event) {
    // SDL_TouchFingerEvent：x、y 是相对窗口的归一化坐标（0～1）；fingerId 区分同时按下的几根手指；
    // timestamp 是 SDL 初始化以来的毫秒数
    const SDL_TouchFingerEvent *fingerEvent = &event->tfinger;

    // 第 1 步 按下：记住位置和时间（已经有一根手指在跟踪时，其他手指不管）
    if (event->type == SDL_FINGERDOWN) {
        if (!g_isTrackingFinger) {
            g_isTrackingFinger = 1;
            g_trackedFingerId = fingerEvent->fingerId;
            g_fingerDownX = fingerEvent->x;
            g_fingerDownY = fingerEvent->y;
            g_fingerDownTimestampMs = fingerEvent->timestamp;
        }
        return 0;
    }
    if (event->type != SDL_FINGERUP || !g_isTrackingFinger || fingerEvent->fingerId != g_trackedFingerId) {
        return 0;
    }

    // 第 2 步 抬起：移动距离、按住时间都小，才算轻点
    g_isTrackingFinger = 0;
    const int isTap = SDL_fabsf(fingerEvent->x - g_fingerDownX) <= kTapMaxMoveRatio
                      && SDL_fabsf(fingerEvent->y - g_fingerDownY) <= kTapMaxMoveRatio
                      && fingerEvent->timestamp - g_fingerDownTimestampMs <= kTapMaxDurationMs;
    if (!isTap) {
        return 0;
    }
    // 窗口还没按视频大小建好之前（read_thread 还在打开文件），和按键一样不处理
    if (!is->width) {
        return 0;
    }

    // 第 3 步 按位置分区：退出 / 跳转 / 暂停
    if (fingerEvent->x >= kExitAreaMinX && fingerEvent->y <= kExitAreaMaxY) {
        return 1;
    }
    if (fingerEvent->y >= kSeekAreaMinY) {
        seekToDurationRatio(is, fingerEvent->x);
        return 0;
    }
    toggle_pause(is);
    return 0;
}
