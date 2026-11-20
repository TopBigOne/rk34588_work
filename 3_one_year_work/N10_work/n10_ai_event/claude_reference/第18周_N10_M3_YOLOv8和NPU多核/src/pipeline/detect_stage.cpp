#include "pipeline/detect_stage.h"

#include <utility>

#include "common/log.h"

static const char *const kTag = "DetectStage";

DetectStage::~DetectStage() {
    join();
}

bool DetectStage::start(FrameQueue &inputQueue, ObjectDetector &detector, DetectedFrameCallback callback) {
    return startThread(
            [&inputQueue](MppFramePtr &framePtr) {
                PopResult popResult = inputQueue.pop(framePtr);
                if (popResult == PopResult::Aborted) {
                    LOGW(kTag, "队列被叫停");
                }
                return popResult == PopResult::Frame;
            },
            [&inputQueue] { inputQueue.abort(); }, detector, std::move(callback));
}

bool DetectStage::start(LatestFrameSlot &inputSlot, ObjectDetector &detector, DetectedFrameCallback callback) {
    return startThread([&inputSlot](MppFramePtr &framePtr) { return inputSlot.take(framePtr); },
                       [&inputSlot] { inputSlot.close(); }, detector, std::move(callback));
}

bool DetectStage::startThread(TakeFrameFunction takeFrame, AbortInputFunction abortInput, ObjectDetector &detector,
                              DetectedFrameCallback callback) {
    if (detectThread.joinable()) {
        LOGE(kTag, "已经启动过了");
        return false;
    }
    detectThread = std::thread(&DetectStage::runDetectLoop, this, std::move(takeFrame), std::move(abortInput),
                               std::ref(detector), std::move(callback));
    return true;
}

void DetectStage::join() {
    if (detectThread.joinable()) {
        detectThread.join();
    }
}

void DetectStage::runDetectLoop(TakeFrameFunction takeFrame, AbortInputFunction abortInput, ObjectDetector &detector,
                                DetectedFrameCallback callback) {
    DetectionResult detectionResult;
    while (true) {
        MppFramePtr framePtr;
        if (!takeFrame(framePtr)) {
            LOGI(kTag, "输入结束，共检测 %d 帧", detectedFrameCount.load());
            return;
        }
        if (!detector.detect(framePtr.get(), detectionResult)) {
            LOGE(kTag, "第 %d 帧检测失败，叫停整个流程", detectedFrameCount.load());
            isFailed = true;
            abortInput();  // 让生产者别一直等
            return;
        }
        detectedFrameCount++;
        if (callback) {
            callback(framePtr.get(), detectionResult);
        }
        // framePtr 在这里析构：buffer 还给解码器的内存池
    }
}
