#include "app/m1_detect_file.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "app/app_config.h"
#include "app/image_input.h"
#include "app/post_process_comparison.h"
#include "common/log.h"
#include "common/time_utils.h"
#include "npu/object_detector.h"
#include "npu/rknn_model.h"
#include "pipeline/decode_stage.h"
#include "pipeline/detect_stage.h"
#include "pipeline/frame_queue.h"
#include "third_party/rknn_yolov5_demo/postprocess.h"

static const char *const kTag = "M1";

// 分段耗时统计（只在检测线程里写，主线程 join 之后才读，不用加锁）
struct StageTimingSummary {
    int countedFrameCount = 0;
    int64_t firstFrameTotalUs = 0;
    int64_t totalPreprocessUs = 0;
    int64_t totalInferenceUs = 0;
    int64_t totalPostprocessUs = 0;
    int64_t maxFrameTotalUs = 0;
    int personFrameCount = 0;  // 至少检出 1 个 person 的帧数（全部帧）
    int totalFrameCount = 0;

    void addFrame(const DetectionResult &detectionResult, int frameIndex) {
        const DetectionTiming &timing = detectionResult.timing;
        int64_t frameTotalUs = timing.preprocessUs + timing.inferenceUs + timing.postprocessUs;
        totalFrameCount++;
        if (std::any_of(detectionResult.objects.begin(), detectionResult.objects.end(),
                        [](const DetectedObject &object) { return object.classIndex == kPersonClassIndex; })) {
            personFrameCount++;
        }
        if (frameIndex == 0) {
            firstFrameTotalUs = frameTotalUs;  // 第一帧含 RKNN 预热和 RGA 第一次初始化，不算进平均
            return;
        }
        if (countedFrameCount >= kTimingSummaryFrameCount) {
            return;
        }
        countedFrameCount++;
        totalPreprocessUs += timing.preprocessUs;
        totalInferenceUs += timing.inferenceUs;
        totalPostprocessUs += timing.postprocessUs;
        maxFrameTotalUs = std::max(maxFrameTotalUs, frameTotalUs);
    }

    void print() const {
        LOGI(kTag, "共 %d 帧，检出 person 的帧 %d", totalFrameCount, personFrameCount);
        if (countedFrameCount == 0) {
            return;
        }
        double preprocessMs = totalPreprocessUs / 1000.0 / countedFrameCount;
        double inferenceMs = totalInferenceUs / 1000.0 / countedFrameCount;
        double postprocessMs = totalPostprocessUs / 1000.0 / countedFrameCount;
        const char *longestStageName = "推理";
        if (preprocessMs > inferenceMs && preprocessMs > postprocessMs) {
            longestStageName = "前处理";
        } else if (postprocessMs > inferenceMs) {
            longestStageName = "后处理";
        }
        LOGI(kTag, "耗时表（第 2～%d 帧平均，第 1 帧 %.2f ms 不算）", countedFrameCount + 1, firstFrameTotalUs / 1000.0);
        LOGI(kTag, "  前处理（RGA）   %7.2f ms", preprocessMs);
        LOGI(kTag, "  推理（RKNN）    %7.2f ms", inferenceMs);
        LOGI(kTag, "  后处理（CPU）   %7.2f ms", postprocessMs);
        LOGI(kTag, "  合计            %7.2f ms（单帧最长 %.2f ms），最长的一段：%s", preprocessMs + inferenceMs + postprocessMs,
             maxFrameTotalUs / 1000.0, longestStageName);
    }
};

