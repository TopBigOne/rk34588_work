// 【扫一眼】计数加 printf
/*
 * DecodeStats：解码统计工具类（帧数、错误帧、送包重试次数、用时），最后打印汇总
 *
 * 用法：
 *   DecodeStats stats;
 *   stats.start();                       // 开始计时
 *   while (...) { ...; stats.add_frame(); }
 *   stats.stop();                        // 停止计时
 *   stats.print_summary(input, output, decoder.info_change_count());
 */
#pragma once

#include <chrono> // std::chrono：统计解码用时
#include <rockchip/rk_mpi.h>

class DecodeStats {
public:
    /**
     * @brief 开始计时。在解码循环开始前调用
     * @note 用时包含读文件、写文件的时间，不是纯硬件解码时间
     */
    void start() { startTime_ = std::chrono::steady_clock::now(); }

    /**
     * @brief 停止计时，算出从 start() 到现在的用时。在解码循环结束后调用
     */
    void stop();

    /** @brief 记录解出一帧正常图像 */
    void add_frame() { decodedFrameCount_++; }

    /** @brief 记录一帧 errinfo / discard 的帧（跳过不写） */
    void add_error() { errorFrameCount_++; }

    /** @brief 记录一次送包重试（decode_put_packet 因为输入队列满返回了 MPP_ERR_BUFFER_FULL） */
    void add_put_retry() { putRetryCount_++; }

    /**
     * @brief 获取已经解出的正常图像帧数
     * @return 到目前为止 add_frame 被调用的次数（判断 -n 帧数上限、打印错误帧序号用）
     */
    RK_S32 frame_count() const { return decodedFrameCount_; }

    /**
     * @brief 打印汇总：输入、输出、帧数、错误帧、info change 次数、用时、fps、送包重试次数
     * @param streamInputPath  输入文件路径（只用来打印）
     * @param yuvOutputPath    输出文件路径，nullptr 表示没写文件
     * @param infoChangeCount  info change 次数（MppDecoder::info_change_count()）
     */
    void print_summary(const char* streamInputPath, const char* yuvOutputPath, RK_S32 infoChangeCount) const;

private:
    RK_S32 decodedFrameCount_ = 0; // 解出了多少帧正常图像
    RK_S32 errorFrameCount_   = 0; // errinfo / discard 的帧数
    long putRetryCount_       = 0; // 送包时队列满、重试的次数
    double seconds_           = 0; // 解码用时
    std::chrono::steady_clock::time_point startTime_;
};
