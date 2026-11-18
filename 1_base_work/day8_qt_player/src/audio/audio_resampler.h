#pragma once

#include <cstdint> // int16_t、uint64_t
#include <vector>

#include "common/ffmpeg_utils.h"

/**
 * @brief 把解码器给的 PCM 转成 ALSA 要的格式：S16（16 位有符号整数）、交错（左右左右……）、固定采样率和声道数
 * @note 解码器输出什么格式由解码器决定（AAC 是 float planar），采样率、声道数跟着文件走（44100、48000、单声道……）。
 *       ALSA 这边我们只打开一种固定格式，所以中间用 libswresample 统一转一下：
 *       采样格式（float → int16）、排列方式（planar → 交错）、采样率（44100 → 48000）、声道（单声道 → 双声道）
 *       都可以转。输入格式在第一帧到来时才知道，所以在 convert 里按需（重新）配置
 */
class AudioResampler {
public:
    /**
     * @brief 构造，记下输出格式
     * @param[in] requestedOutputSampleRate    输出采样率，Hz，例如 48000
     * @param[in] requestedOutputChannelCount  输出声道数，1 或 2
     * @note 输出采样格式固定是 S16 交错
     */
    AudioResampler(int requestedOutputSampleRate, int requestedOutputChannelCount);

    AudioResampler(const AudioResampler &) = delete;            // 不能复制：SwrContext 只能有一个主人
    AudioResampler &operator=(const AudioResampler &) = delete; // 同上

    /**
     * @brief 转换一帧
     * @param[in]  decodedFrame   解码器给的帧
     * @param[out] outputSamples  转好的 S16 交错采样，覆盖写入。元素个数 = 输出帧数 × 声道数
     * @return 输出了多少"帧"（一帧 = 每个声道各一个采样）；< 0 = 出错
     * @note 采样率不变时一进一出帧数相同；采样率变了时，转换器内部会缓存一点数据，帧数不完全对应
     */
    int convert(const AVFrame *decodedFrame, std::vector<int16_t> &outputSamples);

    /**
     * @brief 丢掉转换器内部缓存的数据（seek 时调用）
     * @return 无
     * @note 直接释放转换器，下一帧来时重新配置，最简单也不会留旧数据
     */
    void reset();

private:
    /**
     * @brief 按输入帧的格式（重新）配置转换器
     * @param[in] decodedFrame  输入帧，读它的采样格式、采样率、声道布局
     * @return true = 成功；false = 失败
     * @note 只在第一次、或者输入格式变了的时候调用
     */
    bool configureForInput(const AVFrame *decodedFrame);

    const int outputSampleRate;   // 输出采样率，Hz
    const int outputChannelCount; // 输出声道数
    SwrContextPtr swrContextPtr;  // 转换器；还没配置时是空的
    int inputSampleFormat = -1;   // 当前配置的输入采样格式（AVSampleFormat）
    int inputSampleRate = 0;      // 当前配置的输入采样率
    uint64_t inputChannelLayout = 0; // 当前配置的输入声道布局（位掩码：AV_CH_FRONT_LEFT | AV_CH_FRONT_RIGHT ……）
};