int runM1DetectFile(const char *requestedStreamPath, const char *requestedModelPath, bool isLetterboxEnabled) {
    const char *streamPath = requestedStreamPath != nullptr ? requestedStreamPath : kDefaultStreamPath;
    const char *modelPath = requestedModelPath != nullptr ? requestedModelPath : kDefaultModelPath;
    LOGI(kTag, "输入 %s，模型 %s，%s", streamPath, modelPath, isLetterboxEnabled ? "letterbox" : "直接拉伸");

    // 第 1 步 建检测器（模型 + RGA 目标 buffer）和解码段
    ObjectDetectorConfig detectorConfig;
    detectorConfig.isLetterboxEnabled = isLetterboxEnabled;
    detectorConfig.boxThreshold = kBoxThreshold;
    detectorConfig.nmsThreshold = kNmsThreshold;
    detectorConfig.letterboxPadValue = kLetterboxPadValue;
    ObjectDetector detector;
    if (!detector.init(modelPath, detectorConfig)) {
        return 1;
    }
    DecodeStage decodeStage;
    if (!decodeStage.open(streamPath, MPP_VIDEO_CodingAVC, "ch0")) {
        return 1;
    }
    decodeStage.setPtsFrameRate(kInputFramesPerSecond);  // 裸流没有时间戳，按帧号打，检测结果才有 ptsUs

    // 第 2 步 启动检测线程。回调在检测线程里：打印一行、统计、第 N 帧存前处理结果
    FrameQueue frameQueue(kDetectQueueCapacityFrameCount);
    StageTimingSummary timingSummary;
    int callbackFrameIndex = 0;
    DetectStage detectStage;
    detectStage.start(frameQueue, detector, [&](const DetectionResult &detectionResult) {
        int personCount = static_cast<int>(
                std::count_if(detectionResult.objects.begin(), detectionResult.objects.end(),
                              [](const DetectedObject &object) { return object.classIndex == kPersonClassIndex; }));
        LOGI(kTag, "帧 %4d pts %7lld ms  person %d / 全部 %zu  前处理 %5lld us  推理 %6lld us  后处理 %5lld us",
             callbackFrameIndex, static_cast<long long>(detectionResult.ptsUs / 1000), personCount,
             detectionResult.objects.size(), static_cast<long long>(detectionResult.timing.preprocessUs),
             static_cast<long long>(detectionResult.timing.inferenceUs),
             static_cast<long long>(detectionResult.timing.postprocessUs));
        for (const DetectedObject &object : detectionResult.objects) {
            if (object.classIndex == kPersonClassIndex) {
                LOGD(kTag, "    person %.2f (%d, %d, %d, %d)", object.confidence, object.box.left, object.box.top,
                     object.box.right, object.box.bottom);
            }
        }
        if (callbackFrameIndex == kDumpRgbFrameIndex) {
            char dumpPath[128];
            snprintf(dumpPath, sizeof(dumpPath), kDumpRgbPathPattern, kDumpRgbFrameIndex,
                     detector.getModel().getInputWidth(), detector.getModel().getInputHeight());
            if (detector.saveLastModelInput(dumpPath)) {
                LOGI(kTag, "前处理结果存到 %s", dumpPath);
            }
        }
        timingSummary.addFrame(detectionResult, callbackFrameIndex);
        callbackFrameIndex++;
    });

    // 第 3 步 主线程当解码线程：解一帧放一帧，队列满了 push 会等（背压，文件输入不丢帧）
    double startMs = getNowMs();
    bool isDecodeOk = true;
    while (true) {
        MppFramePtr decodedFramePtr;
        ReadFrameResult readResult = decodeStage.readNextFrame(decodedFramePtr);
        if (readResult == ReadFrameResult::EndOfStream) {
            break;
        }
        if (readResult == ReadFrameResult::Error) {
            isDecodeOk = false;
            frameQueue.abort();
            break;
        }
        if (!frameQueue.push(std::move(decodedFramePtr))) {
            break;  // 检测线程出错叫停了
        }
    }
    frameQueue.closeInput();
    detectStage.join();
    double elapsedMs = getNowMs() - startMs;

    // 第 4 步 汇总
    timingSummary.print();
    LOGI(kTag, "解码 %d 帧，检测 %d 帧，用时 %.0f ms，整体 %.1f 帧/秒；队列最多排过 %zu 帧",
         decodeStage.getDecodedFrameCount(), detectStage.getDetectedFrameCount(), elapsedMs,
         detectStage.getDetectedFrameCount() * 1000.0 / elapsedMs, frameQueue.getMaxQueuedFrameCount());
    return (isDecodeOk && !detectStage.hasFailed()) ? 0 : 1;
}

int runM1ComparePostProcess(const char *requestedModelPath, const char *requestedImagePath) {
    const char *modelPath = requestedModelPath != nullptr ? requestedModelPath : kDefaultModelPath;
    const char *imagePath = requestedImagePath != nullptr ? requestedImagePath : kDefaultImagePath;

    RknnModel model;
    if (!model.load(modelPath)) {
        return 1;
    }
    ImageModelInput imageInput;
    if (!loadImageAsModelInput(imagePath, model.getInputWidth(), model.getInputHeight(), imageInput)) {
        return 1;
    }
    if (!model.runWithRgbInput(imageInput.modelInputRgb.data(), imageInput.modelInputRgb.size())) {
        return 1;
    }

    // 同一份输出张量，两种后处理各跑一遍
    std::vector<Yolov5OutputTensor> outputTensors;
    std::vector<DetectedObject> ownObjects;
    std::vector<DetectedObject> demoObjects;
    std::vector<std::string> demoLabelNames;
    bool isOk = ObjectDetector::buildYolov5OutputTensors(model, outputTensors) &&
                runYolov5PostProcess(outputTensors, imageInput.letterboxInfo, imageInput.sourceWidth,
                                     imageInput.sourceHeight, kBoxThreshold, kNmsThreshold, ownObjects) &&
                runDemoPostProcess(outputTensors, model.getInputWidth(), model.getInputHeight(),
                                   imageInput.letterboxInfo, demoObjects, &demoLabelNames);
    model.releaseOutputs();
    deinitPostProcess();
    if (!isOk) {
        LOGE(kTag, "后处理失败");
        return 1;
    }

    for (const DetectedObject &object : ownObjects) {
        LOGI(kTag, "自己  class=%2d %.3f (%d, %d, %d, %d)", object.classIndex, object.confidence, object.box.left,
             object.box.top, object.box.right, object.box.bottom);
    }
    for (size_t objectIndex = 0; objectIndex < demoObjects.size(); objectIndex++) {
        const DetectedObject &object = demoObjects[objectIndex];
        LOGI(kTag, "demo  class=%2d %.3f (%d, %d, %d, %d) %s", object.classIndex, object.confidence,
             object.box.left, object.box.top, object.box.right, object.box.bottom,
             demoLabelNames[objectIndex].c_str());
    }
    PostProcessComparison comparison = comparePostProcessResults(ownObjects, demoObjects, kCompareTolerancePixels);
    LOGI(kTag, "自己 %d 个，demo %d 个，配上 %d 个，最大误差 %d 像素 → %s", comparison.ownObjectCount,
         comparison.demoObjectCount, comparison.matchedObjectCount, comparison.maxCoordinateDiffPixels,
         comparison.isSame ? "一致" : "不一致");
    // 注意：demo 把框夹在 [0, 模型宽 / scale]，自己的夹在 [0, 原图宽 - 1]；框贴着图片边缘时差 1 像素属正常
    return comparison.isSame ? 0 : 1;
}
