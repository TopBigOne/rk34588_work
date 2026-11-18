/*
 * EncodeStats 的实现，说明见 encode_stats.h
 */
#include "pipeline/encode_stats.h"

#include <cstdio> // printf

void EncodeStats::stop() {
    encodeSeconds_ = std::chrono::duration<double>(std::chrono::steady_clock::now() - encodeStartTime_).count();
}

void EncodeStats::add_frame(size_t bytes, bool isKeyFrame) {
    encodedFrameCount_++;
    if (isKeyFrame) {
        keyFrameCount_++;
    }
    streamTotalBytes_ += bytes;
}

void EncodeStats::print_summary(const char* streamOutputPath, MppCodingType type, int fps, int targetBps) const {
    const char* codecName     = (type == MPP_VIDEO_CodingAVC) ? "h264" : "h265";
    const double videoSeconds = (double) encodedFrameCount_ / fps;
    printf("\n===== summary =====\n");
    printf("output    : %s (%s)\n", streamOutputPath, codecName);
    printf("frames    : %d (I frames: %d)\n", encodedFrameCount_, keyFrameCount_);
    printf("time      : %.2f s, %.1f fps\n", encodeSeconds_,
        encodeSeconds_ > 0 ? encodedFrameCount_ / encodeSeconds_ : 0.0);
    printf("size      : %zu bytes\n", streamTotalBytes_);
    printf("bitrate   : %.2f Mbps (target %.2f Mbps)\n",
        videoSeconds > 0 ? streamTotalBytes_ * 8 / videoSeconds / 1e6 : 0.0, targetBps / 1e6);
}
