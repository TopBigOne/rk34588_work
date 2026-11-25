#pragma once

/**
 * @file
 * @brief MPP 视频硬解：替换 ffplay 视频解码里的 avcodec_send_packet / avcodec_receive_frame
 * @note 数据流：AVPacket（mp4 里的 avcC 格式）→ h264_mp4toannexb / hevc_mp4toannexb → MPP 硬解
 *       → NV12 的 MppFrame → 包成 AV_PIX_FMT_NV12 的 AVFrame（不复制像素）→ ffplay 原来的 pictq / 滤镜 / SDL 显示。
 *
 *       用法和 avcodec 的 send / receive 一样：
 *         mppVideoDecoderSendPacket   送一个包（data 为 NULL 的空包 = 码流结束）
 *         mppVideoDecoderReceiveFrame 取一帧，返回 0 / AVERROR(EAGAIN) / AVERROR_EOF
 *         mppVideoDecoderFlush        seek 后清空（相当于 avcodec_flush_buffers）
 *
 *       生命周期：解出来的 AVFrame 会在 pictq 里多留一会儿，ffplay 关闭流时又是先销毁解码器、后清帧队列，
 *       所以解码器内部带引用计数：每个还没释放的 AVFrame 都持有解码器的一份引用，
 *       mppVideoDecoderRelease 之后，要等最后一帧也释放了，才真正 mpp_destroy、释放内存池。
 */

#include "common/ffplay_common.h"

/** MPP 视频解码器（内部结构在 .c 里，外面只拿指针用） */
typedef struct MppVideoDecoder MppVideoDecoder;

/**
 * @brief 判断这个视频格式能不能用 MPP 硬解
 * @param[in] codecId  流的编码格式（AVCodecParameters::codec_id）
 * @return 1 = 能（目前只接 H.264、H.265）；0 = 不能，调用方走 FFmpeg 软解
 * @note RK3588 的 MPP 还能解 VP9、AV1 等，这里先只接 mp4 里最常见的两种
 */
int mppVideoDecoderIsSupported(enum AVCodecID codecId);

/**
 * @brief 创建 MPP 解码器和 mp4toannexb 码流过滤器
 * @param[in] codecParameters  视频流参数（宽高、extradata 里的 SPS/PPS、颜色信息）
 * @param[in] streamTimeBase   视频流的时间基，pts 的单位
 * @return 成功返回解码器；失败返回 NULL（已打印原因）
 * @note 用完调用 mppVideoDecoderRelease
 */
MppVideoDecoder* mppVideoDecoderCreate(const AVCodecParameters* codecParameters, AVRational streamTimeBase);

/**
 * @brief 送一个压缩包
 * @param[in] decoder  解码器
 * @param[in] packet   mp4 拆出来的一个包；data 为 NULL 表示码流结束（ffplay 读到文件尾时送的空包）
 * @return 0 = 收下了；AVERROR(EAGAIN) = 上一个包还没送进 MPP，先 receive 再送这个包；其他负数 = 出错
 * @note 包里的数据会被复制，返回后调用方可以马上 av_packet_unref
 */
int mppVideoDecoderSendPacket(MppVideoDecoder* decoder, const AVPacket* packet);

/**
 * @brief 取一帧解好的画面
 * @param[in]  decoder  解码器
 * @param[out] frame    取到的帧：AV_PIX_FMT_NV12，data 直接指向 MPP 的 buffer（不复制）；
 *                      av_frame_unref 时 buffer 还给 MPP 内存池
 * @return 0 = 取到一帧；AVERROR(EAGAIN) = 现在没有，要再送包（或稍等再取）；
 *         AVERROR_EOF = 码流结束后的帧都取完了；其他负数 = 出错
 * @note 不阻塞。返回 EAGAIN 时用 mppVideoDecoderIsWaitingForOutput 判断是“要送包”还是“稍等再取”
 */
int mppVideoDecoderReceiveFrame(MppVideoDecoder* decoder, AVFrame* frame);

/**
 * @brief 判断解码器是不是“手里还有活、只是还没出帧”
 * @param[in] decoder  解码器
 * @return 1 = 有一个包因为 MPP 输入队列满了还没送进去，或者已经送了结束包、还在等剩下的帧，
 *             这时再送包也没用，调用方应该稍等一下再 receive；
 *         0 = 解码器在等新的包
 * @note 对应 avcodec 的规则：receive 返回 EAGAIN 时一定能 send。MPP 的输入队列满了时做不到这一点，所以要单独问
 */
int mppVideoDecoderIsWaitingForOutput(const MppVideoDecoder* decoder);

/**
 * @brief 清空解码器（seek 或播完以后）：丢掉还没解的码流和还没取走的帧，回到“等关键帧”的状态
 * @param[in] decoder  解码器
 * @return 无
 * @note 相当于 avcodec_flush_buffers。已经交出去的 AVFrame 不受影响，照常显示和释放
 */
void mppVideoDecoderFlush(MppVideoDecoder* decoder);

/**
 * @brief 放弃调用方手里的这份解码器引用，并把指针置为 NULL
 * @param[in,out] decoderPointer  指向解码器指针的地址；*decoderPointer 为 NULL 时什么都不做
 * @return 无
 * @note 调用前解码线程必须已经退出。还有 AVFrame 没释放时，真正的销毁推迟到最后一帧释放的时候
 */
void mppVideoDecoderRelease(MppVideoDecoder** decoderPointer);
