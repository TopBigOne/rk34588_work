#include "pipeline/detect_stage.h"

#include <utility>

#include "common/log.h"

static const char *const kTag = "DetectStage";

DetectStage::~DetectStage() {
    join();
}

bool DetectStage::start(FrameQueue &inputQueue, ObjectDetector &detector, DetectionCallback callback) {
    if (detectThread.joinable()) {
        LOGE(kTag, "已经启动过了");
        return false;
    }
    detectThread = std::thread(&DetectStage::runDetectLoop, this, std::ref(inputQueue), std::ref(detector),
                               std::move(callback));
    return true;
}

void DetectStage::join() {
    if (detectThread.joinable()) {
        detectThread.join();
    }
}

void DetectStage::runDetectLoop(FrameQueue &inputQueue, ObjectDetector &detector, DetectionCallback callback) {
    DetectionResult detectionResult;
    while (true) {
        MppFramePtr framePtr;
        PopResult popResult = inputQueue.pop(framePtr);
        if (popResult == PopResult::EndOfStream) {
            LOGI(kTag, "输入结束，共检测 %d 帧", detectedFrameCount.load());
            return;
        }
        if (popResult == PopResult::Aborted) {
            LOGW(kTag, "队列被叫停");
            return;
        }
        if (!detector.detect(framePtr.get(), detectionResult)) {
            LOGE(kTag, "第 %d 帧检测失败，叫停整个流程", detectedFrameCount.load());
            isFailed = true;
            inputQueue.abort();  // 让解码线程的 push 返回 false，别一直等
            return;
        }
        detectedFrameCount++;
        if (callback) {
            callback(detectionResult);
        }
        // framePtr 在这里析构：buffer 还给解码器的内存池
    }
}
