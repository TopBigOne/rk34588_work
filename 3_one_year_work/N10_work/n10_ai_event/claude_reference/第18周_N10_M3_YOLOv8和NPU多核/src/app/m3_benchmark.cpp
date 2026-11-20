#include "app/m3_benchmark.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "app/app_config.h"
#include "app/image_input.h"
#include "app/post_process_comparison.h"
#include "common/log.h"
#include "common/time_utils.h"
#include "npu/nms.h"
#include "npu/object_detector.h"
#include "npu/rknn_model.h"
#include "npu/yolov5_post_process.h"
#include "npu/yolov8_post_process.h"
#include "pipeline/decode_stage.h"
#include "pipeline/detect_worker_pool.h"
#include "pipeline/frame_queue.h"
#include "third_party/rknn_model_zoo_yolov8/yolov8_demo_decode.h"

static const char *const kTag = "M3";

struct BenchmarkResult {
    bool isOk = false;
    int frameCount = 0;
    double framesPerSecond = 0.0;
    int countedFrameCount = 0;  // 算进平均耗时的帧（去掉每个 worker 的第 1 帧预热）
    double averagePreprocessMs = 0.0;
    double averageInferenceMs = 0.0;
    double averagePostprocessMs = 0.0;
    int personFrameCount = 0;
    std::vector<int> workerFrameCounts;
    size_t maxPendingResultCount = 0;
};

static ObjectDetectorConfig makeDetectorConfig(YoloVersion yoloVersion) {
    ObjectDetectorConfig detectorConfig;
    detectorConfig.yoloVersion = yoloVersion;
    detectorConfig.boxThreshold = kBoxThreshold;
    detectorConfig.nmsThreshold = kNmsThreshold;
    detectorConfig.letterboxPadValue = kLetterboxPadValue;
    return detectorConfig;
}

static YoloVersion parseYoloVersion(const char *yoloVersionText) {
    return yoloVersionText != nullptr && strcmp(yoloVersionText, "v8") == 0 ? YoloVersion::V8 : YoloVersion::V5;
}

// 只解码不检测：看解码本身每秒能出几帧。检测的吞吐要是接近它，瓶颈就可能在解码而不是 NPU
static double measureDecodeOnlyFramesPerSecond(const char *streamPath, int maxFrameCount) {
    DecodeStage decodeStage;
    if (!decodeStage.open(streamPath, MPP_VIDEO_CodingAVC, "decode_only")) {
        return 0.0;
    }
    double startMs = getNowMs();
    int frameCount = 0;
    while (frameCount < maxFrameCount) {
        MppFramePtr decodedFramePtr;
        if (decodeStage.readNextFrame(decodedFramePtr) != ReadFrameResult::Frame) {
            break;
        }
        frameCount++;
    }
    double elapsedMs = getNowMs() - startMs;
    return elapsedMs > 0.0 ? frameCount * 1000.0 / elapsedMs : 0.0;
}

static BenchmarkResult runDetectBenchmark(const char *streamPath, const char *modelPath, YoloVersion yoloVersion,
                                          const std::vector<rknn_core_mask> &workerCoreMasks, int maxFrameCount) {
    BenchmarkResult benchmarkResult;
    // 声明顺序：队列先于 pool，pool 析构时队列还在
    FrameQueue frameQueue(kBenchmarkQueueCapacityFrameCount);
    DetectWorkerPool workerPool;
    if (!workerPool.init(modelPath, makeDetectorConfig(yoloVersion), workerCoreMasks)) {
        return benchmarkResult;
    }
    DecodeStage decodeStage;
    if (!decodeStage.open(streamPath, MPP_VIDEO_CodingAVC, "ch0")) {
        return benchmarkResult;
    }
    decodeStage.setPtsFrameRate(kInputFramesPerSecond);

    // 回调一次只进一个（pool 保证），直接累加
    int deliveredFrameIndex = 0;
    int64_t totalPreprocessUs = 0;
    int64_t totalInferenceUs = 0;
    int64_t totalPostprocessUs = 0;
    const int warmupFrameCount = static_cast<int>(workerCoreMasks.size());  // 每个 context 第 1 次 run 慢，前 N 帧不算
    workerPool.start(frameQueue, [&](const DetectionResult &detectionResult) {
        if (std::any_of(detectionResult.objects.begin(), detectionResult.objects.end(),
                        [](const DetectedObject &object) { return object.classIndex == kPersonClassIndex; })) {
            benchmarkResult.personFrameCount++;
        }
        if (deliveredFrameIndex >= warmupFrameCount) {
            benchmarkResult.countedFrameCount++;
            totalPreprocessUs += detectionResult.timing.preprocessUs;
            totalInferenceUs += detectionResult.timing.inferenceUs;
            totalPostprocessUs += detectionResult.timing.postprocessUs;
        }
        deliveredFrameIndex++;
    });

    // 主线程解码，队列满了就等（文件输入，不丢帧）
    int pushedFrameCount = 0;
    while (pushedFrameCount < maxFrameCount) {
        MppFramePtr decodedFramePtr;
        if (decodeStage.readNextFrame(decodedFramePtr) != ReadFrameResult::Frame) {
            break;
        }
        if (!frameQueue.push(std::move(decodedFramePtr))) {
            break;
        }
        pushedFrameCount++;
    }
    frameQueue.closeInput();
    workerPool.join();

    benchmarkResult.isOk = !workerPool.hasFailed() && deliveredFrameIndex == pushedFrameCount;
    benchmarkResult.frameCount = deliveredFrameIndex;
    benchmarkResult.framesPerSecond = workerPool.getProcessedFramesPerSecond();
    if (benchmarkResult.countedFrameCount > 0) {
        double countedFrameCount = benchmarkResult.countedFrameCount;
        benchmarkResult.averagePreprocessMs = totalPreprocessUs / 1000.0 / countedFrameCount;
        benchmarkResult.averageInferenceMs = totalInferenceUs / 1000.0 / countedFrameCount;
        benchmarkResult.averagePostprocessMs = totalPostprocessUs / 1000.0 / countedFrameCount;
    }
    for (int workerIndex = 0; workerIndex < workerPool.getWorkerCount(); workerIndex++) {
        benchmarkResult.workerFrameCounts.push_back(workerPool.getProcessedFrameCount(workerIndex));
    }
    benchmarkResult.maxPendingResultCount = workerPool.getMaxPendingResultCount();
    return benchmarkResult;
}

