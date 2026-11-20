#include "app/m1_remux_to_mp4.h"

#include <atomic>
#include <csignal>
#include <filesystem>
#include <system_error>

#include "app/app_config.h"
#include "common/log.h"
#include "common/time_utils.h"
#include "source/ffmpeg_frame_source.h"
#include "storage/mp4_file_writer.h"

static const char *const kTag = "M1";

static std::atomic<bool> g_isStopRequested{false};

static void handleStopSignal(int) { g_isStopRequested = true; }

// 输出目录不存在就建
static bool createParentDirectory(const std::string &filePath) {
    const std::filesystem::path parentPath = std::filesystem::path(filePath).parent_path();
    if (parentPath.empty()) {
        return true;
    }
    std::error_code errorCode;
    std::filesystem::create_directories(parentPath, errorCode);
    if (errorCode) {
        LOGE(kTag, "建目录 %s 失败：%s", parentPath.c_str(), errorCode.message().c_str());
        return false;
    }
    return true;
}

int runM1RemuxToMp4(const char *inputUrl, const char *outputPath) {
    // Ctrl+C 只置一个标志，主循环看到后正常 close（写 moov），不然普通 MP4 放不了
    std::signal(SIGINT, handleStopSignal);
    std::signal(SIGTERM, handleStopSignal);

    // 第 1 步 打开输入
    FfmpegFrameSource frameSource;
    if (!frameSource.open(inputUrl, kRawStreamFps)) {
        return 1;
    }
    const StreamInfo &streamInfo = frameSource.getStreamInfo();
    const int width = streamInfo.width > 0 ? streamInfo.width : kFallbackWidth;
    const int height = streamInfo.height > 0 ? streamInfo.height : kFallbackHeight;

    // 第 2 步 打开输出
    if (!createParentDirectory(outputPath)) {
        return 1;
    }
    Mp4FileWriter mp4Writer;
    if (!mp4Writer.open(outputPath, buildVideoTrackParams(streamInfo, width, height), false)) {
        return 1;
    }

    // 第 3 步 收一帧写一帧。第一帧不是关键帧就丢（RTSP 中途接入时没有 SPS/PPS 和 IDR，解不了）；
    //        文件里的 PTS 从 0 开始 = 本帧时间戳 - 第一个写入帧的时间戳
    const double startMs = getNowMs();
    const int64_t maxDurationIn90kHz = static_cast<int64_t>(kM1MaxDurationSeconds) * streamInfo.clockRateHz;
    int64_t firstWrittenTimestamp = -1;
    int skippedBeforeKeyFrameCount = 0;
    bool isSourceOk = true;
    EncodedFrame frame;
    while (!g_isStopRequested) {
        const ReceiveResult receiveResult = frameSource.receiveNextFrame(frame);
        if (receiveResult == ReceiveResult::StreamEnded) {
            break;
        }
        if (receiveResult != ReceiveResult::FrameReady) {
            isSourceOk = false;
            break;
        }
        if (firstWrittenTimestamp < 0) {
            if (!frame.isKeyFrame) {
                skippedBeforeKeyFrameCount++;
                continue;
            }
            firstWrittenTimestamp = frame.extendedRtpTimestamp;
        }
        const int64_t ptsIn90kHz = frame.extendedRtpTimestamp - firstWrittenTimestamp;
        if (ptsIn90kHz >= maxDurationIn90kHz) {
            break;
        }
        if (!mp4Writer.writeFrame(frame, ptsIn90kHz)) {
            isSourceOk = false;
            break;
        }
    }

    // 第 4 步 收尾：close 写 moov
    const bool isCloseOk = mp4Writer.close();
    const double elapsedMs = getNowMs() - startMs;
    LOGI(kTag, "写了 %d 帧（开头丢掉 %d 个非关键帧），码流 %lld 字节，用时 %.0f ms%s", mp4Writer.getWrittenFrameCount(),
         skippedBeforeKeyFrameCount, static_cast<long long>(mp4Writer.getWrittenBytes()), elapsedMs,
         g_isStopRequested ? "（收到停止信号）" : "");
    return isSourceOk && isCloseOk && mp4Writer.getWrittenFrameCount() > 0 ? 0 : 1;
}
