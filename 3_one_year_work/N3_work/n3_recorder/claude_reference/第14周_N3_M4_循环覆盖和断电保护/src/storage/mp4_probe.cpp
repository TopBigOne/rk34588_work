#include "storage/mp4_probe.h"

#include "common/ffmpeg_utils.h"

int64_t probeMp4DurationMs(const std::string &filePath) {
    AVFormatContext *rawInputContext = nullptr;
    if (avformat_open_input(&rawInputContext, filePath.c_str(), nullptr, nullptr) != 0) {
        return -1;
    }
    AvInputFormatContextPtr inputContextPtr(rawInputContext);
    if (inputContextPtr->nb_streams == 0) {
        return -1;
    }
    AvPacketPtr packetPtr = allocateAvPacket();
    int64_t maxEndMs = 0;
    while (av_read_frame(inputContextPtr.get(), packetPtr.get()) == 0) {
        const AVPacket *packet = packetPtr.get();
        // 文件尾被截断时，最后一个包数据不全，libavformat 会打上 CORRUPT（日志 "Packet corrupt ... partial file"），不算
        if (packet->pts != AV_NOPTS_VALUE && (packet->flags & AV_PKT_FLAG_CORRUPT) == 0) {
            const AVRational timeBase = inputContextPtr->streams[packet->stream_index]->time_base;
            const int64_t endMs = av_rescale_q(packet->pts + packet->duration, timeBase, AVRational{1, 1000});
            maxEndMs = endMs > maxEndMs ? endMs : maxEndMs;
        }
        av_packet_unref(packetPtr.get());
    }
    return maxEndMs;
}
