#include "pipeline/audio_worker.h"

#include <utility> // std::move

#include "audio/audio_output_format.h"
#include "common/log.h"
#include "common/time_utils.h"

static const char *const kTag = "AudioWorker";

/**
 * @brief 记下要用的对象
 * @param[in] requestedAudioStream  音频流
 * @param[in] requestedPacketQueue  音频包队列
 * @param[in] requestedClock        时钟
 * @param[in] requestedState        状态
 * @param[in] requestedCallbacks    回调
 * @param[in] requestedDeviceName   ALSA 设备名
 * @note resampler 的输出格式固定是 audio_output_format.h 里的 48000 Hz 双声道
 */
AudioWorker::AudioWorker(const AVStream *requestedAudioStream, PacketQueue &requestedPacketQueue,
                         MediaClock &requestedClock, PlaybackState &requestedState,
                         AudioWorkerCallbacks requestedCallbacks, const char *requestedDeviceName) :
    audioStream(requestedAudioStream), packetQueue(requestedPacketQueue), clock(requestedClock),
    state(requestedState), callbacks(std::move(requestedCallbacks)), deviceName(requestedDeviceName),
    resampler(kOutputSampleRate, kOutputChannelCount) {}

/**
 * @brief 线程函数
 * @return 无
 * @note 第 1 步 打开解码器和声卡（失败就只丢包）；第 2 步 循环取包：丢旧 serial 的包、新 serial 先清空、
 *       暂停就等、结束标记就收尾、普通包就解码播放
 */
void AudioWorker::run() {
    LOGI(kTag, "开始，设备 %s", deviceName);

    // 第 1 步 打开
    if (!decoder.init(audioStream) ||
        !output.open(deviceName, kOutputSampleRate, kOutputChannelCount, kAlsaBufferDurationUs)) {
        callbacks.onError(std::string("声音打不开（ALSA 设备 ") + deviceName + "），只播画面");
        discardPacketsWithoutOutput();
        return;
    }

    // 第 2 步 循环取包
    QueuedPacket queuedPacket;
    while (packetQueue.pop(queuedPacket)) {
        if (queuedPacket.serial != state.serial) {
            continue; // seek 之前的旧包
        }
        if (queuedPacket.serial != currentSerial) {
            startNewSerial(queuedPacket.serial);
        }
        waitWhilePaused();
        if (shouldAbandonCurrentWork()) {
            continue; // 等暂停的时候有了新 seek（或者要停止了），这个包作废
        }
        if (queuedPacket.isEndOfStream) {
            finishStream();
            continue;
        }
        if (decoder.sendPacket(queuedPacket.packetPtr.get())) {
            playAvailableFrames();
        }
    }
    LOGI(kTag, "退出：一共播了 %.3f 秒的声音", static_cast<double>(playedFrameCount) / kOutputSampleRate);
}

/**
 * @brief 不播声音，只取走包
 * @return 无
 * @note 先把 hasAudioOutput 改成 false 并让时钟开始走，视频才不会一直等音频
 */
void AudioWorker::discardPacketsWithoutOutput() {
    state.hasAudioOutput = false;
    clock.startIfWaiting(state.serial);
    QueuedPacket discardedPacket;
    while (packetQueue.pop(discardedPacket)) {
        if (discardedPacket.isEndOfStream && discardedPacket.serial == state.serial) {
            callbacks.onFinished(discardedPacket.serial);
        }
    }
}

/**
 * @brief 开始新的 serial
 * @param[in] newSerial  新 serial
 * @return 无
 * @note 声卡缓冲区里还有约 100 ms 旧位置的声音，dropQueued 立刻扔掉，否则 seek 后会先听到一小段旧声音
 */
void AudioWorker::startNewSerial(int newSerial) {
    if (currentSerial != -1) {
        decoder.flush();
        resampler.reset();
        output.dropQueued();
    }
    currentSerial = newSerial;
    seekTargetUs = newSerial == 0 ? kNoPtsUs : state.seekTargetUs.load();
    nextExpectedPtsUs = kNoPtsUs;
}

/**
 * @brief 暂停着就等
 * @return 无
 * @note 暂停期间时钟由 PlayerController 停住了，这里只管声卡
 */
