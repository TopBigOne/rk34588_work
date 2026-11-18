#include "audio/alsa_audio_output.h"

#include "common/log.h"

static const char *const kTag = "AlsaAudioOutput";

/**
 * @brief 打开声卡
 * @param[in] deviceName             设备名
 * @param[in] requestedSampleRate    采样率
 * @param[in] requestedChannelCount  声道数
 * @param[in] bufferDurationUs       缓冲区时长
 * @return true = 成功；false = 失败
 * @note 第 1 步 snd_pcm_open；第 2 步 snd_pcm_set_params 一次设好格式、采样率、声道、缓冲区；
 *       第 3 步 查声卡支不支持暂停
 */
bool AlsaAudioOutput::open(const char *deviceName, int requestedSampleRate, int requestedChannelCount,
                           int bufferDurationUs) {
    if (pcmPtr) {
        LOGE(kTag, "open 只能调用一次");
        return false;
    }

    // 第 1 步 snd_pcm_open(&设备, 设备名, 方向, 模式)：
    //   SND_PCM_STREAM_PLAYBACK = 播放（另一个方向 CAPTURE 是录音，Day 3 录麦克风用过）；
    //   模式 0 = 阻塞模式：缓冲区满了 writei 等着，不立刻返回 -EAGAIN
    snd_pcm_t *rawPcm = nullptr;
    int ret = snd_pcm_open(&rawPcm, deviceName, SND_PCM_STREAM_PLAYBACK, 0);
    if (ret < 0) {
        // snd_strerror：把 ALSA 的负数错误码换成文字
        LOGE(kTag, "snd_pcm_open %s 失败：%s", deviceName, snd_strerror(ret));
        return false;
    }
    pcmPtr.reset(rawPcm);

    // 第 2 步 snd_pcm_set_params(设备, 采样格式, 访问方式, 声道数, 采样率, 允许软件重采样, 缓冲区时长 us)：
    //   SND_PCM_FORMAT_S16_LE：16 位有符号整数、小端（和 AudioResampler 的输出一致）；
    //   SND_PCM_ACCESS_RW_INTERLEAVED：用 snd_pcm_writei 写交错数据（i = interleaved）；
    //   允许软件重采样 = 1：声卡不支持 48000 时，ALSA 的 plug 层帮我们转（"default" 设备一般带 plug 层）
    ret = snd_pcm_set_params(pcmPtr.get(), SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                             static_cast<unsigned int>(requestedChannelCount),
                             static_cast<unsigned int>(requestedSampleRate), 1,
                             static_cast<unsigned int>(bufferDurationUs));
    if (ret < 0) {
        LOGE(kTag, "snd_pcm_set_params 失败：%s", snd_strerror(ret));
        return false;
    }
    sampleRate = requestedSampleRate;
    channelCount = requestedChannelCount;

    // 第 3 步 查硬件参数：缓冲区实际多大、支不支持暂停。
    // snd_pcm_hw_params_alloca：在栈上申请一个参数结构体（函数返回自动释放，不用 free）
    snd_pcm_hw_params_t *hardwareParams = nullptr;
    snd_pcm_hw_params_alloca(&hardwareParams);
    snd_pcm_uframes_t bufferFrameCount = 0;
    if (snd_pcm_hw_params_current(pcmPtr.get(), hardwareParams) == 0) {
        canPause = snd_pcm_hw_params_can_pause(hardwareParams) == 1;
        snd_pcm_hw_params_get_buffer_size(hardwareParams, &bufferFrameCount);
    }
    LOGI(kTag, "打开 %s：s16 %d Hz %d 声道，缓冲区 %lu 帧（%.1f ms），%s暂停", deviceName, sampleRate, channelCount,
         static_cast<unsigned long>(bufferFrameCount), bufferFrameCount * 1000.0 / sampleRate,
         canPause ? "支持" : "不支持");
    return true;
}

/**
 * @brief 写一段 PCM
 * @param[in] samples     采样
 * @param[in] frameCount  帧数
 * @return true = 写完；false = 出错
 * @note snd_pcm_writei 可能只写进去一部分（被信号打断等），所以循环写到全部写完
 */
bool AlsaAudioOutput::write(const int16_t *samples, int frameCount) {
    int writtenFrameCount = 0;
    while (writtenFrameCount < frameCount) {
        // snd_pcm_writei(设备, 数据, 帧数)：返回实际写进去的帧数；缓冲区满了就阻塞到有空位
        const snd_pcm_sframes_t ret =
                snd_pcm_writei(pcmPtr.get(), samples + static_cast<size_t>(writtenFrameCount) * channelCount,
                               static_cast<snd_pcm_uframes_t>(frameCount - writtenFrameCount));
        if (ret >= 0) {
            writtenFrameCount += static_cast<int>(ret);
            continue;
        }
        // 出错：-EPIPE = underrun（缓冲区被播空了，比如刚暂停完、刚 seek 完、或者 CPU 太忙）；
        // -ESTRPIPE = 系统挂起过。snd_pcm_recover 处理这两种情况（内部调用 snd_pcm_prepare），
        // 最后一个参数 1 = 不打印 ALSA 自己的提示
        const int recoverRet = snd_pcm_recover(pcmPtr.get(), static_cast<int>(ret), 1);
        if (recoverRet < 0) {
            LOGE(kTag, "snd_pcm_writei 失败：%s", snd_strerror(static_cast<int>(ret)));
            return false;
        }
    }
    return true;
}

/**
 * @brief 还有多少没播
 * @return 微秒
 * @note snd_pcm_delay 在 underrun 状态下返回 -EPIPE，这时缓冲区已经空了，按 0 算
 */
int64_t AlsaAudioOutput::getQueuedDurationUs() {
    snd_pcm_sframes_t delayFrameCount = 0;
    if (snd_pcm_delay(pcmPtr.get(), &delayFrameCount) < 0 || delayFrameCount < 0) {
        return 0;
    }
    return static_cast<int64_t>(delayFrameCount) * 1000000 / sampleRate;
}

/**
 * @brief 暂停 / 继续
 * @param[in] shouldPause  true = 暂停
 * @return 无
 * @note snd_pcm_state 查设备状态：RUNNING = 正在播；PAUSED = 暂停着；PREPARED = 准备好了还没开始；
 *       XRUN = underrun 了。snd_pcm_pause 只在 RUNNING / PAUSED 状态下有效
 */
void AlsaAudioOutput::setPaused(bool shouldPause) {
    if (shouldPause == isPaused) {
        return;
    }
    isPaused = shouldPause;
    const snd_pcm_state_t state = snd_pcm_state(pcmPtr.get());
    if (shouldPause) {
        if (canPause && state == SND_PCM_STATE_RUNNING) {
            snd_pcm_pause(pcmPtr.get(), 1); // 1 = 暂停
        } else {
            snd_pcm_drop(pcmPtr.get());
        }
        return;
    }
    if (state == SND_PCM_STATE_PAUSED) {
        snd_pcm_pause(pcmPtr.get(), 0); // 0 = 继续
    } else if (state != SND_PCM_STATE_PREPARED && state != SND_PCM_STATE_RUNNING) {
        snd_pcm_prepare(pcmPtr.get());
    }
}

/**
 * @brief 丢掉没播的声音
 * @return 无
 * @note 暂停状态下也可以调用，之后仍然是暂停（isPaused 不变），继续时会 prepare
 */
void AlsaAudioOutput::dropQueued() {
    snd_pcm_drop(pcmPtr.get());
    snd_pcm_prepare(pcmPtr.get());
}
