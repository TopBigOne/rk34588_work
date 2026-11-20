// 来源：3_one_year_work/N3_work/n3_recorder/claude_reference/第14周_N3_M4_循环覆盖和断电保护/src/common/ffmpeg_utils.h
// 来源：1_base_work/day8_qt_player/src/common/ffmpeg_utils.h（删掉了 N3 用不到的 swresample、AVFrame、bsf）
#pragma once

#include <memory>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

struct AvInputFormatContextDeleter {
    void operator()(AVFormatContext *formatContext) const { avformat_close_input(&formatContext); }
};
using AvInputFormatContextPtr = std::unique_ptr<AVFormatContext, AvInputFormatContextDeleter>;

// 输出用的上下文：avformat_alloc_output_context2 得到的，用 avformat_free_context 释放（不能用 avformat_close_input）
struct AvOutputFormatContextDeleter {
    void operator()(AVFormatContext *formatContext) const { avformat_free_context(formatContext); }
};
using AvOutputFormatContextPtr = std::unique_ptr<AVFormatContext, AvOutputFormatContextDeleter>;

struct AvPacketDeleter {
    void operator()(AVPacket *packet) const { av_packet_free(&packet); }
};
using AvPacketPtr = std::unique_ptr<AVPacket, AvPacketDeleter>;

inline AvPacketPtr allocateAvPacket() { return AvPacketPtr(av_packet_alloc()); }

inline std::string convertFfmpegErrorToString(int errorCode) {
    char errorText[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(errorCode, errorText, sizeof(errorText));
    return errorText;
}