static std::string formatWorkerFrameCounts(const BenchmarkResult &benchmarkResult) {
    std::string text;
    for (size_t workerIndex = 0; workerIndex < benchmarkResult.workerFrameCounts.size(); workerIndex++) {
        text += (workerIndex == 0 ? "" : " / ") + std::to_string(benchmarkResult.workerFrameCounts[workerIndex]);
    }
    return text;
}

int runM3NpuCoreBenchmark(const char *requestedStreamPath, const char *requestedModelPath, const char *yoloVersionText) {
    const char *streamPath = requestedStreamPath != nullptr ? requestedStreamPath : kDefaultStreamPath;
    YoloVersion yoloVersion = parseYoloVersion(yoloVersionText);
    const char *defaultModelPath = yoloVersion == YoloVersion::V8 ? kDefaultYolov8ModelPath : kDefaultModelPath;
    const char *modelPath = requestedModelPath != nullptr ? requestedModelPath : defaultModelPath;
    LOGI(kTag, "输入 %s，模型 %s（%s），每种跑法 %d 帧", streamPath, modelPath,
         yoloVersion == YoloVersion::V8 ? "YOLOv8" : "YOLOv5", kBenchmarkFrameCount);

    struct CoreUsage {
        const char *name;
        std::vector<rknn_core_mask> workerCoreMasks;
    };
    const CoreUsage coreUsages[] = {
            {"1 个 context，绑核 0", {RKNN_NPU_CORE_0}},
            {"1 个 context，三核合用", {RKNN_NPU_CORE_0_1_2}},
            {"3 个 context，各绑一核", {RKNN_NPU_CORE_0, RKNN_NPU_CORE_1, RKNN_NPU_CORE_2}},
    };

    double decodeOnlyFramesPerSecond = measureDecodeOnlyFramesPerSecond(streamPath, kBenchmarkFrameCount);
    std::vector<BenchmarkResult> benchmarkResults;
    for (const CoreUsage &coreUsage : coreUsages) {
        LOGI(kTag, "---- %s ----", coreUsage.name);
        benchmarkResults.push_back(
                runDetectBenchmark(streamPath, modelPath, yoloVersion, coreUsage.workerCoreMasks, kBenchmarkFrameCount));
    }

    LOGI(kTag, "表 2：多核用法（%d 帧；单帧耗时 = 前处理 + 推理 + 后处理的平均）", kBenchmarkFrameCount);
    LOGI(kTag, "  只解码不检测                   %6.1f 帧/秒（检测吞吐接近它时，瓶颈可能在解码）", decodeOnlyFramesPerSecond);
    bool isAllOk = true;
    for (size_t usageIndex = 0; usageIndex < benchmarkResults.size(); usageIndex++) {
        const BenchmarkResult &benchmarkResult = benchmarkResults[usageIndex];
        isAllOk = isAllOk && benchmarkResult.isOk;
        LOGI(kTag, "  %-28s %6.1f 帧/秒  单帧 %6.2f ms（推理 %6.2f）  各 worker 帧数 %s  排序最多压 %zu  %s",
             coreUsages[usageIndex].name, benchmarkResult.framesPerSecond,
             benchmarkResult.averagePreprocessMs + benchmarkResult.averageInferenceMs +
                     benchmarkResult.averagePostprocessMs,
             benchmarkResult.averageInferenceMs, formatWorkerFrameCounts(benchmarkResult).c_str(),
             benchmarkResult.maxPendingResultCount, benchmarkResult.isOk ? "" : "（失败）");
    }
    return isAllOk ? 0 : 1;
}

