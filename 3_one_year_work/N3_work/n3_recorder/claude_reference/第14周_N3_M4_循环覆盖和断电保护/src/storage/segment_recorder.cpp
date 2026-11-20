#include "storage/segment_recorder.h"

#include <filesystem>
#include <system_error>

#include "common/log.h"
#include "storage/record_path_builder.h"

static const char *const kTag = "SegmentRecorder";
static constexpr int64_t kClockRateHz = 90000;

bool SegmentRecorder::open(const SegmentRecorderConfig &requestedConfig, const VideoTrackParams &trackParams,
                           RecordIndex *requestedRecordIndex, StorageReclaimer *requestedStorageReclaimer) {
    if (requestedConfig.segmentDurationSeconds <= 0) {
        LOGE(kTag, "分段时长 %d 秒不对", requestedConfig.segmentDurationSeconds);
        return false;
    }
    config = requestedConfig;
    recordIndex = requestedRecordIndex;
    storageReclaimer = requestedStorageReclaimer;
    lastClosedSegmentBytes = 0;
    reclaimedSegmentCount = 0;
    currentRecordingId = -1;
    currentTrackParams = trackParams;
    isOpened = true;
    isDiscontinuityPending = false;
    lastFrameTimestamp = -1;
    anchorTimestamp = -1;
    closedSegmentCount = 0;
    writtenFrameCount = 0;
    skippedBeforeKeyFrameCount = 0;
    return true;
}

bool SegmentRecorder::writeFrame(const EncodedFrame &frame) {
    if (!isOpened) {
        LOGE(kTag, "还没 open");
        return false;
    }
    // 第 1 步 时间戳倒退或不动：上游重启过，当作断流
    if (lastFrameTimestamp >= 0 && frame.extendedRtpTimestamp <= lastFrameTimestamp) {
        LOGW(kTag, "时间戳倒退（%lld → %lld），下一个 IDR 开新段", static_cast<long long>(lastFrameTimestamp),
             static_cast<long long>(frame.extendedRtpTimestamp));
        markDiscontinuity();
    }

    // 第 2 步 要不要开新段：还没有段 / 断流过 / 到时间。只在 IDR 处开，不是 IDR 就继续写进旧段（或丢掉）
    const bool hasSegment = mp4Writer.isOpen();
    const int64_t segmentElapsedIn90kHz = frame.extendedRtpTimestamp - segmentStartTimestamp;
    const bool isSegmentDue =
            hasSegment && segmentElapsedIn90kHz >= static_cast<int64_t>(config.segmentDurationSeconds) * kClockRateHz;
    if (frame.isKeyFrame && (!hasSegment || isDiscontinuityPending || isSegmentDue)) {
        if (!startNewSegment(frame)) {
            return false;
        }
    }
    if (!mp4Writer.isOpen() || isDiscontinuityPending) {
        skippedBeforeKeyFrameCount++; // 开头或断流后还没等到 IDR：解不了，丢
        return true;
    }

    // 第 3 步 写进当前段
    lastFrameTimestamp = frame.extendedRtpTimestamp;
    const int64_t ptsIn90kHz = frame.extendedRtpTimestamp - segmentStartTimestamp;
    if (!mp4Writer.writeFrame(frame, ptsIn90kHz)) {
        return false;
    }
    if (lastWrittenPtsIn90kHz >= 0) {
        lastFrameDurationIn90kHz = ptsIn90kHz - lastWrittenPtsIn90kHz;
    }
    lastWrittenPtsIn90kHz = ptsIn90kHz;
    writtenFrameCount++;
    return true;
}

bool SegmentRecorder::startNewSegment(const EncodedFrame &keyFrame) {
    const bool isOk = closeCurrentSegment();
    if (anchorTimestamp < 0 || isDiscontinuityPending) {
        anchorTimestamp = keyFrame.extendedRtpTimestamp;
        anchorWallClockMs = getWallClockMs();
    }
    isDiscontinuityPending = false;
    segmentStartTimestamp = keyFrame.extendedRtpTimestamp;
    segmentStartWallClockMs = anchorWallClockMs + (segmentStartTimestamp - anchorTimestamp) * 1000 / kClockRateHz;

    // 开新段之前腾空间：下一段按上一段的大小估（第一段估 0，只检查剩余空间下限）
    if (storageReclaimer != nullptr) {
        const int deletedCount = storageReclaimer->reclaimUntilEnough(lastClosedSegmentBytes);
        if (deletedCount < 0) {
            return false;
        }
        reclaimedSegmentCount += deletedCount;
    }

    const std::string filePath = buildSegmentFilePath(config.recordRootPath, config.channelIndex, segmentStartWallClockMs);
    if (filePath.empty() || !mp4Writer.open(filePath, currentTrackParams, config.shouldFragment)) {
        return false;
    }
    lastWrittenPtsIn90kHz = -1;
    lastFrameDurationIn90kHz = 0;
    // 先建文件再插记录：插失败只打日志、照常录（录像比索引重要）；M4 的启动修复会把缺的记录补上（推断，M4 定）
    if (recordIndex != nullptr) {
        currentRecordingId = recordIndex->insertRecordingStart(config.channelIndex, segmentStartWallClockMs, filePath);
    }
    return isOk;
}

bool SegmentRecorder::closeCurrentSegment() {
    if (!mp4Writer.isOpen()) {
        return true;
    }
    const std::string filePath = mp4Writer.getFilePath();
    const bool isOk = mp4Writer.close();
    closedSegmentCount++;
    if (recordIndex != nullptr && currentRecordingId >= 0) {
        // 结束时间 = 段首墙上时间 + (最后一帧 PTS + 一帧时长) / 90。段内只用码流时间，不再取系统时间，两种时钟不混用
        const int64_t segmentLengthIn90kHz = lastWrittenPtsIn90kHz >= 0 ? lastWrittenPtsIn90kHz + lastFrameDurationIn90kHz : 0;
        const int64_t endTimeMs = segmentStartWallClockMs + segmentLengthIn90kHz * 1000 / kClockRateHz;
        std::error_code errorCode;
        const auto fileSizeBytes = std::filesystem::file_size(filePath, errorCode);
        lastClosedSegmentBytes = errorCode ? lastClosedSegmentBytes : static_cast<int64_t>(fileSizeBytes);
        recordIndex->finishRecording(currentRecordingId, endTimeMs,
                                     errorCode ? 0 : static_cast<int64_t>(fileSizeBytes));
        currentRecordingId = -1;
    }
    return isOk;
}

void SegmentRecorder::markDiscontinuity() {
    isDiscontinuityPending = true;
    lastFrameTimestamp = -1;
}

void SegmentRecorder::updateTrackParams(const VideoTrackParams &trackParams) { currentTrackParams = trackParams; }

bool SegmentRecorder::close() {
    if (!isOpened) {
        return true;
    }
    isOpened = false;
    return closeCurrentSegment();
}
