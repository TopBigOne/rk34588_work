#include "event/event_recorder.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <vector>

#include "common/log.h"

static const char *const kTag = "EventRecorder";

static int64_t convert90kHzToUs(int64_t timestampIn90kHz) {
    return timestampIn90kHz * 100 / 9;  // × 1000000 / 90000
}

static int64_t convertUsTo90kHz(int64_t durationUs) {
    return durationUs * 9 / 100;
}

static int64_t getWallClockMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

static int64_t getSteadyClockUs() {
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

bool EventRecorder::init(const EventRecorderConfig &requestedConfig, const StreamInfo &streamInfo,
                         EventIndex *requestedIndex) {
    config = requestedConfig;
    eventIndex = requestedIndex;
    stateMachine = EventStateMachine(config.stateConfig);
    preRecordBuffer = PreRecordBuffer(convertUsTo90kHz(config.preRecordDurationUs));
    // 宽高为 0（拿不到）时按 1080p 写进 MP4 头，和 N3 的约定一样
    int width = streamInfo.width > 0 ? streamInfo.width : 1920;
    int height = streamInfo.height > 0 ? streamInfo.height : 1080;
    trackParams = buildVideoTrackParams(streamInfo, width, height);
    if (trackParams.extradataAnnexb.empty()) {
        LOGW(kTag, "没有 SPS/PPS，MP4 头会缺 extradata（推断：播放器可能打不开）");
    }
    std::error_code errorCode;
    std::filesystem::create_directories(config.eventDirectory + "/ch" + std::to_string(config.channelIndex),
                                        errorCode);
    if (errorCode) {
        LOGE(kTag, "建目录失败：%s", errorCode.message().c_str());
        return false;
    }
    return true;
}

std::string EventRecorder::buildEventFilePath(int64_t startTimeMs) const {
    time_t startTimeSeconds = static_cast<time_t>(startTimeMs / 1000);
    struct tm localTime;
    localtime_r(&startTimeSeconds, &localTime);
    char fileName[32];
    strftime(fileName, sizeof(fileName), "%Y%m%d_%H%M%S", &localTime);
    std::string basePath = config.eventDirectory + "/ch" + std::to_string(config.channelIndex) + "/" + fileName;
    std::string filePath = basePath + ".mp4";
    // 同一秒内开了两个事件（比如强制切开后马上又开始）：加 _1、_2
    for (int suffixIndex = 1; std::filesystem::exists(filePath); suffixIndex++) {
        filePath = basePath + "_" + std::to_string(suffixIndex) + ".mp4";
    }
    return filePath;
}

bool EventRecorder::openEventFile(int64_t currentPtsIn90kHz) {
    std::vector<EncodedFrame> bufferedFrames = preRecordBuffer.takeAll();
    if (bufferedFrames.empty()) {
        return false;  // 还没收到过关键帧，等下一帧再试
    }
    fileFirstPtsIn90kHz = bufferedFrames.front().extendedRtpTimestamp;
    // 文件第一帧是“缓冲时长”之前的画面，墙钟时间往前推这么多
    eventStartTimeMs = getWallClockMs() - (currentPtsIn90kHz - fileFirstPtsIn90kHz) / 90;
    std::string filePath = buildEventFilePath(eventStartTimeMs);
    if (!fileWriter.open(filePath, trackParams, config.shouldFragmentMp4)) {
        LOGE(kTag, "打开事件文件失败：%s", filePath.c_str());
        return false;
    }
    for (const EncodedFrame &bufferedFrame : bufferedFrames) {
        fileWriter.writeFrame(bufferedFrame, bufferedFrame.extendedRtpTimestamp - fileFirstPtsIn90kHz);
    }
    lastEventFilePath = filePath;
    LOGI(kTag, "事件开始：%s，先写预录 %zu 帧（%.1f 秒）", filePath.c_str(), bufferedFrames.size(),
         (currentPtsIn90kHz - fileFirstPtsIn90kHz) / 90000.0);
    return true;
}

void EventRecorder::finishEvent(const char *reason) {
    int64_t fileDurationMs = (lastFramePtsIn90kHz - fileFirstPtsIn90kHz) / 90;
    int writtenFrameCount = fileWriter.getWrittenFrameCount();
    fileWriter.close();

    EventRecord eventRecord;
    eventRecord.channelIndex = config.channelIndex;
    eventRecord.startTimeMs = eventStartTimeMs;
    // 结束时间按文件内容算（开始 + 文件时长），不取现在的墙钟：文件输入不按实时速度放时两者会差很多
    eventRecord.endTimeMs = eventStartTimeMs + fileDurationMs;
    eventRecord.maxPersonCount = stateMachine.getMaxPersonCount();
    eventRecord.filePath = lastEventFilePath;
    int64_t eventId = eventIndex != nullptr ? eventIndex->insertEvent(eventRecord) : 0;

    LOGI(kTag, "事件结束（%s）：%s，%d 帧，%.1f 秒，最多 %d 人，记录 id %lld", reason, lastEventFilePath.c_str(),
         writtenFrameCount, fileDurationMs / 1000.0, eventRecord.maxPersonCount, static_cast<long long>(eventId));
    stateMachine.markEventClosed();
    finishedEventCount++;
}

void EventRecorder::onEncodedFrame(const EncodedFrame &frame) {
    int64_t callStartUs = getSteadyClockUs();
    std::lock_guard<std::mutex> lock(mutex);
    lastFramePtsIn90kHz = frame.extendedRtpTimestamp;
    // 每一帧都进预录缓冲：事件期间也进，这样事件刚结束又来人时，新事件的“前 10 秒”也是全的
    preRecordBuffer.pushFrame(frame);

    if (fileWriter.isOpen()) {
        if (stateMachine.shouldCloseEvent(convert90kHzToUs(frame.extendedRtpTimestamp))) {
            finishEvent(stateMachine.getState() == EventState::PostRoll ? "人消失超过收尾时间" : "达到最长时长");
        } else {
            fileWriter.writeFrame(frame, frame.extendedRtpTimestamp - fileFirstPtsIn90kHz);
        }
    } else if (stateMachine.isEventActive()) {
        if (stateMachine.shouldCloseEvent(convert90kHzToUs(frame.extendedRtpTimestamp))) {
            stateMachine.markEventClosed();  // 一直没打开文件（等不到关键帧或打开失败），事件作废，别卡在这个状态
        } else {
            openEventFile(frame.extendedRtpTimestamp);  // 缓冲里已经有当前这一帧，打开后一起写进去
        }
    }
    maxOnFrameDurationUs = std::max(maxOnFrameDurationUs, getSteadyClockUs() - callStartUs);
}

void EventRecorder::onDetectionResult(const DetectionResult &detectionResult) {
    int personCount = static_cast<int>(std::count_if(
            detectionResult.objects.begin(), detectionResult.objects.end(),
            [](const DetectedObject &object) { return object.classIndex == kPersonClassIndex; }));
    std::lock_guard<std::mutex> lock(mutex);
    EventState oldState = stateMachine.getState();
    EventState newState = stateMachine.onDetectionResult(personCount, detectionResult.ptsUs);
    if (newState != oldState) {
        LOGI(kTag, "状态 %s → %s（pts %.2f 秒，person %d）", getEventStateName(oldState), getEventStateName(newState),
             detectionResult.ptsUs / 1e6, personCount);
    }
}

void EventRecorder::closeOpenEvent() {
    std::lock_guard<std::mutex> lock(mutex);
    if (fileWriter.isOpen()) {
        finishEvent("程序退出");
    } else if (stateMachine.isEventActive()) {
        stateMachine.markEventClosed();  // 确认了事件但还没等到关键帧，没有文件
    }
}

int EventRecorder::getFinishedEventCount() const {
    std::lock_guard<std::mutex> lock(mutex);
    return finishedEventCount;
}

EventState EventRecorder::getState() const {
    std::lock_guard<std::mutex> lock(mutex);
    return stateMachine.getState();
}

int64_t EventRecorder::getMaxOnFrameDurationUs() const {
    std::lock_guard<std::mutex> lock(mutex);
    return maxOnFrameDurationUs;
}
