#include "source/ffmpeg_frame_source.h"

#include <cstring>

#include "common/annexb_utils.h"
#include "common/log.h"

static const char *const kTag = "FrameSource";
static constexpr int kRtspTimeoutUs = 5 * 1000 * 1000;

bool FfmpegFrameSource::open(const std::string &requestedUrl, int requestedRawStreamFps) {
    url = requestedUrl;
    rawStreamFps = requestedRawStreamFps > 0 ? requestedRawStreamFps : 30;
    nextFrameIndex = 0;
    loopCount = 0;
    firstPacketPts = AV_NOPTS_VALUE;
    return openInput();
}

bool FfmpegFrameSource::openInput() {
    const std::string &requestedUrl = url;
    const bool isRtsp = requestedUrl.rfind("rtsp://", 0) == 0;

    // 第 1 步 打开输入。RTSP 走 TCP（interleaved）：adb reverse 只能转 TCP；stimeout 是 4.4 里 RTSP 读超时的选项名
    AVDictionary *options = nullptr;
    if (isRtsp) {
        av_dict_set(&options, "rtsp_transport", "tcp", 0);
        av_dict_set_int(&options, "stimeout", kRtspTimeoutUs, 0);
    }
    AVFormatContext *rawInputContext = nullptr;
    int ret = avformat_open_input(&rawInputContext, requestedUrl.c_str(), nullptr, &options);
    av_dict_free(&options);
    if (ret < 0) {
        LOGE(kTag, "打开 %s 失败：%s", requestedUrl.c_str(), convertFfmpegErrorToString(ret).c_str());
        return false;
    }
    inputContextPtr.reset(rawInputContext);

    // 第 2 步 读一小段数据，把宽高、extradata（SPS/PPS）填进 codecpar
    ret = avformat_find_stream_info(inputContextPtr.get(), nullptr);
    if (ret < 0) {
        LOGE(kTag, "find_stream_info 失败：%s", convertFfmpegErrorToString(ret).c_str());
        return false;
    }
    videoStreamIndex = av_find_best_stream(inputContextPtr.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoStreamIndex < 0) {
        LOGE(kTag, "%s 里没有视频流", requestedUrl.c_str());
        return false;
    }
    const AVCodecParameters *codecParameters = inputContextPtr->streams[videoStreamIndex]->codecpar;
    if (codecParameters->codec_id == AV_CODEC_ID_H264) {
        streamInfo.codec = VideoCodec::H264;
    } else if (codecParameters->codec_id == AV_CODEC_ID_HEVC) {
        streamInfo.codec = VideoCodec::H265;
    } else {
        LOGE(kTag, "只支持 H.264 / H.265，这路是 %s", avcodec_get_name(codecParameters->codec_id));
        return false;
    }
    streamInfo.width = codecParameters->width;
    streamInfo.height = codecParameters->height;
    // 裸流和 RTSP（SDP 的 sprop-parameter-sets）给的 extradata 都是 Annex-B（推断，单测里核对裸流）
    const ParameterSets parameterSets =
            extractParameterSets(streamInfo.codec, codecParameters->extradata, codecParameters->extradata_size);
    streamInfo.vps = parameterSets.vps;
    streamInfo.sps = parameterSets.sps;
    streamInfo.pps = parameterSets.pps;

    // 裸流文件：iformat 是 "h264" / "hevc"，它给的时间戳不可靠（N0 M5 碰到过），自己按帧率造
    isRawStream = std::strcmp(inputContextPtr->iformat->name, "h264") == 0 ||
                  std::strcmp(inputContextPtr->iformat->name, "hevc") == 0;
    packetPtr = allocateAvPacket();
    LOGI(kTag, "%s：%s %dx%d，%s，sps %zu 字节，pps %zu 字节", requestedUrl.c_str(),
         avcodec_get_name(codecParameters->codec_id), streamInfo.width, streamInfo.height,
         isRawStream ? "裸流文件（按帧率造时间戳）" : "用输入自带的时间戳", streamInfo.sps.size(), streamInfo.pps.size());
    return packetPtr != nullptr;
}

ReceiveResult FfmpegFrameSource::receiveNextFrame(EncodedFrame &outputFrame) {
    while (true) {
        const int ret = av_read_frame(inputContextPtr.get(), packetPtr.get());
        if (ret == AVERROR_EOF) {
            // 循环：重新打开比 av_seek_frame 稳，裸流的 parser 状态也一起清掉。nextFrameIndex 不清零，时间戳接着涨
            if (isRawStream && shouldLoopRawFile) {
                inputContextPtr.reset();
                if (!openInput()) {
                    return ReceiveResult::Error;
                }
                loopCount++;
                continue;
            }
            return ReceiveResult::StreamEnded;
        }
        if (ret == AVERROR(ETIMEDOUT)) {
            return ReceiveResult::Timeout;
        }
        if (ret < 0) {
            LOGE(kTag, "av_read_frame 失败：%s", convertFfmpegErrorToString(ret).c_str());
            return ReceiveResult::Error;
        }
        AVPacket *packet = packetPtr.get();
        if (packet->stream_index != videoStreamIndex) {
            av_packet_unref(packet);
            continue;
        }

        outputFrame.annexBData.assign(packet->data, packet->data + packet->size);
        outputFrame.isKeyFrame = containsKeyFrameNal(streamInfo.codec, packet->data, packet->size);
        outputFrame.frameIndex = nextFrameIndex;
        if (isRawStream) {
            outputFrame.extendedRtpTimestamp = nextFrameIndex * streamInfo.clockRateHz / rawStreamFps;
        } else {
            // RTSP 视频流的 time_base 是 1/90000；libavformat 给的 pts 已经是 64 位（推断：rtpdec 处理了回绕）
            const AVRational streamTimeBase = inputContextPtr->streams[videoStreamIndex]->time_base;
            const int64_t packetPts = packet->pts != AV_NOPTS_VALUE ? packet->pts : packet->dts;
            if (firstPacketPts == AV_NOPTS_VALUE) {
                firstPacketPts = packetPts;
            }
            outputFrame.extendedRtpTimestamp =
                    av_rescale_q(packetPts - firstPacketPts, streamTimeBase, AVRational{1, streamInfo.clockRateHz});
        }
        outputFrame.rtpTimestamp = static_cast<uint32_t>(outputFrame.extendedRtpTimestamp);
        nextFrameIndex++;
        av_packet_unref(packet);
        return ReceiveResult::FrameReady;
    }
}

void FfmpegFrameSource::close() {
    packetPtr.reset();
    inputContextPtr.reset();
}
