#pragma once

#include "common/ffmpeg_utils.h"

/** receiveFrame() 的结果 */
enum class ReceiveFrameResult {
    Frame,         // 取到一帧 PCM
    NeedMoreInput, // 送进去的包都解完了，要再送包才有
    EndOfStream,   // 送过"结束"（sendPacket(nullptr)）以后，剩下的帧都取完了
    Error,         // 出错
};

/**
 * @brief 用 ffmpeg 的软件解码器解音频（AAC，也能解 MP3 等其他格式）：压缩包 → PCM 帧
 * @note RK3588 没有音频解码硬件（MPP 只管视频），音频都是 CPU 软件解。AAC 计算量很小。
 *       解出来的 PCM 格式由解码器决定：ffmpeg 的 aac 解码器输出 AV_SAMPLE_FMT_FLTP
 *       （float、planar：左声道一整块、右声道一整块），要交给 ALSA 还得经过 AudioResampler 转换
 */
class SoftwareAudioDecoder {
public:
    SoftwareAudioDecoder() = default;
    SoftwareAudioDecoder(const SoftwareAudioDecoder &) = delete;            // 不能复制：AVCodecContext 只能有一个主人
    SoftwareAudioDecoder &operator=(const SoftwareAudioDecoder &) = delete; // 同上

    /**
     * @brief 按音频流的参数找解码器并打开
     * @param[in] audioStream  音频流；用它的 codec_id 找解码器，codecpar 里的 extradata（AudioSpecificConfig）
     *                         告诉解码器采样率、声道数、AAC 的类型
     * @return true = 成功；false = 失败（已打印原因）
     * @note 只能调用一次
     */
    bool init(const AVStream *audioStream);

    /**
     * @brief 送一个压缩包
     * @param[in] packet  音频包；传 nullptr 表示"没有了"，解码器把缓存的数据都吐出来
     * @return true = 成功；false = 出错（已打印原因）
     * @note 送完要调用 receiveFrame 把帧都取出来，直到返回 NeedMoreInput，再送下一个包
     */
    bool sendPacket(const AVPacket *packet);

    /**
     * @brief 取一帧解好的 PCM
     * @param[out] outputFramePtr  取到的帧，只有返回 Frame 时有效。传进来空的也行，函数里会申请
     * @return Frame / NeedMoreInput / EndOfStream / Error
     * @note 帧的 best_effort_timestamp 是显示时间，单位是音频流的 time_base
     */
    ReceiveFrameResult receiveFrame(AvFramePtr &outputFramePtr);

    /**
     * @brief 清空解码器内部的缓存（seek 时调用；送过 nullptr 以后想接着用也要调用）
     * @return 无
     * @note avcodec_flush_buffers 之后解码器回到刚打开时的状态
     */
    void flush();

private:
    AvCodecContextPtr codecContextPtr; // 打开的解码器
};
