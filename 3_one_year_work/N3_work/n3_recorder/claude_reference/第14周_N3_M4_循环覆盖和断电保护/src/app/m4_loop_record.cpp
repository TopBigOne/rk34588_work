#include "app/m4_loop_record.h"

#include <atomic>
#include <csignal>
#include <filesystem>
#include <string>
#include <system_error>

#include "app/app_config.h"
#include "common/log.h"
#include "common/time_utils.h"
#include "source/ffmpeg_frame_source.h"
#include "storage/index_repairer.h"
#include "storage/record_index.h"
#include "storage/segment_recorder.h"
#include "storage/storage_reclaimer.h"

static const char *const kTag = "M4";

static std::atomic<bool> g_isStopRequested{false};

static void handleStopSignal(int) { g_isStopRequested = true; }

static bool openIndexAndRepair(const char *recordRootPath, RecordIndex &recordIndex) {
    std::error_code errorCode;
    std::filesystem::create_directories(recordRootPath, errorCode);
    if (!recordIndex.open((std::filesystem::path(recordRootPath) / kRecordIndexFileName).string())) {
        return false;
    }
    const int repairedCount = repairUnfinishedRecordings(recordIndex);
    LOGI(kTag, "启动修复：修了 %d 条", repairedCount);
    return repairedCount >= 0;
}

int runM4Repair(const char *recordRootPath) {
    RecordIndex recordIndex;
    return openIndexAndRepair(recordRootPath, recordIndex) ? 0 : 1;
}

int runM4LoopRecord(const char *inputUrl, const char *recordRootPath, int totalDurationSeconds,
                    int segmentDurationSeconds, int quotaMegabytes, bool shouldPaceRawFile) {
    std::signal(SIGINT, handleStopSignal);
    std::signal(SIGTERM, handleStopSignal);

    // 第 1 步 索引 + 启动修复（上次 kill -9 留下的 NULL），再按配额先清一遍
    RecordIndex recordIndex;
    if (!openIndexAndRepair(recordRootPath, recordIndex)) {
        return 1;
    }
    StorageQuota quota;
    quota.minFreeBytes = kM4MinFreeBytes;
    quota.maxTotalRecordingBytes = static_cast<int64_t>(quotaMegabytes) * 1024 * 1024;
    StorageReclaimer storageReclaimer(recordIndex, quota, recordRootPath);
    if (storageReclaimer.reclaimUntilEnough(0) < 0) {
        return 1;
    }

    // 第 2 步 输入：裸流循环读，可选按实时速度
    FfmpegFrameSource frameSource;
    if (!frameSource.open(inputUrl, kRawStreamFps)) {
        return 1;
    }
    frameSource.setShouldLoopRawFile(true);
    frameSource.setShouldPaceRawFile(shouldPaceRawFile);
    const StreamInfo &streamInfo = frameSource.getStreamInfo();
    const int width = streamInfo.width > 0 ? streamInfo.width : kFallbackWidth;
    const int height = streamInfo.height > 0 ? streamInfo.height : kFallbackHeight;

    // 第 3 步 分段录像：fMP4，每段开头前回收空间
    SegmentRecorder segmentRecorder;
    SegmentRecorderConfig recorderConfig;
    recorderConfig.recordRootPath = recordRootPath;
    recorderConfig.channelIndex = kM2ChannelIndex;
    recorderConfig.segmentDurationSeconds = segmentDurationSeconds;
    recorderConfig.shouldFragment = true;
    if (!segmentRecorder.open(recorderConfig, buildVideoTrackParams(streamInfo, width, height), &recordIndex,
                              &storageReclaimer)) {
        return 1;
    }

    // 第 4 步 收一帧写一帧
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

    // 第 5 步 收尾
    const bool isCloseOk = segmentRecorder.close();
    LOGI(kTag, "收到 %d 帧，写入 %d 帧，%d 个文件，回收删掉 %d 段（%lld 字节），索引里剩 %lld 字节，用时 %.0f ms%s",
         receivedFrameCount, segmentRecorder.getWrittenFrameCount(), segmentRecorder.getClosedSegmentCount(),
         segmentRecorder.getReclaimedSegmentCount(), static_cast<long long>(storageReclaimer.getDeletedBytes()),
         static_cast<long long>(recordIndex.sumRecordingSizeBytes()), getNowMs() - startMs,
         g_isStopRequested ? "（收到停止信号）" : "");
    return isOk && isCloseOk ? 0 : 1;
}
