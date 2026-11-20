#include "app/m0_detect_image.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "app/app_config.h"
#include "common/detection_types.h"
#include "common/log.h"
#include "common/time_utils.h"
#include "npu/rknn_model.h"
#include "third_party/rknn_yolov5_demo/postprocess.h"
#include "third_party/stb/stb_image.h"

static const char *const kTag = "M0";

struct StbImageDeleter {
    void operator()(uint8_t *pixels) const { stbi_image_free(pixels); }
};
using StbImagePtr = std::unique_ptr<uint8_t, StbImageDeleter>;

struct CpuLetterboxResult {
    float scale = 1.0f;  // 原图 → 模型输入的缩放比例（宽高同一个）
    int padLeftPixels = 0;
    int padTopPixels = 0;
    int padRightPixels = 0;
    int padBottomPixels = 0;
};

// 等比缩放 + 四周补灰，和 demo preprocess.cc 的 letterbox() 算法一致（cv::resize 双线性 + copyMakeBorder）。
// M0 临时用 CPU 做；M1 换成 RGA（npu/letterbox + rga/rga_preprocessor）。
static CpuLetterboxResult letterboxRgbOnCpu(const uint8_t *sourceRgb, int sourceWidth, int sourceHeight,
                                            std::vector<uint8_t> &modelInputRgb, int modelWidth, int modelHeight) {
    CpuLetterboxResult letterboxResult;
    letterboxResult.scale = std::min(static_cast<float>(modelWidth) / sourceWidth,
                                     static_cast<float>(modelHeight) / sourceHeight);
    int resizedWidth = std::min(modelWidth, static_cast<int>(std::lround(sourceWidth * letterboxResult.scale)));
    int resizedHeight = std::min(modelHeight, static_cast<int>(std::lround(sourceHeight * letterboxResult.scale)));
    letterboxResult.padLeftPixels = (modelWidth - resizedWidth) / 2;
    letterboxResult.padTopPixels = (modelHeight - resizedHeight) / 2;
    letterboxResult.padRightPixels = modelWidth - resizedWidth - letterboxResult.padLeftPixels;
    letterboxResult.padBottomPixels = modelHeight - resizedHeight - letterboxResult.padTopPixels;

    modelInputRgb.assign(static_cast<size_t>(modelWidth) * modelHeight * 3, static_cast<uint8_t>(kLetterboxPadValue));

    // 双线性：目标像素中心反算到原图（半像素对齐，和 OpenCV INTER_LINEAR 一样），取周围 4 个点加权
    float sourcePerResizedX = static_cast<float>(sourceWidth) / resizedWidth;
    float sourcePerResizedY = static_cast<float>(sourceHeight) / resizedHeight;
    for (int resizedY = 0; resizedY < resizedHeight; resizedY++) {
        float sourceY = std::max(0.0f, (resizedY + 0.5f) * sourcePerResizedY - 0.5f);
        int sourceY0 = std::min(static_cast<int>(sourceY), sourceHeight - 1);
        int sourceY1 = std::min(sourceY0 + 1, sourceHeight - 1);
        float weightY = sourceY - sourceY0;
        uint8_t *outputRow = modelInputRgb.data() +
                             (static_cast<size_t>(resizedY + letterboxResult.padTopPixels) * modelWidth +
                              letterboxResult.padLeftPixels) * 3;
        for (int resizedX = 0; resizedX < resizedWidth; resizedX++) {
            float sourceX = std::max(0.0f, (resizedX + 0.5f) * sourcePerResizedX - 0.5f);
            int sourceX0 = std::min(static_cast<int>(sourceX), sourceWidth - 1);
            int sourceX1 = std::min(sourceX0 + 1, sourceWidth - 1);
            float weightX = sourceX - sourceX0;
            for (int channelIndex = 0; channelIndex < 3; channelIndex++) {
                auto pixelAt = [&](int x, int y) {
                    return static_cast<float>(sourceRgb[(static_cast<size_t>(y) * sourceWidth + x) * 3 + channelIndex]);
                };
                float topValue = pixelAt(sourceX0, sourceY0) * (1 - weightX) + pixelAt(sourceX1, sourceY0) * weightX;
                float bottomValue = pixelAt(sourceX0, sourceY1) * (1 - weightX) + pixelAt(sourceX1, sourceY1) * weightX;
                float value = topValue * (1 - weightY) + bottomValue * weightY;
                outputRow[resizedX * 3 + channelIndex] = static_cast<uint8_t>(std::clamp(std::lround(value), 0L, 255L));
            }
        }
    }
    return letterboxResult;
}

