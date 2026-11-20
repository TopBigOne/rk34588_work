#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

#include "common/encoded_frame.h"

// 最近一段时间的码流（不是解码后的帧：1080p 一帧码流几十 KB，NV12 一帧 3 MB）。
// 规则：
// 1. 永远从关键帧开始：还没见过关键帧时，非关键帧直接丢掉（录像文件第一帧必须是 IDR，否则开头解不出来）
// 2. 按整个 GOP 从头淘汰：只要“第二个关键帧到最新一帧”已经够 keepDuration，就把第一个 GOP 整个丢掉。
//    所以缓冲的时长在 [keepDuration, keepDuration + 1 个 GOP) 之间（不够 keepDuration 时就是全部）
class PreRecordBuffer {
public:
    explicit PreRecordBuffer(int64_t requestedKeepDurationIn90kHz);

    void pushFrame(const EncodedFrame &frame);
    std::vector<EncodedFrame> takeAll();  // 事件开始时一次取走，缓冲清空（之后从下一个关键帧重新攒）
    void clear();

    int64_t getBufferedDurationIn90kHz() const;  // 最新一帧 - 最早一帧
    size_t getFrameCount() const { return frames.size(); }
    size_t getBufferedBytes() const { return bufferedBytes; }
    bool startsWithKeyFrame() const { return !frames.empty() && frames.front().isKeyFrame; }

private:
    void dropOldGops();

    int64_t keepDurationIn90kHz;
    std::deque<EncodedFrame> frames;
    size_t bufferedBytes = 0;
};
