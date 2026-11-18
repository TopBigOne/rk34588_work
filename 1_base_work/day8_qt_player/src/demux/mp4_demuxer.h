#pragma once

#include <cstdint> // int64_t

#include "common/ffmpeg_utils.h"

/** readPacket() 的结果 */
enum class ReadPacketResult {
    Packet,    // 读到一个包
    EndOfFile, // 文件读完了
    Error,     // 出错
};

/**
 * @brief mp4 拆封装：打开文件、找出视频流和音频流、一个一个读出压缩包、跳到指定时间
 * @note 只拆封装，不解码。读出来的包还是压缩数据：视频是 H.264 / H.265 码流（mp4 里是 AVCC 格式），
 *       音频是 AAC 码流。
 *       一个对象只能 open 一次；不能复制
 */
class Mp4Demuxer {
public:
    Mp4Demuxer() = default;
    Mp4Demuxer(const Mp4Demuxer &) = delete;            // 不能复制：两个对象拿着同一个 AVFormatContext，会关两次
    Mp4Demuxer &operator=(const Mp4Demuxer &) = delete; // 同上

    /**
     * @brief 打开文件，读出流信息，找到最合适的视频流和音频流
     * @param[in] filePath  mp4 文件路径（libavformat 按内容识别格式，mov、mkv、flv 其实也能打开）
     * @return true = 成功，而且至少有一个视频流或音频流；false = 失败（已打印原因）
     * @note 第 1 步 avformat_open_input 读文件头；第 2 步 avformat_find_stream_info 补全流信息；
     *       第 3 步 av_find_best_stream 分别挑视频流、音频流
     */
    bool open(const char *filePath);

    /**
     * @brief 视频流
     * @return 视频流；文件里没有视频时返回 nullptr
     * @note AVStream 归 AVFormatContext 所有，demuxer 析构以后就不能再用
     */
    const AVStream *getVideoStream() const;

    /**
     * @brief 音频流
     * @return 音频流；文件里没有音频时返回 nullptr
     * @note 同上
     */
    const AVStream *getAudioStream() const;

    /**
     * @brief 整个文件有多长
     * @return 微秒；文件头里没写时返回 0
     * @note AVFormatContext::duration 的单位固定是 AV_TIME_BASE（1/1000000 秒），本来就是微秒
     */
    int64_t getDurationUs() const;

    /**
     * @brief 读下一个包（视频包、音频包交错着出来，用 stream_index 区分）
     * @param[out] outputPacketPtr  读到的包，只有返回 Packet 时有效。调用方传进来一个空的 AvPacketPtr 也行，
     *                              函数里会申请
     * @return Packet = 读到一个包（可能是视频、音频，也可能是我们不用的流，调用方按 stream_index 判断）；
     *         EndOfFile = 读完了；Error = 出错
     * @note 包里的 pts、dts、duration 的单位是这个包所属流的 time_base，还没换成微秒
     */
    ReadPacketResult readPacket(AvPacketPtr &outputPacketPtr);

    /**
     * @brief 跳到指定时间之前最近的关键帧
     * @param[in] targetUs  目标时间，微秒，从文件开头算
     * @return true = 成功；false = 失败（已打印原因）
     * @note 只能跳到关键帧（解码必须从关键帧开始），所以实际位置一般比 targetUs 早；
     *       从关键帧到 targetUs 之间解出来的帧，由调用方自己丢掉
     */
    bool seekTo(int64_t targetUs);

private:
    AvFormatContextPtr formatContextPtr; // 打开的文件
    int videoStreamIndex = -1;           // 视频流是第几个流；-1 = 没有
    int audioStreamIndex = -1;           // 音频流是第几个流；-1 = 没有
};
