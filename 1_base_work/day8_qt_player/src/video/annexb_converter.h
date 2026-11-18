#pragma once

#include <vector>

#include "common/ffmpeg_utils.h"

/**
 * @brief 把 mp4 里的 H.264 / H.265 视频包从 AVCC 格式转成 Annex B 格式（包一层 ffmpeg 的 bitstream filter）
 * @note 两种格式装的是同样的 NAL 单元，只是分隔方式不一样：
 *       - AVCC（mp4 里）：每个 NAL 前面是 4 字节的长度；SPS/PPS 不在码流里，单独放在文件头的 avcC / hvcC 里
 *       - Annex B（裸码流文件、MPP 要的）：每个 NAL 前面是起始码 00 00 00 01；SPS/PPS 就在码流里
 *       h264_mp4toannexb / hevc_mp4toannexb 把长度换成起始码，并在每个关键帧前面插上 SPS/PPS
 *       （以后 seek 到任何一个关键帧，解码器都能拿到 SPS/PPS）
 */
class AnnexbConverter {
public:
    AnnexbConverter() = default;
    AnnexbConverter(const AnnexbConverter &) = delete;            // 不能复制：AVBSFContext 只能有一个主人
    AnnexbConverter &operator=(const AnnexbConverter &) = delete; // 同上

    /**
     * @brief 按视频流的编码格式选 filter 并初始化
     * @param[in] videoStream  视频流；用它的 codec_id 选 filter，用它的 codecpar（带 avcC / hvcC）初始化
     * @return true = 成功；false = 失败（不是 H.264 / H.265，或者 ffmpeg 里没有这个 filter）
     * @note 只能调用一次
     */
    bool init(const AVStream *videoStream);

    /**
     * @brief 转换一个包
     * @param[in]  inputPacketPtr   AVCC 格式的视频包，所有权交给 filter（调用方要写 std::move）
     * @param[out] outputPackets    转好的 Annex B 包追加到这里。一般一进一出，个别情况可能 0 个或多个
     * @return true = 成功；false = 出错（已打印原因）
     * @note 包的 pts、dts 原样带过来，单位还是视频流的 time_base
     */
    bool convert(AvPacketPtr inputPacketPtr, std::vector<AvPacketPtr> &outputPackets);

    /**
     * @brief 清掉 filter 里还没取走的包（seek 时调用）
     * @return 无
     * @note 清完可以接着送新位置的包
     */
    void flush();

private:
    AvBsfContextPtr bsfContextPtr; // bitstream filter 的实例
};