// demo 后处理要求 3 个输出依次是 stride 8 / 16 / 32（80×80、40×40、20×20），int8 量化
static bool runDemoPostProcess(const RknnModel &model, const CpuLetterboxResult &letterboxResult,
                               detect_result_group_t &resultGroup) {
    if (model.getOutputCount() != 3) {
        LOGE(kTag, "demo 后处理只认 3 个输出（YOLOv5），这个模型有 %d 个", model.getOutputCount());
        return false;
    }
    std::vector<int32_t> zeroPoints;
    std::vector<float> scales;
    for (int outputIndex = 0; outputIndex < 3; outputIndex++) {
        TensorQuantInfo quantInfo = model.getOutputQuantInfo(outputIndex);
        zeroPoints.push_back(quantInfo.zeroPoint);
        scales.push_back(quantInfo.scale);
    }
    BOX_RECT pads;
    pads.left = letterboxResult.padLeftPixels;
    pads.right = letterboxResult.padRightPixels;
    pads.top = letterboxResult.padTopPixels;
    pads.bottom = letterboxResult.padBottomPixels;
    // demo 的函数参数不带 const，只读不写，所以 const_cast
    int ret = post_process(const_cast<int8_t *>(model.getOutputData(0)), const_cast<int8_t *>(model.getOutputData(1)),
                           const_cast<int8_t *>(model.getOutputData(2)), model.getInputHeight(), model.getInputWidth(),
                           kBoxThreshold, kNmsThreshold, pads, letterboxResult.scale, letterboxResult.scale, zeroPoints,
                           scales, &resultGroup);
    return ret == 0;
}

static void convertToDetectionResult(const detect_result_group_t &resultGroup, int sourceWidth, int sourceHeight,
                                     DetectionResult &detectionResult) {
    detectionResult.sourceWidth = sourceWidth;
    detectionResult.sourceHeight = sourceHeight;
    detectionResult.objects.clear();
    for (int resultIndex = 0; resultIndex < resultGroup.count; resultIndex++) {
        const detect_result_t &demoResult = resultGroup.results[resultIndex];
        DetectedObject detectedObject;
        detectedObject.classIndex = demoResult.class_id;
        detectedObject.confidence = demoResult.prop;
        detectedObject.box.left = demoResult.box.left;
        detectedObject.box.top = demoResult.box.top;
        detectedObject.box.right = demoResult.box.right;
        detectedObject.box.bottom = demoResult.box.bottom;
        detectionResult.objects.push_back(detectedObject);
    }
}

