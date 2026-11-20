#include "event/pre_record_buffer.h"

#include <iterator>

PreRecordBuffer::PreRecordBuffer(int64_t requestedKeepDurationIn90kHz)
    : keepDurationIn90kHz(requestedKeepDurationIn90kHz) {}

void PreRecordBuffer::pushFrame(const EncodedFrame &frame) {
    if (frames.empty() && !frame.isKeyFrame) {
        return;  // 规则 1
    }
    frames.push_back(frame);
    bufferedBytes += frame.annexBData.size();
    dropOldGops();
}

void PreRecordBuffer::dropOldGops() {
    while (true) {
        // 找第二个 GOP 的开头（下标 0 之后的第一个关键帧）
        size_t secondGopStartIndex = 0;
        for (size_t frameIndex = 1; frameIndex < frames.size(); frameIndex++) {
            if (frames[frameIndex].isKeyFrame) {
                secondGopStartIndex = frameIndex;
                break;
            }
        }
        if (secondGopStartIndex == 0) {
            return;  // 只有一个 GOP，不能丢
        }
        int64_t durationFromSecondGop =
                frames.back().extendedRtpTimestamp - frames[secondGopStartIndex].extendedRtpTimestamp;
        if (durationFromSecondGop < keepDurationIn90kHz) {
            return;  // 丢了第一个 GOP 就不够了
        }
        for (size_t frameIndex = 0; frameIndex < secondGopStartIndex; frameIndex++) {
            bufferedBytes -= frames.front().annexBData.size();
            frames.pop_front();
        }
    }
}

std::vector<EncodedFrame> PreRecordBuffer::takeAll() {
    std::vector<EncodedFrame> takenFrames(std::make_move_iterator(frames.begin()),
                                          std::make_move_iterator(frames.end()));
    clear();
    return takenFrames;
}

void PreRecordBuffer::clear() {
    frames.clear();
    bufferedBytes = 0;
}

int64_t PreRecordBuffer::getBufferedDurationIn90kHz() const {
    if (frames.empty()) {
        return 0;
    }
    return frames.back().extendedRtpTimestamp - frames.front().extendedRtpTimestamp;
}
