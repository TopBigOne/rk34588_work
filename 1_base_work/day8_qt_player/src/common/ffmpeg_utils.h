/*
 * ffmpeg 相关的小工具：把 ffmpeg 的对象包成 unique_ptr 的删除器、错误码转文字、时间换算成微秒
 *
 * ffmpeg 是 C 库，头文件里的函数没有 extern "C"，C++ 直接 include 的话链接时会按 C++ 的规则去找
 * 带"名字修饰"的符号（例如 _Z15av_packet_allocv），找不到。所以要套一层 extern "C" { ... }，
 * 告诉编译器"这些函数是按 C 的规则编译的，符号名就是函数名本身"
 */
#pragma once

#include <cstdint> // int64_t、INT64_MIN
#include <memory>  // std::unique_ptr
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>          // AVCodecContext、AVPacket、AVBSFContext（bitstream filter）
#include <libavformat/avformat.h>        // AVFormatContext、AVStream
#include <libavutil/frame.h>             // AVFrame
#include <libswresample/swresample.h>    // SwrContext
}

/** AvFormatContextPtr 的删除器：关闭文件并释放 AVFormatContext */
struct AvFormatContextDeleter {
    /**
     * @brief 关闭输入文件
     * @param[in] formatContext  avformat_open_input 打开的上下文
     * @return 无
     * @note avformat_close_input 要的是 AVFormatContext**（地址），它关完会把指针置成 NULL
     */
    void operator()(AVFormatContext *formatContext) const { avformat_close_input(&formatContext); }
};
using AvFormatContextPtr = std::unique_ptr<AVFormatContext, AvFormatContextDeleter>;

/** AvPacketPtr 的删除器 */
struct AvPacketDeleter {
    /**
     * @brief 释放 AVPacket 结构体，以及它引用的数据
     * @param[in] packet  av_packet_alloc 得到的包
     * @return 无
     * @note av_packet_free = 先 av_packet_unref（数据引用计数减 1），再释放结构体本身
     */
    void operator()(AVPacket *packet) const { av_packet_free(&packet); }
};
using AvPacketPtr = std::unique_ptr<AVPacket, AvPacketDeleter>;

/** AvFramePtr 的删除器 */
struct AvFrameDeleter {
    /**
     * @brief 释放 AVFrame 结构体，以及它引用的采样 / 像素数据
     * @param[in] frame  av_frame_alloc 得到的帧
     * @return 无
     * @note 和 av_packet_free 一样：先 unref 数据，再释放结构体
     */
    void operator()(AVFrame *frame) const { av_frame_free(&frame); }
};
using AvFramePtr = std::unique_ptr<AVFrame, AvFrameDeleter>;

/** AvCodecContextPtr 的删除器 */
struct AvCodecContextDeleter {
    /**
     * @brief 关闭解码器并释放上下文
     * @param[in] codecContext  avcodec_alloc_context3 得到的上下文
     * @return 无
     * @note avcodec_free_context 会顺带关闭（avcodec_close）已经打开的解码器
     */
    void operator()(AVCodecContext *codecContext) const { avcodec_free_context(&codecContext); }
};
using AvCodecContextPtr = std::unique_ptr<AVCodecContext, AvCodecContextDeleter>;

/** AvBsfContextPtr 的删除器 */
struct AvBsfContextDeleter {
    /**
     * @brief 释放 bitstream filter 上下文
     * @param[in] bsfContext  av_bsf_alloc 得到的上下文
     * @return 无
     * @note 里面还没取走的包也一起释放
     */
    void operator()(AVBSFContext *bsfContext) const { av_bsf_free(&bsfContext); }
};
using AvBsfContextPtr = std::unique_ptr<AVBSFContext, AvBsfContextDeleter>;

/** SwrContextPtr 的删除器 */
struct SwrContextDeleter {
    /**
     * @brief 释放重采样上下文
     * @param[in] swrContext  swr_alloc_set_opts 得到的上下文
     * @return 无
     * @note swr_free 要的也是地址
     */
    void operator()(SwrContext *swrContext) const { swr_free(&swrContext); }
};
using SwrContextPtr = std::unique_ptr<SwrContext, SwrContextDeleter>;

/**
 * @brief 申请一个空的 AVPacket，包进 unique_ptr
 * @return 新的包；内存不够时里面是 nullptr
 * @note 只分配结构体，不分配数据；av_read_frame / av_bsf_receive_packet 会往里面填数据
 */
inline AvPacketPtr allocateAvPacket() { return AvPacketPtr(av_packet_alloc()); }

/**
 * @brief 申请一个空的 AVFrame，包进 unique_ptr
 * @return 新的帧；内存不够时里面是 nullptr
 * @note 只分配结构体，avcodec_receive_frame 会往里面填数据
 */
inline AvFramePtr allocateAvFrame() { return AvFramePtr(av_frame_alloc()); }

/**
 * @brief 把 ffmpeg 的错误码换成文字
 * @param[in] errorCode  ffmpeg 函数返回的负数，例如 AVERROR(EAGAIN)、AVERROR_EOF
 * @return 错误说明，例如 "Resource temporarily unavailable"、"End of file"
 * @note AV_ERROR_MAX_STRING_SIZE = 64，av_strerror 保证写进去的文字不超过这么长
 */
inline std::string convertFfmpegErrorToString(int errorCode) {
    char errorText[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(errorCode, errorText, sizeof(errorText));
    return errorText;
}

// 时间戳"没有值"时用的数。ffmpeg 自己用 AV_NOPTS_VALUE（也是 INT64_MIN），这里换算成微秒后沿用同一个数
constexpr int64_t kNoPtsUs = INT64_MIN;

/**
 * @brief 把某个时间单位下的时间戳换算成微秒
 * @param[in] timestamp  时间戳，单位是 timeBase。例如 timeBase = 1/15360 时，timestamp = 15360 就是 1 秒
 * @param[in] timeBase   时间单位（一个分数，num/den 秒），来自 AVStream::time_base
 * @return 微秒；timestamp 是 AV_NOPTS_VALUE（没有时间戳）时返回 kNoPtsUs
 * @note av_rescale_q(a, bq, cq) = a × bq ÷ cq，内部用 128 位整数算，不会溢出也不会丢精度。
 *       mp4 里每个流的时间单位都不一样（视频常见 1/15360、1/90000，音频常见 1/48000），
 *       一读出来就换成微秒，后面所有模块只认微秒
 */
inline int64_t convertToMicroseconds(int64_t timestamp, AVRational timeBase) {
    if (timestamp == AV_NOPTS_VALUE) {
        return kNoPtsUs;
    }
    const AVRational microsecondTimeBase = {1, 1000000}; // 1/1000000 秒 = 1 微秒
    return av_rescale_q(timestamp, timeBase, microsecondTimeBase);
}
