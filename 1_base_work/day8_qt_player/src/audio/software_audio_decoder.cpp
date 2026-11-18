#include "audio/software_audio_decoder.h"

#include "common/log.h"

static const char *const kTag = "SoftwareAudioDecoder";

/**
 * @brief 找解码器并打开
 * @param[in] audioStream  音频流
 * @return true = 成功；false = 失败
 * @note 第 1 步 按 codec_id 找解码器；第 2 步 申请上下文；第 3 步 抄流参数；第 4 步 打开
 */
bool SoftwareAudioDecoder::init(const AVStream *audioStream) {
    if (codecContextPtr) {
        LOGE(kTag, "init 只能调用一次");
        return false;
    }

    // 第 1 步 avcodec_find_decoder：按编码格式（AV_CODEC_ID_AAC）找一个解码器。
    // 板子的 ffmpeg 没有 rkmpp，找到的就是 ffmpeg 自带的软件解码器 "aac"
    const AVCodec *codec = avcodec_find_decoder(audioStream->codecpar->codec_id);
    if (!codec) {
        LOGE(kTag, "ffmpeg 里没有 %s 的解码器", avcodec_get_name(audioStream->codecpar->codec_id));
        return false;
    }

    // 第 2 步 申请解码器上下文（解码器的一个实例）
    codecContextPtr.reset(avcodec_alloc_context3(codec));
    if (!codecContextPtr) {
        LOGE(kTag, "avcodec_alloc_context3 失败");
        return false;
    }

    // 第 3 步 把流参数（采样率、声道、extradata）抄进上下文；pkt_timebase 告诉解码器包的时间单位，
    // 它算 best_effort_timestamp、处理 AAC 开头要跳过的采样（priming）时要用
    int ret = avcodec_parameters_to_context(codecContextPtr.get(), audioStream->codecpar);
    if (ret < 0) {
        LOGE(kTag, "avcodec_parameters_to_context 失败：%s", convertFfmpegErrorToString(ret).c_str());
        return false;
    }
    codecContextPtr->pkt_timebase = audioStream->time_base;

    // 第 4 步 打开解码器
    ret = avcodec_open2(codecContextPtr.get(), codec, nullptr);
    if (ret < 0) {
        LOGE(kTag, "avcodec_open2 失败：%s", convertFfmpegErrorToString(ret).c_str());
        return false;
    }
    LOGI(kTag, "解码器 %s：%d Hz，%d 声道，输出采样格式 %s", codec->name, codecContextPtr->sample_rate,
         codecContextPtr->channels, av_get_sample_fmt_name(codecContextPtr->sample_fmt));
    return true;
}

/**
 * @brief 送一个压缩包
 * @param[in] packet  音频包；nullptr = 没有了
 * @return true = 成功；false = 失败
 * @note avcodec_send_packet 只复制引用（引用计数加 1），不复制数据
 */
bool SoftwareAudioDecoder::sendPacket(const AVPacket *packet) {
    const int ret = avcodec_send_packet(codecContextPtr.get(), packet);
    // AVERROR_EOF：已经送过 nullptr 了还在送，按"没出错"处理，调用方接下来会 flush
    if (ret < 0 && ret != AVERROR_EOF) {
        LOGE(kTag, "avcodec_send_packet 失败：%s", convertFfmpegErrorToString(ret).c_str());
        return false;
    }
    return true;
}

/**
 * @brief 取一帧 PCM
 * @param[out] outputFramePtr  取到的帧
 * @return Frame / NeedMoreInput / EndOfStream / Error
 * @note avcodec_receive_frame 会先 unref 传进去的帧，再填新数据，所以同一个帧可以反复用
 */
ReceiveFrameResult SoftwareAudioDecoder::receiveFrame(AvFramePtr &outputFramePtr) {
    if (!outputFramePtr) {
        outputFramePtr = allocateAvFrame();
        if (!outputFramePtr) {
            LOGE(kTag, "av_frame_alloc 失败");
            return ReceiveFrameResult::Error;
        }
    }
    const int ret = avcodec_receive_frame(codecContextPtr.get(), outputFramePtr.get());
    if (ret == 0) {
        return ReceiveFrameResult::Frame;
    }
    if (ret == AVERROR(EAGAIN)) {
        return ReceiveFrameResult::NeedMoreInput;
    }
    if (ret == AVERROR_EOF) {
        return ReceiveFrameResult::EndOfStream;
    }
    LOGE(kTag, "avcodec_receive_frame 失败：%s", convertFfmpegErrorToString(ret).c_str());
    return ReceiveFrameResult::Error;
}

/**
 * @brief 清空解码器缓存
 * @return 无
 * @note seek 以后旧位置的数据不能再用；EOS 以后解码器处于"结束"状态，也要 flush 才能接着解
 */
void SoftwareAudioDecoder::flush() {
    if (codecContextPtr) {
        avcodec_flush_buffers(codecContextPtr.get());
    }
}
