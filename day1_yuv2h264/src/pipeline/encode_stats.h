/*
 * EncodeStats：编码统计工具类（帧数、I 帧数、总字节数、用时），最后打印汇总
 *
 * 用法：
 *   EncodeStats stats;
 *   stats.add_header(header.size());   // 文件头也算进总字节数
 *   stats.start();                     // 开始计时
 *   while (...) { ...; stats.add_frame(packet.data.size(), packet.isKeyFrame); }
 *   stats.stop();                      // 停止计时
 *   stats.print_summary(outputPath, type, fps, bps);
 */
#pragma once

#include <chrono> // std::chrono：统计编码用时
#include <cstddef> // size_t
#include <rockchip/rk_mpi.h>

class EncodeStats {
public:
    /**
     * 记录文件头（SPS/PPS）的字节数：只累加总字节数，不算帧
     * 总字节数算上文件头，才和输出文件的大小一致
     * @param bytes  文件头的字节数
     */
    void add_header(size_t bytes) { streamTotalBytes_ += bytes; }

    /**
     * 开始计时。在编码循环开始前调用
     * 统计的用时包含读文件的时间，不是纯硬件编码时间
     */
    void start() { encodeStartTime_ = std::chrono::steady_clock::now(); }

    /**
     * 停止计时，算出从 start() 到现在的用时。在编码循环结束后调用
     */
    void stop();

    /**
     * 记录编出的一帧：帧数 +1，是 I 帧的话 I 帧数 +1，总字节数累加
     * @param bytes       这一帧码流的字节数
     * @param isKeyFrame  是不是 I 帧（来自 EncodedPacket::isKeyFrame）
     */
    void add_frame(size_t bytes, bool isKeyFrame);

    /**
     * 获取已经编出的帧数（编码循环里打印 "frame N" 用）
     * @return  到目前为止 add_frame 被调用的次数
     */
    RK_S32 frame_count() const { return encodedFrameCount_; }

    /**
     * 打印汇总：输出文件、帧数、用时、文件大小、实际码率
     * 实际码率 = 总字节数 × 8 / 视频时长（帧数 / 帧率）
     * @param streamOutputPath  输出文件路径（只用来打印）
     * @param type              编码格式，打印成 h264 / h265
     * @param fps               帧率，用来算视频时长 → 实际码率
     * @param targetBps         目标码率（bit/s），和实际码率对比
     */
    void print_summary(const char* streamOutputPath, MppCodingType type, int fps, int targetBps) const;

private:
    RK_S32 encodedFrameCount_ = 0; // 编出了多少帧
    RK_S32 keyFrameCount_     = 0; // I帧数量
    size_t streamTotalBytes_  = 0; // 码流总字节数（含开头的 SPS/PPS，和输出文件大小一致）
    double encodeSeconds_     = 0; // 编码用时
    std::chrono::steady_clock::time_point encodeStartTime_;
};
