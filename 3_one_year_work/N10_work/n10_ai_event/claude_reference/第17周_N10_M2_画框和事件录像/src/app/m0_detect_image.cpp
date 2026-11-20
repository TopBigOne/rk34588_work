#include "app/m0_detect_image.h"

#include <map>
#include <string>
#include <vector>

#include "app/app_config.h"
#include "app/image_input.h"
#include "app/post_process_comparison.h"
#include "common/detection_types.h"
#include "common/log.h"
#include "common/time_utils.h"
#include "npu/letterbox.h"
#include "npu/object_detector.h"
#include "npu/rknn_model.h"
#include "third_party/rknn_yolov5_demo/postprocess.h"

static const char *const kTag = "M0";

int runM0DetectImage(const char *requestedModelPath, const char *requestedImagePath) {
    const char *modelPath = requestedModelPath != nullptr ? requestedModelPath : kDefaultModelPath;
    const char *imagePath = requestedImagePath != nullptr ? requestedImagePath : kDefaultImagePath;

    // 第 1 步 加载模型，打印版本和输入输出属性（zp、scale 后处理要用）
    RknnModel model;
    if (!model.load(modelPath)) {
        return 1;
    }
    model.printTensorAttrs();

    // 第 2 步 读图片，CPU letterbox 成模型输入
    double preprocessStartMs = getNowMs();
    ImageModelInput imageInput;
    if (!loadImageAsModelInput(imagePath, model.getInputWidth(), model.getInputHeight(), imageInput)) {
        return 1;
    }
    double preprocessMs = getNowMs() - preprocessStartMs;

    // 第 3 步 推理一次（inputs_set + run + outputs_get 算在一起，和 demo 的 "once run" 口径一样）
    double inferenceStartMs = getNowMs();
    if (!model.runWithRgbInput(imageInput.modelInputRgb.data(), imageInput.modelInputRgb.size())) {
        return 1;
    }
    double inferenceMs = getNowMs() - inferenceStartMs;

    // 第 4 步 后处理（M0 用 demo 的），输出已经是原图坐标
    double postprocessStartMs = getNowMs();
    std::vector<Yolov5OutputTensor> outputTensors;
    std::vector<DetectedObject> demoObjects;
    std::vector<std::string> labelNames;
    bool isPostProcessOk = ObjectDetector::buildYolov5OutputTensors(model, outputTensors) &&
                           runDemoPostProcess(outputTensors, model.getInputWidth(), model.getInputHeight(),
                                              imageInput.letterboxInfo, demoObjects, &labelNames);
    double postprocessMs = getNowMs() - postprocessStartMs;
    model.releaseOutputs();
    if (!isPostProcessOk) {
        LOGE(kTag, "后处理失败（标签文件在不在 /userdata/n10/model/？）");
        deinitPostProcess();
        return 1;
    }

    std::map<std::string, int> countByLabel;
    for (size_t objectIndex = 0; objectIndex < demoObjects.size(); objectIndex++) {
        const DetectedObject &detectedObject = demoObjects[objectIndex];
        LOGI(kTag, "%-12s class=%2d  %.1f%%  (%d, %d, %d, %d)", labelNames[objectIndex].c_str(),
             detectedObject.classIndex, detectedObject.confidence * 100, detectedObject.box.left,
             detectedObject.box.top, detectedObject.box.right, detectedObject.box.bottom);
        countByLabel[labelNames[objectIndex]]++;
    }
    std::string countText;
    for (const auto &[labelName, objectCount] : countByLabel) {
        countText += labelName + " x" + std::to_string(objectCount) + "  ";
    }
    LOGI(kTag, "共 %zu 个框：%s", demoObjects.size(), countText.c_str());
    LOGI(kTag, "第一次：前处理（读图 + CPU letterbox）%.2f ms，推理 %.2f ms，后处理 %.2f ms", preprocessMs,
         inferenceMs, postprocessMs);

    // 第 5 步 连跑几次取平均（第一次含预热，通常偏慢）
    double totalInferenceMs = 0;
    double totalPostprocessMs = 0;
    for (int runIndex = 0; runIndex < kBenchmarkRunCount; runIndex++) {
        double runStartMs = getNowMs();
        if (!model.runWithRgbInput(imageInput.modelInputRgb.data(), imageInput.modelInputRgb.size())) {
            deinitPostProcess();
            return 1;
        }
        double runEndMs = getNowMs();
        ObjectDetector::buildYolov5OutputTensors(model, outputTensors);
        runDemoPostProcess(outputTensors, model.getInputWidth(), model.getInputHeight(), imageInput.letterboxInfo,
                           demoObjects);
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