void AudioWorker::waitWhilePaused() {
    if (!state.isPaused) {
        return;
    }
    output.setPaused(true);
    while (state.isPaused && !shouldAbandonCurrentWork()) {
        sleepForUs(10 * 1000);
    }
    output.setPaused(false);
}

/**
 * @brief 取出能取的帧播放
 * @return 无
 * @note receiveFrame 出错时也直接返回，丢掉这一包，不影响后面
 */
void AudioWorker::playAvailableFrames() {
    while (!shouldAbandonCurrentWork()) {
        const ReceiveFrameResult receiveResult = decoder.receiveFrame(decodedFramePtr);
        if (receiveResult != ReceiveFrameResult::Frame) {
            return;
        }
        playFrame(decodedFramePtr.get());
    }
}

/**
 * @brief 播放一帧
 * @param[in] decodedFrame  解码帧
 * @return 无
 * @note 第 1 步 算这一帧的时间；第 2 步 seek 目标之前的整帧丢掉；第 3 步 转格式；第 4 步 写声卡；
 *       第 5 步 校准时钟：喇叭里正在响的 = 刚写进去的末尾 − 声卡里还没播的
 */
void AudioWorker::playFrame(const AVFrame *decodedFrame) {
    // 第 1 步 best_effort_timestamp：解码器给的"最可信"的显示时间（一般就是包的 pts）
    int64_t ptsUs = convertToMicroseconds(decodedFrame->best_effort_timestamp, audioStream->time_base);
    if (ptsUs == kNoPtsUs) {
        ptsUs = nextExpectedPtsUs; // 没有时间戳就接着上一帧往后排
    }
    const int64_t frameDurationUs =
            static_cast<int64_t>(decodedFrame->nb_samples) * 1000000 / decodedFrame->sample_rate;

    // 第 2 步 seek 之后，从关键帧附近开始读的音频包，结束时间还没到目标的整帧丢掉
    if (seekTargetUs != kNoPtsUs && ptsUs != kNoPtsUs && ptsUs + frameDurationUs <= seekTargetUs) {
        return;
    }

    // 第 3 步 转成 S16 交错 48000 Hz 双声道
    const int outputFrameCount = resampler.convert(decodedFrame, outputSamples);
    if (outputFrameCount <= 0) {
        return;
    }

    // 第 4 步 写声卡；缓冲区满了这里会阻塞，线程就被声卡牵着走
    if (!output.write(outputSamples.data(), outputFrameCount)) {
        return;
    }
    playedFrameCount += outputFrameCount;

    // 第 5 步 校准时钟
    if (ptsUs != kNoPtsUs) {
        const int64_t endPtsUs = ptsUs + static_cast<int64_t>(outputFrameCount) * 1000000 / kOutputSampleRate;
        nextExpectedPtsUs = endPtsUs;
        clock.updateFromAudio(endPtsUs - output.getQueuedDurationUs(), currentSerial);
    }
}

/**
 * @brief 文件读完了的收尾
 * @return 无
 * @note 第 1 步 送 nullptr，取出解码器剩下的帧；第 2 步 等声卡播空；第 3 步 时钟交给系统时间、报告播完；
 *       第 4 步 flush 解码器，下次 seek 后还能接着用
 */
void AudioWorker::finishStream() {
    // 第 1 步 送"没有了"，解码器把缓存吐出来
    if (decoder.sendPacket(nullptr)) {
        playAvailableFrames();
    }

    // 第 2 步 等声卡把缓冲区里的播完（最多约 100 ms）；中途暂停就等，seek / 停止就放弃
    while (!shouldAbandonCurrentWork() && output.getQueuedDurationUs() > 0) {
        waitWhilePaused();
        sleepForUs(10 * 1000);
    }
    if (shouldAbandonCurrentWork()) {
        decoder.flush();
        return;
    }

    // 第 3 步 声音没了，时钟不能再停在"等数据"上（视频可能比音频长），交给系统时间往前走
    clock.startIfWaiting(currentSerial);
    LOGI(kTag, "声音播完");
    callbacks.onFinished(currentSerial);

    // 第 4 步 送过 nullptr 的解码器处于"结束"状态，flush 之后才能再送包
    decoder.flush();
}

/**
 * @brief 要不要放弃手上的活
 * @return true = 放弃
 * @note 要求停止，或者 serial 变了
 */
bool AudioWorker::shouldAbandonCurrentWork() const { return state.shouldStop || state.serial != currentSerial; }
