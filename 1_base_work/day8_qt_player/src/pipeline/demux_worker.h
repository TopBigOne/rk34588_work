#pragma once

#include <functional>
#include <string>

#include "demux/mp4_demuxer.h"
#include "pipeline/packet_queue.h"
#include "pipeline/playback_state.h"
#include "sync/media_clock.h"

/**
 * @brief 解封装线程要做的事：读 mp4，把视频包、音频包分别放进两个队列；处理 seek
 * @note 读到文件末尾不退出：放一个"结束"标记进队列，然后等着（用户可能往回拖进度条）。
 *       只有 shouldStop 或者队列被叫停时才退出
 */
class DemuxWorker {
public:
    /**
     * @brief 记下要用的对象（都不归它所有，由 PlayerController 拥有，生命周期比线程长）
     * @param[in] requestedDemuxer     已经 open 好的 demuxer
     * @param[in] requestedVideoQueue  视频包队列；文件里没有视频时传 nullptr
     * @param[in] requestedAudioQueue  音频包队列；没有音频或不播声音时传 nullptr
     * @param[in] requestedClock       播放时钟，seek 时重设
     * @param[in] requestedState       共用的播放状态
     * @note 只是保存引用，不做别的事
     */
    DemuxWorker(Mp4Demuxer &requestedDemuxer, PacketQueue *requestedVideoQueue, PacketQueue *requestedAudioQueue,
                MediaClock &requestedClock, PlaybackState &requestedState);

    /**
     * @brief 线程函数：循环读包、分发、处理 seek，直到要求停止
     * @return 无
     * @note 在 std::thread 里运行
     */
    void run();

private:
    /**
     * @brief 处理一次 seek：跳转、serial 加 1、清空队列、重设时钟
     * @param[in] targetUs  目标时间，微秒
     * @return 无
     * @note 写 seekTargetUs 要在写 serial 之前（原因见 playback_state.h）
     */
    void performSeek(int64_t targetUs);

    /**
     * @brief 往两个队列里各放一个"结束"标记
     * @return true = 放好了；false = 队列被叫停
     * @note 带着当前 serial
     */
    bool pushEndOfStreamMarkers();

    Mp4Demuxer &demuxer;      // 拆封装
    PacketQueue *videoQueue;  // 视频包队列，可能是 nullptr
    PacketQueue *audioQueue;  // 音频包队列，可能是 nullptr
    MediaClock &clock;        // 播放时钟
    PlaybackState &state;     // 共用状态
    int videoStreamIndex;     // 视频流序号；-1 = 不要视频
    int audioStreamIndex;     // 音频流序号；-1 = 不要音频
};
