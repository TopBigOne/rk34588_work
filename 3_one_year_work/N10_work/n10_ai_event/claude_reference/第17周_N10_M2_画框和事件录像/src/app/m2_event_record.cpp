#include "app/m2_event_record.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <string>
#include <thread>

#include "app/app_config.h"
#include "common/log.h"
#include "common/time_utils.h"
#include "decoder/mpp_decoder.h"
#include "event/event_index.h"
#include "event/event_recorder.h"
#include "npu/object_detector.h"
#include "overlay/nv12_frame_overlay.h"
#include "pipeline/detect_stage.h"
#include "source/ffmpeg_frame_source.h"

static const char *const kTag = "M2";

static std::atomic<bool> g_shouldStop{false};

static void handleStopSignal(int) {
    g_shouldStop = true;  // 信号处理函数里只改一个原子变量，别的事回主循环做
}

static MppCodingType convertToMppCodingType(VideoCodec codec) {
    return codec == VideoCodec::H265 ? MPP_VIDEO_CodingHEVC : MPP_VIDEO_CodingAVC;
}

// 把解码器里现在能取的帧都取出来放进槽（槽只留最新一帧）。false = 解码器出错
static bool drainDecodedFrames(MppDecoder &decoder, LatestFrameSlot &frameSlot) {
    while (true) {
        MppFramePtr framePtr;
        GetFrameResult getResult = decoder.getFrame(framePtr);
        if (getResult == GetFrameResult::None) {
            return true;
        }
        if (getResult == GetFrameResult::Error) {
            return false;
        }
        MppFrame rawFrame = framePtr.get();
        // 只带 EOS 的空帧、出错或被丢弃的帧不送检测
        if (mpp_frame_get_buffer(rawFrame) == nullptr || mpp_frame_get_errinfo(rawFrame) != 0 ||
            mpp_frame_get_discard(rawFrame) != 0) {
            continue;
        }
        frameSlot.put(std::move(framePtr));
    }
}