int runM0DetectImage(const char *requestedModelPath, const char *requestedImagePath) {
    const char *modelPath = requestedModelPath != nullptr ? requestedModelPath : kDefaultModelPath;
    const char *imagePath = requestedImagePath != nullptr ? requestedImagePath : kDefaultImagePath;

    // 第 1 步 加载模型，打印版本和输入输出属性（zp、scale 后处理要用）
    RknnModel model;
    if (!model.load(modelPath)) {
        return 1;
    }
    model.printTensorAttrs();
    if (model.getInputChannelCount() != 3) {
        LOGE(kTag, "模型输入不是 3 通道：%d", model.getInputChannelCount());
        return 1;
    }

    // 第 2 步 读图片成 RGB（stb_image 输出就是 RGB 顺序；OpenCV imread 是 BGR，demo 才要 cvtColor）
    int sourceWidth = 0;
    int sourceHeight = 0;
    int fileChannelCount = 0;
    StbImagePtr sourceRgbPtr(stbi_load(imagePath, &sourceWidth, &sourceHeight, &fileChannelCount, 3));
    if (!sourceRgbPtr) {
        LOGE(kTag, "读图片失败：%s（%s）", imagePath, stbi_failure_reason());
        return 1;
    }
    LOGI(kTag, "图片 %s：%dx%d", imagePath, sourceWidth, sourceHeight);

    // 第 3 步 letterbox 成模型输入大小
    double preprocessStartMs = getNowMs();
    std::vector<uint8_t> modelInputRgb;
    CpuLetterboxResult letterboxResult =
            letterboxRgbOnCpu(sourceRgbPtr.get(), sourceWidth, sourceHeight, modelInputRgb, model.getInputWidth(),
                              model.getInputHeight());
    double preprocessMs = getNowMs() - preprocessStartMs;
    LOGI(kTag, "letterbox：scale=%.4f，补边 左 %d 上 %d 右 %d 下 %d，CPU 用时 %.2f ms", letterboxResult.scale,
         letterboxResult.padLeftPixels, letterboxResult.padTopPixels, letterboxResult.padRightPixels,
         letterboxResult.padBottomPixels, preprocessMs);

    // 第 4 步 推理一次（inputs_set + run + outputs_get 算在一起，和 demo 的 "once run" 口径一样）
    double inferenceStartMs = getNowMs();
    if (!model.runWithRgbInput(modelInputRgb.data(), modelInputRgb.size())) {
        return 1;
    }
    double inferenceMs = getNowMs() - inferenceStartMs;

    // 第 5 步 后处理，输出已经是原图坐标
    double postprocessStartMs = getNowMs();
    detect_result_group_t resultGroup;
    bool isPostProcessOk = runDemoPostProcess(model, letterboxResult, resultGroup);
    double postprocessMs = getNowMs() - postprocessStartMs;
    model.releaseOutputs();
    if (!isPostProcessOk) {
        LOGE(kTag, "后处理失败（标签文件在不在 /userdata/n10/model/？）");
        deinitPostProcess();
        return 1;
    }

    DetectionResult detectionResult;
    convertToDetectionResult(resultGroup, sourceWidth, sourceHeight, detectionResult);
    detectionResult.timing.preprocessUs = static_cast<int64_t>(preprocessMs * 1000);
    detectionResult.timing.inferenceUs = static_cast<int64_t>(inferenceMs * 1000);
    detectionResult.timing.postprocessUs = static_cast<int64_t>(postprocessMs * 1000);

    std::map<std::string, int> countByLabel;
    for (int resultIndex = 0; resultIndex < resultGroup.count; resultIndex++) {
        const detect_result_t &demoResult = resultGroup.results[resultIndex];
        const DetectedObject &detectedObject = detectionResult.objects[resultIndex];
        LOGI(kTag, "%-12s class=%2d  %.1f%%  (%d, %d, %d, %d)", demoResult.name, detectedObject.classIndex,
             detectedObject.confidence * 100, detectedObject.box.left, detectedObject.box.top,
             detectedObject.box.right, detectedObject.box.bottom);
        countByLabel[demoResult.name]++;
    }
    std::string countText;
    for (const auto &[labelName, objectCount] : countByLabel) {
        countText += labelName + " x" + std::to_string(objectCount) + "  ";
    }
    LOGI(kTag, "共 %d 个框：%s", resultGroup.count, countText.c_str());
    LOGI(kTag, "第一次：前处理 %.2f ms，推理 %.2f ms，后处理 %.2f ms", preprocessMs, inferenceMs, postprocessMs);

    // 第 6 步 连跑几次取平均（第一次含预热，通常偏慢）
    double totalInferenceMs = 0;
    double totalPostprocessMs = 0;
    for (int runIndex = 0; runIndex < kBenchmarkRunCount; runIndex++) {
        double runStartMs = getNowMs();
        if (!model.runWithRgbInput(modelInputRgb.data(), modelInputRgb.size())) {
            deinitPostProcess();
            return 1;
        }
        double runEndMs = getNowMs();
        runDemoPostProcess(model, letterboxResult, resultGroup);
        totalPostprocessMs += getNowMs() - runEndMs;
        totalInferenceMs += runEndMs - runStartMs;
        model.releaseOutputs();
    }
    LOGI(kTag, "连跑 %d 次平均：推理 %.2f ms，后处理 %.2f ms", kBenchmarkRunCount,
         totalInferenceMs / kBenchmarkRunCount, totalPostprocessMs / kBenchmarkRunCount);
    LOGI(kTag, "librknnrt %s，驱动 %s", model.getRuntimeVersion().c_str(), model.getDriverVersion().c_str());

    deinitPostProcess();
    return 0;
}
