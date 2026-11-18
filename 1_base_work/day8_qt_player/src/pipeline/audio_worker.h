#pragma once

#include <cstdint> // int16_t、int64_t
#include <functional>
#include <string>
#include <vector>

#include "audio/alsa_audio_output.h"
#include "audio/audio_resampler.h"
#include "audio/software_audio_decoder.h"
#include "common/ffmpeg_utils.h"
#include "pipeline/packet_queue.h"
#include "pipeline/playback_state.h"
#include "sync/media_clock.h"

/** 音频线程向外报告事情用的回调（在音频线程里被调用） */
struct AudioWorkerCallbacks {
    std::function<void(int serial)> onFinished;       // 这个 serial 的声音播完了
    std::function<void(const std::string &)> onError; // 出错了，参数是给人看的说明
};

/**
 * @brief 音频线程要做的事：取包 → 软解成 PCM → 转成 S16 交错 → 写进声卡 → 校准时钟
 * @note 声卡缓冲区满了 snd_pcm_writei 就阻塞，所以这个线程的节奏就是声卡的节奏。
 *       每写一次，用"刚写进去的最后一个采样的时间 − 声卡里还没播的时长"校准 MediaClock。
 *       声卡打不开时不播声音，但照样把包取走（否则解封装线程会卡住），时钟改由视频驱动
 */
class AudioWorker {
public:
    /**
     * @brief 记下要用的对象
     * @param[in] requestedAudioStream  音频流
     * @param[in] requestedPacketQueue  音频包队列
     * @param[in] requestedClock        播放时钟
     * @param[in] requestedState        共用状态
     * @param[in] requestedCallbacks    回调
     * @param[in] requestedDeviceName   ALSA 设备名，例如 "default"
     * @note 解码器和声卡在 run() 里打开
     */
    AudioWorker(const AVStream *requestedAudioStream, PacketQueue &requestedPacketQueue, MediaClock &requestedClock,
                PlaybackState &requestedState, AudioWorkerCallbacks requestedCallbacks, const char *requestedDeviceName);

    /**
     * @brief 线程函数：打开解码器和声卡，然后循环处理包，直到队列被叫停
     * @return 无
     * @note 在 std::thread 里运行
     */
    void run();

private:
    /**
     * @brief 声卡或解码器打不开时：不播声音，只把包取走，遇到结束标记照样报告"播完了"
     * @return 无
     * @note 同时把 hasAudioOutput 设成 false，让视频线程自己驱动时钟
     */
    void discardPacketsWithoutOutput();

    /**
     * @brief 开始新的 serial：（不是第一次时）清空解码器、转换器、声卡缓冲区，记下目标时间
     * @param[in] newSerial  新的 serial
     * @return 无
     * @note serial 0 不丢任何声音
     */
    void startNewSerial(int newSerial);

    /**
     * @brief 暂停着就让声卡暂停，并一直等到继续、seek 或停止
     * @return 无
     * @note 每 10 ms 看一次状态
     */
    void waitWhilePaused();

    /**
     * @brief 把解码器里现在能取的帧都取出来播放
     * @return 无
     * @note 取到 NeedMoreInput 或 EndOfStream 就返回
     */
    void playAvailableFrames();

    /**
     * @brief 播放一帧：丢掉 seek 目标之前的、转格式、写声卡、校准时钟
     * @param[in] decodedFrame  解码帧
     * @return 无
     * @note 写声卡可能阻塞（缓冲区满了）
     */
    void playFrame(const AVFrame *decodedFrame);

    /**
     * @brief 文件读完了：把解码器里剩下的播完，等声卡把缓冲区播空，报告"播完了"
     * @return 无
     * @note 等的时候照样响应暂停、seek、停止
     */
    void finishStream();

    /**
     * @brief 现在要不要放弃手上的活：要求停止了，或者有了新的 seek
     * @return true = 放弃
     * @note 和视频线程的同名函数一样
     */
    bool shouldAbandonCurrentWork() const;

    const AVStream *audioStream;        // 音频流
    PacketQueue &packetQueue;           // 音频包队列
    MediaClock &clock;                  // 播放时钟
    PlaybackState &state;               // 共用状态
    AudioWorkerCallbacks callbacks;     // 回调
    const char *deviceName;             // ALSA 设备名
    SoftwareAudioDecoder decoder;       // 软解
    AudioResampler resampler;           // 转成 S16 交错
    AlsaAudioOutput output;             // 声卡
    AvFramePtr decodedFramePtr;         // 解码帧，反复用
    std::vector<int16_t> outputSamples; // 转好的采样，反复用

    int currentSerial = -1;               // 正在处理的 serial；-1 = 还没开始
    int64_t seekTargetUs = kNoPtsUs;      // 早于它的声音丢掉
    int64_t nextExpectedPtsUs = kNoPtsUs; // 下一帧应该从哪个时间开始（帧上没有时间戳时用它）
    int64_t playedFrameCount = 0;         // 一共写进声卡多少帧（采样）
};
