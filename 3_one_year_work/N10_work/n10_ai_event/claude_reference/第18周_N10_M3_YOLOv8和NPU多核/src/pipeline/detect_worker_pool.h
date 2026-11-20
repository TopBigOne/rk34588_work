#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <rknn/rknn_api.h>

#include "common/detection_types.h"
#include "npu/object_detector.h"
#include "pipeline/frame_queue.h"

// 多个检测线程从同一个 FrameQueue 取帧，每个线程一个 ObjectDetector（各自的 rknn context + RGA buffer）。
// 第 1 个 detector 用 rknn_init 加载，其余用 rknn_dup_context 共享权重；每个 context 设自己的 core mask。
// 结果按取帧顺序（= 解码顺序）排好后再回调，回调一次只进一个（在某个检测线程里调用，不用自己加锁）。
class DetectWorkerPool {
public:
    DetectWorkerPool() = default;
    ~DetectWorkerPool();
    DetectWorkerPool(const DetectWorkerPool &) = delete;
    DetectWorkerPool &operator=(const DetectWorkerPool &) = delete;

    // 一个元素 = 一个 worker（一个 context + 一个线程）。例：{CORE_0}、{CORE_0_1_2}、{CORE_0, CORE_1, CORE_2}
    bool init(const char *requestedModelPath, const ObjectDetectorConfig &requestedConfig,
              const std::vector<rknn_core_mask> &workerCoreMasks);
    bool start(FrameQueue &requestedInputQueue, DetectionCallback requestedCallback);  // inputQueue 要活得更久
    void join();  // 等所有线程退出：生产者 closeInput 之后，队列取空线程就退出

    int getWorkerCount() const { return static_cast<int>(detectors.size()); }
    int getProcessedFrameCount() const;
    int getProcessedFrameCount(int workerIndex) const;
    double getProcessedFramesPerSecond() const;  // 处理帧数 / （start 到最后一帧交出去）
    size_t getMaxPendingResultCount() const;     // 为了按顺序交出，最多压过几个结果（慢的那个 worker 拖住了后面的）
    bool hasFailed() const { return isFailed.load(); }

private:
    void runWorkerLoop(int workerIndex);
    bool popNextFrame(MppFramePtr &framePtr, int64_t &sequenceIndex);
    void deliverInOrder(int workerIndex, int64_t sequenceIndex, DetectionResult &&detectionResult);

    std::vector<std::unique_ptr<ObjectDetector>> detectors;  // [0] 是源，析构时最后销毁
    std::vector<std::thread> workerThreads;
    FrameQueue *inputQueue = nullptr;
    DetectionCallback callback;
    std::atomic<bool> isFailed{false};

    std::mutex popMutex;  // 取帧和分配序号要一起做，否则序号和解码顺序对不上
    int64_t nextPopSequenceIndex = 0;

    mutable std::mutex deliverMutex;
    int64_t nextDeliverSequenceIndex = 0;
    std::map<int64_t, DetectionResult> pendingResults;  // 先做完、但前面还有没交出的
    size_t maxPendingResultCount = 0;
    std::vector<int> processedFrameCounts;  // 每个 worker 处理了几帧
    double startMs = 0.0;
    double lastDeliverMs = 0.0;
};
