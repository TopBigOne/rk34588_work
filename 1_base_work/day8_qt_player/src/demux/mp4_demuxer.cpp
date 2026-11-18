#include "demux/mp4_demuxer.h"

#include "common/log.h"

static const char *const kTag = "Mp4Demuxer";

/**
 * @brief 打开文件，找到视频流和音频流
 * @param[in] filePath  文件路径
 * @return true = 成功；false = 失败
 * @note 第 1 步 读文件头；第 2 步 补全流信息；第 3 步 挑视频流、音频流
 */
bool Mp4Demuxer::open(const char *filePath) {
    if (formatContextPtr) {
        LOGE(kTag, "open 只能调用一次");
        return false;
    }

    // 第 1 步 avformat_open_input(&上下文, 路径, 指定格式, 选项)：
    //   上下文传 nullptr 的地址 → 函数自己申请一个，写回来；
    //   指定格式传 nullptr → 按文件内容自动识别（mp4 认的是 'ftyp' 这几个字节）；
    //   失败时它会自己释放上下文，不用我们管
    AVFormatContext *rawFormatContext = nullptr;
    int ret = avformat_open_input(&rawFormatContext, filePath, nullptr, nullptr);
    if (ret < 0) {
        LOGE(kTag, "avformat_open_input %s 失败：%s", filePath, convertFfmpegErrorToString(ret).c_str());
        return false;
    }
    formatContextPtr.reset(rawFormatContext);

    // 第 2 步 avformat_find_stream_info：mp4 的文件头（moov）里已经有大部分信息，
    // 这一步再读几个包，把缺的（比如帧率）补上
    ret = avformat_find_stream_info(formatContextPtr.get(), nullptr);
    if (ret < 0) {
        LOGE(kTag, "avformat_find_stream_info 失败：%s", convertFfmpegErrorToString(ret).c_str());
        return false;
    }

    // 第 3 步 av_find_best_stream(上下文, 流类型, 想要第几个(-1=自动), 相关流(-1=不管), 解码器输出(不要), 标志)：
    // 一个文件里可能有多条音轨（比如国语、粤语），它挑一条"最合适"的（一般是默认音轨）。
    // 返回流的序号；没有这种流时返回负数 AVERROR_STREAM_NOT_FOUND
    videoStreamIndex = av_find_best_stream(formatContextPtr.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    audioStreamIndex = av_find_best_stream(formatContextPtr.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (videoStreamIndex < 0) {
        videoStreamIndex = -1;
    }
    if (audioStreamIndex < 0) {
        audioStreamIndex = -1;
    }
    if (videoStreamIndex < 0 && audioStreamIndex < 0) {
        LOGE(kTag, "%s 里既没有视频流也没有音频流", filePath);
        return false;
    }

    // 打印一下找到了什么，方便核对
    if (const AVStream *videoStream = getVideoStream()) {
        const AVCodecParameters *videoParameters = videoStream->codecpar; // 这条流的编码参数（格式、宽高……）
        LOGI(kTag, "视频流 #%d：%s %dx%d，time_base %d/%d", videoStreamIndex,
             avcodec_get_name(videoParameters->codec_id), videoParameters->width, videoParameters->height,
             videoStream->time_base.num, videoStream->time_base.den);
    }
    if (const AVStream *audioStream = getAudioStream()) {
        const AVCodecParameters *audioParameters = audioStream->codecpar;
        LOGI(kTag, "音频流 #%d：%s %d Hz %d 声道，time_base %d/%d", audioStreamIndex,
             avcodec_get_name(audioParameters->codec_id), audioParameters->sample_rate, audioParameters->channels,
             audioStream->time_base.num, audioStream->time_base.den);
    }
    LOGI(kTag, "时长 %.3f 秒", getDurationUs() / 1e6);
    return true;
}

/**
 * @brief 视频流
 * @return 视频流；没有时返回 nullptr
 * @note streams 是 AVStream* 数组，下标就是流的序号
 */
const AVStream *Mp4Demuxer::getVideoStream() const {
    return videoStreamIndex >= 0 ? formatContextPtr->streams[videoStreamIndex] : nullptr;
}

/**
 * @brief 音频流
 * @return 音频流；没有时返回 nullptr
 * @note 同上
 */
const AVStream *Mp4Demuxer::getAudioStream() const {
    return audioStreamIndex >= 0 ? formatContextPtr->streams[audioStreamIndex] : nullptr;
}

/**
 * @brief 文件时长
 * @return 微秒；不知道时返回 0
 * @note duration 是 AV_NOPTS_VALUE 表示文件头里没写
 */
int64_t Mp4Demuxer::getDurationUs() const {
    if (!formatContextPtr || formatContextPtr->duration == AV_NOPTS_VALUE) {
        return 0;
    }
    return formatContextPtr->duration;
}

/**
 * @brief 读下一个包
 * @param[out] outputPacketPtr  读到的包
 * @return Packet / EndOfFile / Error
 * @note av_read_frame 每次给一个完整的包：视频是一帧，音频是一个 AAC 帧（1024 个采样）
 */
ReadPacketResult Mp4Demuxer::readPacket(AvPacketPtr &outputPacketPtr) {
    if (!outputPacketPtr) {
        outputPacketPtr = allocateAvPacket();
        if (!outputPacketPtr) {
            LOGE(kTag, "av_packet_alloc 失败");
            return ReadPacketResult::Error;
        }
    }
    // av_read_frame 往包里填数据之前不会先清掉旧数据，传进来的包如果还引用着上一次的数据就会泄漏，
    // 所以先 unref 一下（空包 unref 什么也不做）
    av_packet_unref(outputPacketPtr.get());
    const int ret = av_read_frame(formatContextPtr.get(), outputPacketPtr.get());
    if (ret == AVERROR_EOF) {
        return ReadPacketResult::EndOfFile;
    }
    if (ret < 0) {
        LOGE(kTag, "av_read_frame 失败：%s", convertFfmpegErrorToString(ret).c_str());
        return ReadPacketResult::Error;
    }
    return ReadPacketResult::Packet;
}

/**
 * @brief 跳到指定时间之前最近的关键帧
 * @param[in] targetUs  目标时间，微秒
 * @return true = 成功；false = 失败
 * @note av_seek_frame(上下文, 流序号, 时间戳, 标志)：
 *       流序号传 -1 → 时间戳的单位是 AV_TIME_BASE（微秒），libavformat 自己选一条流（有视频时选视频）来找关键帧；
 *       AVSEEK_FLAG_BACKWARD → 找"不晚于"目标时间的关键帧，否则可能跳到目标后面的关键帧
 */
bool Mp4Demuxer::seekTo(int64_t targetUs) {
    const int ret = av_seek_frame(formatContextPtr.get(), -1, targetUs, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        LOGE(kTag, "av_seek_frame %.3f 秒失败：%s", targetUs / 1e6, convertFfmpegErrorToString(ret).c_str());
        return false;
    }
    return true;
}
