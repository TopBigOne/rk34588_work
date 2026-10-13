// 【扫一眼】计数加 printf
#include "pipeline/decode_stats.h"

#include <cstdio> // printf

/**
 * @brief 停止计时，算出从 start() 到现在的秒数
 * @note 【扫一眼】
 */
void DecodeStats::stop() {
    seconds_ = std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime_).count();
}

/**
 * @brief 打印汇总：输入、输出、帧数、错误帧、info change 次数、用时、fps、送包重试次数
 * @param streamInputPath  输入文件路径（只用来打印）
 * @param yuvOutputPath    输出文件路径；nullptr 打印成"(不写文件)"
 * @param infoChangeCount  info change 次数（MppDecoder::info_change_count()）
 * @note 【扫一眼】
 */
void DecodeStats::print_summary(const char* streamInputPath, const char* yuvOutputPath, RK_S32 infoChangeCount) const {
    printf("\n===== summary =====\n");
    printf("    input     : %s\n", streamInputPath);
    printf("    output    : %s\n", yuvOutputPath ? yuvOutputPath : "(不写文件)");
    printf("    frames    : %d (error/discard: %d, info change: %d)\n", decodedFrameCount_, errorFrameCount_,
        infoChangeCount);
    printf("    time      : %.2f s, %.1f fps\n", seconds_, seconds_ > 0 ? decodedFrameCount_ / seconds_ : 0.0);
    printf("    put retry : %ld 次（队列满）\n", putRetryCount_);
    printf("\n===============================\n");
}
