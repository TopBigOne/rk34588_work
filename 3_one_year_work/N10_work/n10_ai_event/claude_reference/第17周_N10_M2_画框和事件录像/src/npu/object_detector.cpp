#include "npu/object_detector.h"

#include "common/log.h"
#include "common/time_utils.h"
#include "npu/letterbox.h"

static const char *const kTag = "ObjectDetector";

// 输出形状必须是 [1, 3×85, H, W]，并且按 stride 8 / 16 / 32 的顺序（和 demo 的假设一样）。不对就直接报错，不猜。
bool ObjectDetector::buildYolov5OutputTensors(const RknnModel &model, std::vector<Yolov5OutputTensor> &outputTensors) {
    static const int kExpectedStrides[3] = {8, 16, 32};
    if (model.getOutputCount() != 3) {
        LOGE(kTag, "YOLOv5 应该有 3 个输出，这个模型有 %d 个", model.getOutputCount());
        return false;
    }
    outputTensors.clear();
    for (int outputIndex = 0; outputIndex < 3; outputIndex++) {
        std::vector<int> dims = model.getOutputDims(outputIndex);
        if (dims.size() != 4 || dims[1] != kYolov5AnchorCountPerGrid * kYolov5ValuesPerAnchor) {
            LOGE(kTag, "输出 %d 的形状不是 [1, 255, H, W]（NCHW）", outputIndex);
            return false;
        }
        Yolov5OutputTensor tensor;
        tensor.data = model.getOutputData(outputIndex);
        tensor.quantInfo = model.getOutputQuantInfo(outputIndex);
        tensor.gridHeight = dims[2];
        tensor.gridWidth = dims[3];
        tensor.stridePixels = model.getInputHeight() / tensor.gridHeight;
        if (tensor.stridePixels != kExpectedStrides[outputIndex]) {
            LOGE(kTag, "输出 %d 的 stride 是 %d，应该是 %d（输出顺序不对？）", outputIndex, tensor.stridePixels,
                 kExpectedStrides[outputIndex]);
            return false;
        }
        outputTensors.push_back(tensor);
    }
    return true;
}

bool ObjectDetector::init(const char *requestedModelPath, const ObjectDetectorConfig &requestedConfig) {
    config = requestedConfig;
    if (!model.load(requestedModelPath)) {
        return false;
    }
    if (model.getInputChannelCount() != 3) {
        LOGE(kTag, "模型输入不是 3 通道");
        return false;
    }
    return preprocessor.init(model.getInputWidth(), model.getInputHeight(), config.letterboxPadValue);
}

bool ObjectDetector::detect(MppFrame decodedFrame, DetectionResult &detectionResult) {
    detectionResult.objects.clear();
    detectionResult.ptsUs = mpp_frame_get_pts(decodedFrame);
    detectionResult.sourceWidth = static_cast<int>(mpp_frame_get_width(decodedFrame));
    detectionResult.sourceHeight = static_cast<int>(mpp_frame_get_height(decodedFrame));

    // 第 1 步 前处理：RGA 把 NV12 缩放 + 转成 640×640 RGB
    double stageStartMs = getNowMs();
    LetterboxInfo letterboxInfo;
    if (!preprocessor.convertToModelInput(decodedFrame, config.isLetterboxEnabled, letterboxInfo)) {
        return false;
    }
    double preprocessEndMs = getNowMs();

    // 第 2 步 推理：inputs_set（会拷贝一份）+ run + outputs_get
    if (!model.runWithRgbInput(preprocessor.getRgbData(), preprocessor.getRgbSizeBytes())) {
        return false;
    }
    double inferenceEndMs = getNowMs();

    // 第 3 步 后处理：解码 → NMS → 换算回原图坐标
    std::vector<Yolov5OutputTensor> outputTensors;
    bool isPostProcessOk = buildYolov5OutputTensors(model, outputTensors) &&
                           runYolov5PostProcess(outputTensors, letterboxInfo, detectionResult.sourceWidth,
                                                detectionResult.sourceHeight, config.boxThreshold,
                                                config.nmsThreshold, detectionResult.objects);
    model.releaseOutputs();  // 不管成败都要还，否则下一次 run 会失败
    if (!isPostProcessOk) {
        return false;
    }
    double postprocessEndMs = getNowMs();

    detectionResult.timing.preprocessUs = static_cast<int64_t>((preprocessEndMs - stageStartMs) * 1000);
    detectionResult.timing.inferenceUs = static_cast<int64_t>((inferenceEndMs - preprocessEndMs) * 1000);
    detectionResult.timing.postprocessUs = static_cast<int64_t>((postprocessEndMs - inferenceEndMs) * 1000);
    return true;
}

bool ObjectDetector::saveLastModelInput(const char *filePath) const {
    return preprocessor.saveRgbToFile(filePath);
}
