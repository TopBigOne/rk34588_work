#pragma once

#include <cstdint>
#include <string>

#include "common/encoded_frame.h"
#include "storage/mp4_file_writer.h"

struct SegmentRecorderConfig {
    std::string recordRootPath;      // 例 /userdata/n3_rec
    int channelIndex = 0;            // 0～3
    int segmentDurationSeconds = 60; // 测试 60，产品 600
    bool shouldFragment = false;     // M4 起为 true
};

// 一路录像：按时长在 IDR 处切成一个个 MP4。每个文件第一帧 PTS = 0（给 N4 的约定，计划第 7 节）
class SegmentRecorder {
public:
    bool open(const SegmentRecorderConfig &requestedConfig, const VideoTrackParams &trackParams);
    // 到时间且是 IDR → 切段；第一帧不是 IDR 就丢，直到 IDR。返回 false = 写盘出错
    bool writeFrame(const EncodedFrame &frame);
    void markDiscontinuity(); // 上游重连 / 时间戳倒退：下一个 IDR 强制开新段
    void updateTrackParams(const VideoTrackParams &trackParams); // 下一段起用新的 SPS/PPS
    bool close();

    int getClosedSegmentCount() const { return closedSegmentCount; }
    int getWrittenFrameCount() const { return writtenFrameCount; }
    int getSkippedBeforeKeyFrameCount() const { return skippedBeforeKeyFrameCount; }

private:
    bool startNewSegment(const EncodedFrame &keyFrame);
    bool closeCurrentSegment();

    SegmentRecorderConfig config;
    VideoTrackParams currentTrackParams;
    Mp4FileWriter mp4Writer;
    bool isOpened = false;
    bool isDiscontinuityPending = false;
    int64_t segmentStartTimestamp = 0;     // 段首帧的 extendedRtpTimestamp
    int64_t segmentStartWallClockMs = 0;   // 段首帧对应的墙上时间
    int64_t lastFrameTimestamp = -1;
    // 墙上时间的锚点：第一帧（或断流后第一帧）时取一次系统时间，之后的段首时间 = 锚点 + 码流时间差。
    // 这样输入比实时快（读文件）时文件名也不会撞，而且文件名时间和 PTS 对得上
    int64_t anchorTimestamp = -1;
    int64_t anchorWallClockMs = 0;
    int closedSegmentCount = 0;
    int writtenFrameCount = 0;
    int skippedBeforeKeyFrameCount = 0;
};