int runM3CompareModels(const char *requestedStreamPath, const char *requestedV5ModelPath,
                       const char *requestedV8ModelPath) {
    const char *streamPath = requestedStreamPath != nullptr ? requestedStreamPath : kDefaultStreamPath;
    const char *modelPaths[2] = {requestedV5ModelPath != nullptr ? requestedV5ModelPath : kDefaultModelPath,
                                 requestedV8ModelPath != nullptr ? requestedV8ModelPath : kDefaultYolov8ModelPath};
    const YoloVersion yoloVersions[2] = {YoloVersion::V5, YoloVersion::V8};
    const char *versionNames[2] = {"YOLOv5", "YOLOv8"};

    BenchmarkResult benchmarkResults[2];
    for (int modelIndex = 0; modelIndex < 2; modelIndex++) {
        LOGI(kTag, "---- %s：%s ----", versionNames[modelIndex], modelPaths[modelIndex]);
        benchmarkResults[modelIndex] = runDetectBenchmark(streamPath, modelPaths[modelIndex], yoloVersions[modelIndex],
                                                          {RKNN_NPU_CORE_0}, kBenchmarkFrameCount);
    }
    LOGI(kTag, "表 1：YOLOv5 vs YOLOv8（%s，1 个 context 绑核 0，%d 帧）", streamPath, kBenchmarkFrameCount);
    for (int modelIndex = 0; modelIndex < 2; modelIndex++) {
        const BenchmarkResult &benchmarkResult = benchmarkResults[modelIndex];
        LOGI(kTag, "  %s  推理 %6.2f ms  后处理 %6.2f ms  前处理 %5.2f ms  检出 person 的帧 %d / %d  %s",
             versionNames[modelIndex], benchmarkResult.averageInferenceMs, benchmarkResult.averagePostprocessMs,
             benchmarkResult.averagePreprocessMs, benchmarkResult.personFrameCount, benchmarkResult.frameCount,
             benchmarkResult.isOk ? "" : "（失败）");
    }
    LOGI(kTag, "“有人的总帧数”要人工数（或拿两个模型都检出的帧数近似），程序不知道真值");
    return benchmarkResults[0].isOk && benchmarkResults[1].isOk ? 0 : 1;
}

// 跑一张图，输出留在 model 里（调用方负责 releaseOutputs）
static bool runModelOnImage(RknnModel &model, const char *imagePath, ImageModelInput &imageInput) {
    return loadImageAsModelInput(imagePath, model.getInputWidth(), model.getInputHeight(), imageInput) &&
           model.runWithRgbInput(imageInput.modelInputRgb.data(), imageInput.modelInputRgb.size());
}

static void printDetectedObjects(const char *prefix, const std::vector<DetectedObject> &detectedObjects) {
    LOGI(kTag, "%s：%zu 个", prefix, detectedObjects.size());
    for (const DetectedObject &object : detectedObjects) {
        LOGI(kTag, "    class=%2d%s %.3f (%d, %d, %d, %d)", object.classIndex,
             object.classIndex == kPersonClassIndex ? "(person)" : "", object.confidence, object.box.left,
             object.box.top, object.box.right, object.box.bottom);
    }
}

