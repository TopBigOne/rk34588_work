#pragma once

#include <cstdint>
#include <string>

#include "common/encoded_frame.h"
#include "common/ffmpeg_utils.h"

enum class ReceiveResult { FrameReady, StreamEnded, Timeout, Error };

// 用 libavformat 读一路视频码流：本地 .h264/.h265 裸流文件，或者 rtsp:// URL（SRS 等服务端）。
// 输出和 0_跨阶段接口.md 的 RtspReceiver 一样的 StreamInfo / EncodedFrame（N2 跳过，用它代替）
class FfmpegFrameSource {
public:
    // requestedRawStreamFps：裸流文件没有可靠的时间戳，按这个帧率造；RTSP 不用它
    bool open(const std::string &requestedUrl, int requestedRawStreamFps);
    ReceiveResult receiveNextFrame(EncodedFrame &outputFrame);
    const StreamInfo &getStreamInfo() const { return streamInfo; }
    bool isRawStreamFile() const { return isRawStream; }
    // 裸流文件读完后从头再读（M2 起，用 60 秒的 aaa.264 录 10 分钟）；时间戳接着涨，不回到 0
    void setShouldLoopRawFile(bool requestedShouldLoop) { shouldLoopRawFile = requestedShouldLoop; }
    int getLoopCount() const { return loopCount; }
    void close();

private:
    bool openInput();

    std::string url;
    bool shouldLoopRawFile = false;
    int loopCount = 0;
    AvInputFormatContextPtr inputContextPtr;
    AvPacketPtr packetPtr;
    int videoStreamIndex = -1;
    StreamInfo streamInfo;
    bool isRawStream = false;
    int rawStreamFps = 30;
    int64_t nextFrameIndex = 0;
    int64_t firstPacketPts = AV_NOPTS_VALUE;
};
