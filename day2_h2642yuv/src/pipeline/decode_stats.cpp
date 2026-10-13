#include "pipeline/decode_stats.h"

#include <cstdio> // printf

void DecodeStats::stop() {
    seconds_ = std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime_).count();
}

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