int runM3CompareYolov8PostProcess(const char *requestedModelPath, const char *requestedImagePath) {
    const char *modelPath = requestedModelPath != nullptr ? requestedModelPath : kDefaultYolov8ModelPath;
    const char *imagePath = requestedImagePath != nullptr ? requestedImagePath : kDefaultImagePath;
    RknnModel model;
    ImageModelInput imageInput;
    if (!model.load(modelPath)) {
        return 1;
    }
    model.printTensorAttrs();
    if (!runModelOnImage(model, imagePath, imageInput)) {
        return 1;
    }
    std::vector<Yolov8OutputBranch> outputBranches;
    std::vector<CandidateObject> ownCandidates;
    std::vector<CandidateObject> demoCandidates;
    std::vector<DetectedObject> detectedObjects;
    bool isOk = ObjectDetector::buildYolov8OutputBranches(model, outputBranches) &&
                decodeYolov8OutputBranches(outputBranches, kBoxThreshold, ownCandidates) &&
                runDemoYolov8Decode(outputBranches, kBoxThreshold, demoCandidates) &&
                runYolov8PostProcess(outputBranches, imageInput.letterboxInfo, imageInput.sourceWidth,
                                     imageInput.sourceHeight, kBoxThreshold, kNmsThreshold, detectedObjects);
    model.releaseOutputs();
    if (!isOk) {
        LOGE(kTag, "后处理失败");
        return 1;
    }
    printDetectedObjects("自己的 YOLOv8 后处理（NMS 之后，原图坐标）", detectedObjects);
    PostProcessComparison comparison =
            compareCandidateObjects(ownCandidates, demoCandidates, kCandidateCompareTolerancePixels);
    LOGI(kTag, "NMS 之前：自己 %d 个候选，model_zoo %d 个，配上 %d 个，最大误差 ≤ %d 像素 → %s", comparison.ownObjectCount,
         comparison.demoObjectCount, comparison.matchedObjectCount, comparison.maxCoordinateDiffPixels,
         comparison.isSame ? "一致" : "不一致");
    return comparison.isSame ? 0 : 1;
}

static long readResidentMemoryKb() {
    FILE *statusFile = fopen("/proc/self/status", "r");
    if (statusFile == nullptr) {
        return -1;
    }
    char line[256];
    long residentKb = -1;
    while (fgets(line, sizeof(line), statusFile) != nullptr) {
        if (sscanf(line, "VmRSS: %ld kB", &residentKb) == 1) {
            break;
        }
    }
    fclose(statusFile);
    return residentKb;
}

int runM3FaultInjection(const char *faultName, const char *requestedModelPath, const char *requestedImagePath) {
    const char *fault = faultName != nullptr ? faultName : "none";
    const char *modelPath = requestedModelPath != nullptr ? requestedModelPath : kDefaultModelPath;
    const char *imagePath = requestedImagePath != nullptr ? requestedImagePath : kDefaultImagePath;
    LOGI(kTag, "故障注入 %s，模型 %s（YOLOv5），图片 %s", fault, modelPath, imagePath);
    RknnModel model;
    ImageModelInput imageInput;
    if (!model.load(modelPath)) {
        return 1;
    }

    if (strcmp(fault, "norelease") == 0) {
        // 每次 run 之后不调 rknn_outputs_release，看内存和 run 本身有什么变化（推断：内存一直涨）
        for (int runIndex = 0; runIndex < kFaultReleaseLoopCount; runIndex++) {
            if (!runModelOnImage(model, imagePath, imageInput)) {
                LOGE(kTag, "第 %d 次 run 失败", runIndex);
                return 1;
            }
            model.forgetOutputsWithoutReleaseForFaultInjection();
            if (runIndex % 50 == 0 || runIndex == kFaultReleaseLoopCount - 1) {
                LOGI(kTag, "第 %3d 次  VmRSS %ld kB", runIndex, readResidentMemoryKb());
            }
        }
        return 0;
    }

    if (!runModelOnImage(model, imagePath, imageInput)) {
        return 1;
    }
    std::vector<Yolov5OutputTensor> outputTensors;
    if (!ObjectDetector::buildYolov5OutputTensors(model, outputTensors)) {
        model.releaseOutputs();
        return 1;
    }
    if (strcmp(fault, "swap") == 0) {
        // 像 demo 那样“按下标认 stride”：第 0 个当 stride 8、第 2 个当 stride 32，但两份数据对调了
        std::swap(outputTensors[0], outputTensors[2]);
        outputTensors[0].stridePixels = 8;
        outputTensors[2].stridePixels = 32;
        LOGW(kTag, "注意：真把输出顺序弄反时，buildYolov5OutputTensors 的 stride 检查会直接报错；这里绕过检查看现象");
    } else if (strcmp(fault, "zp") == 0) {
        for (Yolov5OutputTensor &tensor : outputTensors) {
            tensor.quantInfo.zeroPoint = 0;  // 忘了减 zeroPoint：(int8 - 0) × scale
        }
    } else if (strcmp(fault, "none") != 0) {
        LOGE(kTag, "不认识的故障：%s（none / swap / zp / norelease）", fault);
        model.releaseOutputs();
        return 1;
    }
    std::vector<DetectedObject> detectedObjects;
    bool isOk = runYolov5PostProcess(outputTensors, imageInput.letterboxInfo, imageInput.sourceWidth,
                                     imageInput.sourceHeight, kBoxThreshold, kNmsThreshold, detectedObjects);
    model.releaseOutputs();
    if (!isOk) {
        LOGE(kTag, "后处理失败");
        return 1;
    }
    printDetectedObjects(fault, detectedObjects);
    return 0;
}
