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

// model_zoo v8 的输出顺序：stride 8 的 框、分数、(分数和)，stride 16 的……，stride 32 的……
bool ObjectDetector::buildYolov8OutputBranches(const RknnModel &model, std::vector<Yolov8OutputBranch> &outputBranches) {
    static const int kExpectedStrides[3] = {8, 16, 32};
    const int outputCount = model.getOutputCount();
    if (outputCount != 6 && outputCount != 9) {
        LOGE(kTag, "YOLOv8 应该有 6 或 9 个输出，这个模型有 %d 个（是不是 YOLOv5 模型？）", outputCount);
        return false;
    }
    const int outputsPerBranch = outputCount / 3;
    outputBranches.clear();
    for (int branchIndex = 0; branchIndex < 3; branchIndex++) {
        const int boxOutputIndex = branchIndex * outputsPerBranch;
        const int scoreOutputIndex = boxOutputIndex + 1;
        std::vector<int> boxDims = model.getOutputDims(boxOutputIndex);
        std::vector<int> scoreDims = model.getOutputDims(scoreOutputIndex);
        if (boxDims.size() != 4 || scoreDims.size() != 4 || boxDims[1] % 4 != 0 ||
            scoreDims[1] != kYolov8ClassCount || boxDims[2] != scoreDims[2] || boxDims[3] != scoreDims[3]) {
            LOGE(kTag, "第 %d 组输出形状不对：框应为 [1, 64, H, W]，分数应为 [1, 80, H, W]（NCHW）", branchIndex);
            return false;
        }
        Yolov8OutputBranch branch;
        branch.boxData = model.getOutputData(boxOutputIndex);
        branch.boxQuantInfo = model.getOutputQuantInfo(boxOutputIndex);
        branch.classScoreData = model.getOutputData(scoreOutputIndex);
        branch.classScoreQuantInfo = model.getOutputQuantInfo(scoreOutputIndex);
        if (outputsPerBranch == 3) {
            std::vector<int> scoreSumDims = model.getOutputDims(boxOutputIndex + 2);
            if (scoreSumDims.size() != 4 || scoreSumDims[1] != 1) {
                LOGE(kTag, "第 %d 组的分数和应为 [1, 1, H, W]", branchIndex);
                return false;
            }
            branch.scoreSumData = model.getOutputData(boxOutputIndex + 2);
            branch.scoreSumQuantInfo = model.getOutputQuantInfo(boxOutputIndex + 2);
        }
        branch.gridHeight = boxDims[2];
        branch.gridWidth = boxDims[3];
        branch.dflBinCount = boxDims[1] / 4;
        branch.stridePixels = model.getInputHeight() / branch.gridHeight;
        if (branch.stridePixels != kExpectedStrides[branchIndex] || branch.dflBinCount > kYolov8MaxDflBinCount) {
            LOGE(kTag, "第 %d 组 stride %d（应为 %d）、DFL bin %d", branchIndex, branch.stridePixels,
                 kExpectedStrides[branchIndex], branch.dflBinCount);
            return false;
        }
        outputBranches.push_back(branch);
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

bool ObjectDetector::initSharingModel(ObjectDetector &sourceDetector) {
    config = sourceDetector.config;
    if (!model.duplicateFrom(sourceDetector.model)) {
        return false;
    }
    return preprocessor.init(model.getInputWidth(), model.getInputHeight(), config.letterboxPadValue);
}

bool ObjectDetector::setNpuCoreMask(rknn_core_mask coreMask) {
    return model.setCoreMask(coreMask);
}

bool ObjectDetector::runPostProcess(const LetterboxInfo &letterboxInfo, DetectionResult &detectionResult) {
    if (config.yoloVersion == YoloVersion::V8) {
        std::vector<Yolov8OutputBranch> outputBranches;
        return buildYolov8OutputBranches(model, outputBranches) &&
               runYolov8PostProcess(outputBranches, letterboxInfo, detectionResult.sourceWidth,
                                    detectionResult.sourceHeight, config.boxThreshold, config.nmsThreshold,
                                    detectionResult.objects);
    }
    std::vector<Yolov5OutputTensor> outputTensors;
    return buildYolov5OutputTensors(model, outputTensors) &&
           runYolov5PostProcess(outputTensors, letterboxInfo, detectionResult.sourceWidth,
                                detectionResult.sourceHeight, config.boxThreshold, config.nmsThreshold,
                                detectionResult.objects);
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
    bool isPostProcessOk = runPostProcess(letterboxInfo, detectionResult);
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