// 送一帧码流；解码器输入队列满了就先取帧腾地方再重送
static bool feedOneFrame(MppDecoder &decoder, LatestFrameSlot &frameSlot, const EncodedFrame &frame, int64_t ptsUs) {
    constexpr int kMaxFullRetryCount = 1000;
    for (int retryIndex = 0; retryIndex < kMaxFullRetryCount; retryIndex++) {
        FeedResult feedResult = decoder.feed(frame.annexBData.data(), frame.annexBData.size(), false, ptsUs);
        if (feedResult == FeedResult::Error) {
            return false;
        }
        if (!drainDecodedFrames(decoder, frameSlot)) {
            return false;
        }
        if (feedResult == FeedResult::Sent) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    LOGE(kTag, "解码器输入一直是满的");
    return false;
}

int runM2EventRecord(const char *requestedInputUrl, const char *requestedModelPath, int requestedLoopCount) {
    const std::string inputUrl = requestedInputUrl != nullptr ? requestedInputUrl : kDefaultStreamPath;
    const char *modelPath = requestedModelPath != nullptr ? requestedModelPath : kDefaultModelPath;
    const int loopCount = std::max(1, requestedLoopCount);

    // 第 1 步 打开输入。裸流文件按帧率实时放（不然 30 秒的文件 1 秒就读完，检测只能看到零星几帧）
    FfmpegFrameSource frameSource;
    if (!frameSource.open(inputUrl, kInputFramesPerSecond)) {
        return 1;
    }
    if (frameSource.isRawStreamFile()) {
        frameSource.setShouldPaceRawFile(true);
        frameSource.setShouldLoopRawFile(loopCount > 1);
    }
    const StreamInfo &streamInfo = frameSource.getStreamInfo();
    LOGI(kTag, "输入 %s：%s %dx%d，文件放 %d 遍", inputUrl.c_str(), streamInfo.codec == VideoCodec::H265 ? "H.265" : "H.264",
         streamInfo.width, streamInfo.height, loopCount);

    // 第 2 步 事件录像（索引库 + 录像器）、检测器、解码器
    EventIndex eventIndex;
    if (!eventIndex.open(kEventDatabasePath)) {
        return 1;
    }
    EventRecorderConfig recorderConfig;
    recorderConfig.channelIndex = 0;
    recorderConfig.eventDirectory = kEventDirectory;
    recorderConfig.preRecordDurationUs = kPreRecordDurationUs;
    recorderConfig.stateConfig.startConfirmDetectionCount = kStartConfirmDetectionCount;
    recorderConfig.stateConfig.postRollDurationUs = kPostRollDurationUs;
    EventRecorder eventRecorder;
    if (!eventRecorder.init(recorderConfig, streamInfo, &eventIndex)) {
        return 1;
    }
    ObjectDetectorConfig detectorConfig;
    detectorConfig.boxThreshold = kBoxThreshold;
    detectorConfig.nmsThreshold = kNmsThreshold;
    detectorConfig.letterboxPadValue = kLetterboxPadValue;
    ObjectDetector detector;
    if (!detector.init(modelPath, detectorConfig)) {
        return 1;
    }
    MppDecoder decoder;
    if (!decoder.init(convertToMppCodingType(streamInfo.codec), "ch0")) {
        return 1;
    }

    // 第 3 步 检测线程：检测 → 画框 → 状态机；偶尔存一帧画好框的画面
    LatestFrameSlot frameSlot;
    std::atomic<int> personFrameCount{0};
    int dumpedFrameCount = 0;  // 只在检测线程里用
    DetectStage detectStage;
    detectStage.start(frameSlot, detector, [&](MppFrame decodedFrame, const DetectionResult &detectionResult) {
        int drawnBoxCount = drawDetectionBoxesOnFrame(decodedFrame, detectionResult.objects, kPersonClassIndex,
                                                      kNv12Green, kBoxLineWidthPixels);
        if (drawnBoxCount > 0) {
            int personFrameIndex = personFrameCount++;
            if (dumpedFrameCount < kDumpBoxedFrameCount && personFrameIndex % kDumpEveryNthPersonFrame == 0) {
                char dumpPath[128];
                snprintf(dumpPath, sizeof(dumpPath), kDumpBoxedPathPattern, dumpedFrameCount,
                         detectionResult.sourceWidth, detectionResult.sourceHeight);
                if (saveFrameAsCompactNv12(decodedFrame, dumpPath)) {
                    LOGI(kTag, "画好框的帧存到 %s（%d 个框）", dumpPath, drawnBoxCount);
                    dumpedFrameCount++;
                }
            }
        }
        eventRecorder.onDetectionResult(detectionResult);
    });

    // 第 4 步 接收循环（主线程）：码流先给录像器，再送解码
    signal(SIGINT, handleStopSignal);
    signal(SIGTERM, handleStopSignal);
    int receivedFrameCount = 0;
    int lastPrintDetectedCount = 0;
    double lastPrintMs = getNowMs();
    bool isOk = true;
    while (!g_shouldStop && !detectStage.hasFailed()) {
        EncodedFrame frame;
        ReceiveResult receiveResult = frameSource.receiveNextFrame(frame);
        if (receiveResult == ReceiveResult::StreamEnded) {
            LOGI(kTag, "输入结束");
            break;
        }
        if (receiveResult == ReceiveResult::Timeout) {
            continue;
        }
        if (receiveResult == ReceiveResult::Error) {
            isOk = false;
            break;
        }
        if (frameSource.getLoopCount() >= loopCount) {
            LOGI(kTag, "文件放完 %d 遍", loopCount);
            break;
        }
        receivedFrameCount++;
        eventRecorder.onEncodedFrame(frame);
        int64_t ptsUs = frame.extendedRtpTimestamp * 100 / 9;  // 90 kHz → 微秒
        if (!feedOneFrame(decoder, frameSlot, frame, ptsUs)) {
            isOk = false;
            break;
        }

        double nowMs = getNowMs();
        if (nowMs - lastPrintMs >= 1000) {
            int detectedCount = detectStage.getDetectedFrameCount();
            LOGI(kTag, "收到 %d 帧 | 检测 %.1f 帧/秒 | 槽里丢掉 %d 帧 | 状态 %s | 已完成事件 %d | onEncodedFrame 最长 %lld us",
                 receivedFrameCount, (detectedCount - lastPrintDetectedCount) * 1000.0 / (nowMs - lastPrintMs),
                 frameSlot.getDroppedCount(), getEventStateName(eventRecorder.getState()),
                 eventRecorder.getFinishedEventCount(),
                 static_cast<long long>(eventRecorder.getMaxOnFrameDurationUs()));
            lastPrintDetectedCount = detectedCount;
            lastPrintMs = nowMs;
        }
    }

    // 第 5 步 收尾：先停检测（不会再有结果改状态），再把正在录的事件写完
    frameSlot.close();
    detectStage.join();
    eventRecorder.closeOpenEvent();
    frameSource.close();

    LOGI(kTag, "收到 %d 帧，检测 %d 帧，槽里丢掉 %d 帧；完成事件 %d 个；onEncodedFrame 最长 %lld us", receivedFrameCount,
         detectStage.getDetectedFrameCount(), frameSlot.getDroppedCount(), eventRecorder.getFinishedEventCount(),
         static_cast<long long>(eventRecorder.getMaxOnFrameDurationUs()));
    for (const EventRecord &eventRecord : eventIndex.listEvents(0)) {
        LOGI(kTag, "events #%lld：%lld ～ %lld（%.1f 秒），最多 %d 人，%s", static_cast<long long>(eventRecord.eventId),
             static_cast<long long>(eventRecord.startTimeMs), static_cast<long long>(eventRecord.endTimeMs),
             (eventRecord.endTimeMs - eventRecord.startTimeMs) / 1000.0, eventRecord.maxPersonCount,
             eventRecord.filePath.c_str());
    }
    return (isOk && !detectStage.hasFailed()) ? 0 : 1;
}
