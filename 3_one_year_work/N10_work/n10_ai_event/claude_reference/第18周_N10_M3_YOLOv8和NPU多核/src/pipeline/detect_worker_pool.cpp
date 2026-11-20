#include "pipeline/detect_worker_pool.h"

#include <algorithm>
#include <utility>

#include "common/log.h"
#include "common/time_utils.h"

static const char *const kTag = "DetectWorkerPool";

DetectWorkerPool::~DetectWorkerPool() {
    if (inputQueue != nullptr) {
        inputQueue->abort();  // 生产者忘了 closeInput 时也别卡在析构里
    }
    join();
    // 复制出来的 context 先销毁，源 context 最后（推断：权重归源 context，先销毁源可能出问题）
    while (!detectors.empty()) {
        detectors.pop_back();
    }
}

bool DetectWorkerPool::init(const char *requestedModelPath, const ObjectDetectorConfig &requestedConfig,
                            const std::vector<rknn_core_mask> &workerCoreMasks) {
    if (!detectors.empty() || workerCoreMasks.empty()) {
        LOGE(kTag, "已经 init 过，或 worker 数为 0");
        return false;
    }
    for (size_t workerIndex = 0; workerIndex < workerCoreMasks.size(); workerIndex++) {
        auto detectorPtr = std::make_unique<ObjectDetector>();
        bool isInitOk = workerIndex == 0 ? detectorPtr->init(requestedModelPath, requestedConfig)
                                         : detectorPtr->initSharingModel(*detectors[0]);
        if (!isInitOk || !detectorPtr->setNpuCoreMask(workerCoreMasks[workerIndex])) {
            LOGE(kTag, "worker %zu 初始化失败", workerIndex);
            return false;
        }
        LOGI(kTag, "worker %zu：%s，core mask 0x%x", workerIndex, workerIndex == 0 ? "rknn_init" : "rknn_dup_context",
             static_cast<unsigned>(workerCoreMasks[workerIndex]));
        detectors.push_back(std::move(detectorPtr));
    }
    processedFrameCounts.assign(detectors.size(), 0);
    return true;
}

bool DetectWorkerPool::start(FrameQueue &requestedInputQueue, DetectionCallback requestedCallback) {
    if (detectors.empty() || !workerThreads.empty()) {
        LOGE(kTag, "还没 init，或已经启动过了");
        return false;
    }
    inputQueue = &requestedInputQueue;
    callback = std::move(requestedCallback);
    startMs = getNowMs();
    lastDeliverMs = startMs;
    for (int workerIndex = 0; workerIndex < getWorkerCount(); workerIndex++) {
        workerThreads.emplace_back(&DetectWorkerPool::runWorkerLoop, this, workerIndex);
    }
    return true;
}

void DetectWorkerPool::join() {
    for (std::thread &workerThread : workerThreads) {
        if (workerThread.joinable()) {
            workerThread.join();
        }
    }
}

bool DetectWorkerPool::popNextFrame(MppFramePtr &framePtr, int64_t &sequenceIndex) {
    std::lock_guard<std::mutex> lock(popMutex);
    if (inputQueue->pop(framePtr) != PopResult::Frame) {
        return false;
    }
    sequenceIndex = nextPopSequenceIndex++;
    return true;
}

void DetectWorkerPool::runWorkerLoop(int workerIndex) {
    ObjectDetector &detector = *detectors[workerIndex];
    while (true) {
        MppFramePtr framePtr;
        int64_t sequenceIndex = 0;
        if (!popNextFrame(framePtr, sequenceIndex)) {
            return;
        }
        DetectionResult detectionResult;
        if (!detector.detect(framePtr.get(), detectionResult)) {
            LOGE(kTag, "worker %d 检测第 %lld 帧失败，叫停", workerIndex, static_cast<long long>(sequenceIndex));
            isFailed = true;
            inputQueue->abort();
            return;
        }
        framePtr.reset();  // 结果里不带帧：马上还给解码器，不因为等排序多占 buffer
        deliverInOrder(workerIndex, sequenceIndex, std::move(detectionResult));
    }
}

void DetectWorkerPool::deliverInOrder(int workerIndex, int64_t sequenceIndex, DetectionResult &&detectionResult) {
    std::lock_guard<std::mutex> lock(deliverMutex);
    processedFrameCounts[workerIndex]++;
    pendingResults.emplace(sequenceIndex, std::move(detectionResult));
    maxPendingResultCount = std::max(maxPendingResultCount, pendingResults.size() - 1);
    // 前面的都交出去了才能交这一个；一次可能连着交出好几个
    auto firstPendingIt = pendingResults.begin();
    while (firstPendingIt != pendingResults.end() && firstPendingIt->first == nextDeliverSequenceIndex) {
        if (callback) {
            callback(firstPendingIt->second);
        }
        nextDeliverSequenceIndex++;
        firstPendingIt = pendingResults.erase(firstPendingIt);
    }
    lastDeliverMs = getNowMs();
}

int DetectWorkerPool::getProcessedFrameCount() const {
    std::lock_guard<std::mutex> lock(deliverMutex);
    int totalCount = 0;
    for (int frameCount : processedFrameCounts) {
        totalCount += frameCount;
    }
    return totalCount;
}

int DetectWorkerPool::getProcessedFrameCount(int workerIndex) const {
    std::lock_guard<std::mutex> lock(deliverMutex);
    return workerIndex >= 0 && workerIndex < static_cast<int>(processedFrameCounts.size())
                   ? processedFrameCounts[workerIndex]
                   : 0;
}

double DetectWorkerPool::getProcessedFramesPerSecond() const {
    int processedFrameCount = getProcessedFrameCount();
    std::lock_guard<std::mutex> lock(deliverMutex);
    double elapsedMs = lastDeliverMs - startMs;
    return elapsedMs > 0.0 ? processedFrameCount * 1000.0 / elapsedMs : 0.0;
}

size_t DetectWorkerPool::getMaxPendingResultCount() const {
    std::lock_guard<std::mutex> lock(deliverMutex);
    return maxPendingResultCount;
}
