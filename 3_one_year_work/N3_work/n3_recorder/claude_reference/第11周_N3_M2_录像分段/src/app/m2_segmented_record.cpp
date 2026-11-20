#include "app/m2_segmented_record.h"

#include <atomic>
#include <csignal>

#include "app/app_config.h"
#include "common/log.h"
#include "common/time_utils.h"
#include "source/ffmpeg_frame_source.h"
#include "storage/segment_recorder.h"

static const char *const kTag = "M2";

static std::atomic<bool> g_isStopRequested{false};

static void handleStopSignal(int) { g_isStopRequested = true; }

int runM2SegmentedRecord(const char *inputUrl, const char *recordRootPath, int totalDurationSeconds,
                         int segmentDurationSeconds) {
    std::signal(SIGINT, handleStopSignal);
    std::signal(SIGTERM, handleStopSignal);

    // 第 1 步 打开输入；裸流文件循环读
    FfmpegFrameSource frameSource;
    if (!frameSource.open(inputUrl, kRawStreamFps)) {
        return 1;
    }
    frameSource.setShouldLoopRawFile(true);
    const StreamInfo &streamInfo = frameSource.getStreamInfo();
    const int width = streamInfo.width > 0 ? streamInfo.width : kFallbackWidth;
    const int height = streamInfo.height > 0 ? streamInfo.height : kFallbackHeight;

    // 第 2 步 打开分段录像
    SegmentRecorder segmentRecorder;
    SegmentRecorderConfig recorderConfig;
    recorderConfig.recordRootPath = recordRootPath;
    recorderConfig.channelIndex = kM2ChannelIndex;
    recorderConfig.segmentDurationSeconds = segmentDurationSeconds;
    recorderConfig.shouldFragment = false;
    if (!segmentRecorder.open(recorderConfig, buildVideoTrackParams(streamInfo, width, height))) {
        return 1;
    }

    // 第 3 步 收一帧写一帧，按码流时间录够 totalDurationSeconds
    const double startMs = getNowMs();
    const int64_t totalDurationIn90kHz = static_cast<int64_t>(totalDurationSeconds) * streamInfo.clockRateHz;
    int64_t firstTimestamp = -1;
    int receivedFrameCount = 0;
    bool isOk = true;
    EncodedFrame frame;
    while (!g_isStopRequested) {
        const ReceiveResult receiveResult = frameSource.receiveNextFrame(frame);
        if (receiveResult == ReceiveResult::StreamEnded) {
            break;
        }
        if (receiveResult != ReceiveResult::FrameReady) {
            isOk = false;
            break;
        }
        if (firstTimestamp < 0) {
            firstTimestamp = frame.extendedRtpTimestamp;
        }
        if (frame.extendedRtpTimestamp - firstTimestamp >= totalDurationIn90kHz) {
            break;
        }
        receivedFrameCount++;
        if (!segmentRecorder.writeFrame(frame)) {
            isOk = false;
            break;
        }
    }

    // 第 4 步 关掉最后一段
    const bool isCloseOk = segmentRecorder.close();
    LOGI(kTag, "收到 %d 帧（输入循环 %d 次），开头等 IDR 丢掉 %d 帧，写入 %d 帧，%d 个文件，用时 %.0f ms%s",
         receivedFrameCount, frameSource.getLoopCount(), segmentRecorder.getSkippedBeforeKeyFrameCount(),
         segmentRecorder.getWrittenFrameCount(), segmentRecorder.getClosedSegmentCount(), getNowMs() - startMs,
         g_isStopRequested ? "（收到停止信号）" : "");
    return isOk && isCloseOk ? 0 : 1;
}
