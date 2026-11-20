#pragma once

#include <atomic>
#include <functional>
#include <thread>

#include <rockchip/rk_mpi.h>

#include "common/detection_types.h"
#include "common/mpp_utils.h"
#include "npu/object_detector.h"
#include "pipeline/frame_queue.h"
#include "pipeline/latest_slot.h"

using LatestFrameSlot = LatestSlot<MppFramePtr>;

// 回调在检测线程里调用，同时拿到帧和结果：可以直接在这一帧上画框（帧在回调返回后才释放）
using DetectedFrameCallback = std::function<void(MppFrame decodedFrame, const DetectionResult &detectionResult)>;

// 检测线程：取帧 → detector.detect → callback。
//   M1 文件输入：FrameQueue（满了解码线程等着，不丢帧）
//   M2 实时输入：LatestFrameSlot（只留最新一帧，检测跟不上就丢旧帧）
class DetectStage {
public:
    DetectStage() = default;
    ~DetectStage();
    DetectStage(const DetectStage &) = delete;
    DetectStage &operator=(const DetectStage &) = delete;

    // 输入、detector 要活得比这个对象久
    bool start(FrameQueue &inputQueue, ObjectDetector &detector, DetectedFrameCallback callback);
    bool start(LatestFrameSlot &inputSlot, ObjectDetector &detector, DetectedFrameCallback callback);
    void join();
    int getDetectedFrameCount() const { return detectedFrameCount.load(); }
    bool hasFailed() const { return isFailed.load(); }

private:
    using TakeFrameFunction = std::function<bool(MppFramePtr &framePtr)>;  // false = 输入结束
    using AbortInputFunction = std::function<void()>;
    bool startThread(TakeFrameFunction takeFrame, AbortInputFunction abortInput, ObjectDetector &detector,
                     DetectedFrameCallback callback);
    void runDetectLoop(TakeFrameFunction takeFrame, AbortInputFunction abortInput, ObjectDetector &detector,
                       DetectedFrameCallback callback);

    std::thread detectThread;
    std::atomic<int> detectedFrameCount{0};
    std::atomic<bool> isFailed{false};
};
