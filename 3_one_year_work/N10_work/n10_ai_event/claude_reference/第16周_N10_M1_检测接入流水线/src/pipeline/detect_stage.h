#pragma once

#include <atomic>
#include <thread>

#include "common/detection_types.h"
#include "npu/object_detector.h"
#include "pipeline/frame_queue.h"

// 检测线程：从 FrameQueue 取帧 → detector.detect → callback（在检测线程里调用）。
// M1 文件输入用 FrameQueue（满了解码线程等着，不丢帧）；M2 实时输入换成 LatestFrameSlot。
class DetectStage {
public:
    DetectStage() = default;
    ~DetectStage();
    DetectStage(const DetectStage &) = delete;
    DetectStage &operator=(const DetectStage &) = delete;

    // inputQueue、detector 要活得比这个对象久
    bool start(FrameQueue &inputQueue, ObjectDetector &detector, DetectionCallback callback);
    void join();
    int getDetectedFrameCount() const { return detectedFrameCount.load(); }
    bool hasFailed() const { return isFailed.load(); }

private:
    void runDetectLoop(FrameQueue &inputQueue, ObjectDetector &detector, DetectionCallback callback);

    std::thread detectThread;
    std::atomic<int> detectedFrameCount{0};
    std::atomic<bool> isFailed{false};
};
