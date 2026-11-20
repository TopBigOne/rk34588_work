#include "storage/mp4_file_writer.h"

#include <algorithm>

#include "common/annexb_utils.h"
#include "common/log.h"

static const char *const kTag = "Mp4Writer";
static constexpr AVRational kInputTimeBase = {1, 90000};

VideoTrackParams buildVideoTrackParams(const StreamInfo &streamInfo, int width, int height) {
    VideoTrackParams trackParams;
    trackParams.codec = streamInfo.codec;
    trackParams.width = width;
    trackParams.height = height;
    trackParams.extradataAnnexb = buildAnnexBExtradata({streamInfo.vps, streamInfo.sps, streamInfo.pps});
    return trackParams;
}

bool Mp4FileWriter::open(const std::string &requestedFilePath, const VideoTrackParams &trackParams,
                         bool shouldFragment) {
    if (isOpen()) {
        LOGE(kTag, "%s 还没 close，不能再 open", filePath.c_str());
        return false;
    }
    if (trackParams.extradataAnnexb.empty()) {
        LOGE(kTag, "没有 SPS/PPS，写不了 avcC/hvcC");
        return false;
    }
    filePath = requestedFilePath;
    lastPtsIn90kHz = -1;
    writtenBytes = 0;
    writtenFrameCount = 0;

    // 第 1 步 输出上下文，格式按名字 "mp4" 选，不看扩展名
    AVFormatContext *rawOutputContext = nullptr;
    int ret = avformat_alloc_output_context2(&rawOutputContext, nullptr, "mp4", filePath.c_str());
    if (ret < 0) {
        LOGE(kTag, "alloc_output_context2 失败：%s", convertFfmpegErrorToString(ret).c_str());
        return false;
    }
    outputContextPtr.reset(rawOutputContext);

    // 第 2 步 视频轨。extradata 要用 av_malloc 分配，并多留 AV_INPUT_BUFFER_PADDING_SIZE 个 0，释放归 libavformat
    videoStream = avformat_new_stream(outputContextPtr.get(), nullptr);
    if (videoStream == nullptr) {
        LOGE(kTag, "new_stream 失败");
        outputContextPtr.reset();
        return false;
    }
    AVCodecParameters *codecParameters = videoStream->codecpar;
    codecParameters->codec_type = AVMEDIA_TYPE_VIDEO;
    codecParameters->codec_id = trackParams.codec == VideoCodec::H264 ? AV_CODEC_ID_H264 : AV_CODEC_ID_HEVC;
    codecParameters->width = trackParams.width;
    codecParameters->height = trackParams.height;
    const size_t extradataSizeBytes = trackParams.extradataAnnexb.size();
    codecParameters->extradata = static_cast<uint8_t *>(av_mallocz(extradataSizeBytes + AV_INPUT_BUFFER_PADDING_SIZE));
    if (codecParameters->extradata == nullptr) {
        outputContextPtr.reset();
        return false;
    }
    std::copy(trackParams.extradataAnnexb.begin(), trackParams.extradataAnnexb.end(), codecParameters->extradata);
    codecParameters->extradata_size = static_cast<int>(extradataSizeBytes);
    videoStream->time_base = kInputTimeBase; // 只是建议，写头时封装器可能改

    // 第 3 步 打开文件
    ret = avio_open(&outputContextPtr->pb, filePath.c_str(), AVIO_FLAG_WRITE);
    if (ret < 0) {
        LOGE(kTag, "打开 %s 失败：%s", filePath.c_str(), convertFfmpegErrorToString(ret).c_str());
        outputContextPtr.reset();
        return false;
    }

    // 第 4 步 写头。fMP4：moov 在前、每个关键帧开一个新的 moof+mdat 片段，进程被杀也能播已写完的片段
    AVDictionary *options = nullptr;
    if (shouldFragment) {
        av_dict_set(&options, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
        // 每写完一个包就把 AVIO 缓冲区（默认 32 KB）刷进文件。不刷的话，片段写完一部分还压在缓冲区里，
        // kill -9 时连上一个完整片段也会缺尾巴（2026-11-20 Mac 实测丢了约 63 帧，刷了以后见实现记录）
        outputContextPtr->flags |= AVFMT_FLAG_FLUSH_PACKETS;
    }
    ret = avformat_write_header(outputContextPtr.get(), &options);
    av_dict_free(&options);
    if (ret < 0) {
        LOGE(kTag, "write_header 失败：%s", convertFfmpegErrorToString(ret).c_str());
        avio_closep(&outputContextPtr->pb);
        outputContextPtr.reset();
        return false;
    }
    packetPtr = allocateAvPacket();
    LOGI(kTag, "打开 %s（%s，%dx%d，time_base 1/%d）", filePath.c_str(), shouldFragment ? "fMP4" : "MP4",
         trackParams.width, trackParams.height, videoStream->time_base.den);
    return packetPtr != nullptr;
}

bool Mp4FileWriter::writeFrame(const EncodedFrame &frame, int64_t ptsIn90kHz) {
    if (!isOpen()) {
        LOGE(kTag, "还没 open");
        return false;
    }
    if (ptsIn90kHz <= lastPtsIn90kHz) {
        // MP4 要求 dts 严格递增（没有 B 帧时 pts = dts），重复或倒退的帧丢掉
        LOGW(kTag, "pts %lld 不大于上一帧 %lld，丢掉", static_cast<long long>(ptsIn90kHz),
             static_cast<long long>(lastPtsIn90kHz));
        return true;
    }

    // AVPacket 只借用 frame 的数据（不拷贝）：data 指过去，buf 为空，封装器写的时候需要自己会复制
    AVPacket *packet = packetPtr.get();
    av_packet_unref(packet);
    packet->data = const_cast<uint8_t *>(frame.annexBData.data());
    packet->size = static_cast<int>(frame.annexBData.size());
    packet->stream_index = videoStream->index;
    packet->pts = ptsIn90kHz;
    packet->dts = ptsIn90kHz;
    // 没有下一帧时不知道这一帧多长，按和上一帧的间隔估（帧率恒定时就是对的），否则最后一帧时长是 0
    packet->duration = lastPtsIn90kHz >= 0 ? ptsIn90kHz - lastPtsIn90kHz : 0;
    packet->flags = frame.isKeyFrame ? AV_PKT_FLAG_KEY : 0;
    av_packet_rescale_ts(packet, kInputTimeBase, videoStream->time_base);

    const int ret = av_interleaved_write_frame(outputContextPtr.get(), packet);
    packet->data = nullptr;
    packet->size = 0;
    if (ret < 0) {
        LOGE(kTag, "写第 %d 帧失败：%s", writtenFrameCount, convertFfmpegErrorToString(ret).c_str());
        return false;
    }
    lastPtsIn90kHz = ptsIn90kHz;
    writtenBytes += static_cast<int64_t>(frame.annexBData.size());
    writtenFrameCount++;
    return true;
}

bool Mp4FileWriter::close() {
    if (!isOpen()) {
        return true;
    }
    // 普通 MP4 的 moov（索引）在这里才写；不 close 就没有 moov，文件放不了
    const int trailerRet = av_write_trailer(outputContextPtr.get());
    const int closeRet = avio_closep(&outputContextPtr->pb);
    outputContextPtr.reset();
    videoStream = nullptr;
    packetPtr.reset();
    if (trailerRet < 0 || closeRet < 0) {
        LOGE(kTag, "关 %s 失败：trailer %s，close %s", filePath.c_str(),
             convertFfmpegErrorToString(trailerRet).c_str(), convertFfmpegErrorToString(closeRet).c_str());
        return false;
    }
    LOGI(kTag, "关闭 %s：%d 帧，码流 %lld 字节", filePath.c_str(), writtenFrameCount,
         static_cast<long long>(writtenBytes));
    return true;
}
