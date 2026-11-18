#include "audio/audio_resampler.h"

#include "common/log.h"

static const char *const kTag = "AudioResampler";

/**
 * @brief 构造
 * @param[in] requestedOutputSampleRate    输出采样率
 * @param[in] requestedOutputChannelCount  输出声道数
 * @note 转换器等第一帧来了再建
 */
AudioResampler::AudioResampler(int requestedOutputSampleRate, int requestedOutputChannelCount) :
    outputSampleRate(requestedOutputSampleRate), outputChannelCount(requestedOutputChannelCount) {}

/**
 * @brief 取一帧的声道布局
 * @param[in] decodedFrame  解码帧
 * @return 声道布局位掩码
 * @note 有的文件不写声道布局（channel_layout = 0），只知道声道数，就按声道数取默认布局
 *       （1 → 单声道，2 → 左右立体声）
 */
static uint64_t getFrameChannelLayout(const AVFrame *decodedFrame) {
    if (decodedFrame->channel_layout != 0) {
        return decodedFrame->channel_layout;
    }
    return static_cast<uint64_t>(av_get_default_channel_layout(decodedFrame->channels));
}

/**
 * @brief 按输入帧配置转换器
 * @param[in] decodedFrame  输入帧
 * @return true = 成功；false = 失败
 * @note 第 1 步 swr_alloc_set_opts 填输出、输入参数；第 2 步 swr_init
 */
bool AudioResampler::configureForInput(const AVFrame *decodedFrame) {
    const uint64_t frameChannelLayout = getFrameChannelLayout(decodedFrame);
    const uint64_t outputChannelLayout =
            static_cast<uint64_t>(av_get_default_channel_layout(outputChannelCount));

    // 第 1 步 swr_alloc_set_opts(已有的上下文(传 nullptr 新建), 输出布局, 输出格式, 输出采样率,
    //                            输入布局, 输入格式, 输入采样率, 日志偏移, 日志上下文)
    SwrContext *rawSwrContext = swr_alloc_set_opts(
            nullptr, static_cast<int64_t>(outputChannelLayout), AV_SAMPLE_FMT_S16, outputSampleRate,
            static_cast<int64_t>(frameChannelLayout), static_cast<AVSampleFormat>(decodedFrame->format),
            decodedFrame->sample_rate, 0, nullptr);
    if (!rawSwrContext) {
        LOGE(kTag, "swr_alloc_set_opts 失败");
        return false;
    }
    swrContextPtr.reset(rawSwrContext);

    // 第 2 步 swr_init：按上面的参数准备好转换（要不要重采样、要不要混声道）
    const int ret = swr_init(swrContextPtr.get());
    if (ret < 0) {
        LOGE(kTag, "swr_init 失败：%s", convertFfmpegErrorToString(ret).c_str());
        swrContextPtr.reset();
        return false;
    }
    inputSampleFormat = decodedFrame->format;
    inputSampleRate = decodedFrame->sample_rate;
    inputChannelLayout = frameChannelLayout;
    LOGI(kTag, "输入 %s %d Hz %d 声道 → 输出 s16 %d Hz %d 声道",
         av_get_sample_fmt_name(static_cast<AVSampleFormat>(decodedFrame->format)), decodedFrame->sample_rate,
         decodedFrame->channels, outputSampleRate, outputChannelCount);
    return true;
}

/**
 * @brief 转换一帧
 * @param[in]  decodedFrame   输入帧
 * @param[out] outputSamples  输出采样
 * @return 输出帧数；< 0 = 出错
 * @note 第 1 步 输入格式变了就重新配置；第 2 步 算输出最多多少帧，准备空间；第 3 步 swr_convert
 */
int AudioResampler::convert(const AVFrame *decodedFrame, std::vector<int16_t> &outputSamples) {
    // 第 1 步 第一次、或者输入格式变了（极少见），重新配置
    if (!swrContextPtr || decodedFrame->format != inputSampleFormat || decodedFrame->sample_rate != inputSampleRate ||
        getFrameChannelLayout(decodedFrame) != inputChannelLayout) {
        if (!configureForInput(decodedFrame)) {
            return -1;
        }
    }

    // 第 2 步 swr_get_out_samples(上下文, 输入帧数)：这次最多能输出多少帧（算上内部缓存的），按这个准备空间
    const int maxOutputFrameCount = swr_get_out_samples(swrContextPtr.get(), decodedFrame->nb_samples);
    if (maxOutputFrameCount < 0) {
        LOGE(kTag, "swr_get_out_samples 失败：%s", convertFfmpegErrorToString(maxOutputFrameCount).c_str());
        return -1;
    }
    outputSamples.resize(static_cast<size_t>(maxOutputFrameCount) * outputChannelCount);

    // 第 3 步 swr_convert(上下文, 输出平面数组, 输出最多几帧, 输入平面数组, 输入几帧)：
    //   交错格式只有一个"平面"，所以输出数组只有一个元素；
    //   输入用 extended_data：planar 格式时每个声道一个平面（AAC 双声道就是 2 个），交错格式时只有 1 个
    uint8_t *outputPlanes[1] = {reinterpret_cast<uint8_t *>(outputSamples.data())};
    const int outputFrameCount =
            swr_convert(swrContextPtr.get(), outputPlanes, maxOutputFrameCount,
                        const_cast<const uint8_t **>(decodedFrame->extended_data), decodedFrame->nb_samples);
    if (outputFrameCount < 0) {
        LOGE(kTag, "swr_convert 失败：%s", convertFfmpegErrorToString(outputFrameCount).c_str());
        return -1;
    }
    outputSamples.resize(static_cast<size_t>(outputFrameCount) * outputChannelCount);
    return outputFrameCount;
}

/**
 * @brief 丢掉内部缓存
 * @return 无
 * @note 释放转换器，下一帧来时 convert 发现是空的，会重新配置
 */
void AudioResampler::reset() { swrContextPtr.reset(); }
