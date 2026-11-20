#include "npu/yolov5_post_process.h"

#include <algorithm>
#include <cmath>

#include "npu/nms.h"

// demo postprocess.cc 的 anchor0 / anchor1 / anchor2：每组 3 个 (宽, 高)
static const int kAnchorsStride8[6] = {10, 13, 16, 30, 33, 23};
static const int kAnchorsStride16[6] = {30, 61, 62, 45, 59, 119};
static const int kAnchorsStride32[6] = {116, 90, 156, 198, 373, 326};

const int *getYolov5AnchorsForStride(int stridePixels) {
    switch (stridePixels) {
        case 8:
            return kAnchorsStride8;
        case 16:
            return kAnchorsStride16;
        case 32:
            return kAnchorsStride32;
        default:
            return nullptr;
    }
}

static float dequantizeToFloat(int8_t quantizedValue, const TensorQuantInfo &quantInfo) {
    return (static_cast<float>(quantizedValue) - static_cast<float>(quantInfo.zeroPoint)) * quantInfo.scale;
}

// float 阈值 → int8：之后每个格子直接拿 int8 比，不用每个值都先还原成 float（80×80×3 个格子，省很多乘法）
static int8_t quantizeFromFloat(float floatValue, const TensorQuantInfo &quantInfo) {
    float quantizedValue = floatValue / quantInfo.scale + static_cast<float>(quantInfo.zeroPoint);
    return static_cast<int8_t>(std::clamp(quantizedValue, -128.0f, 127.0f));
}

static void decodeOneTensor(const Yolov5OutputTensor &tensor, float boxThreshold,
                            std::vector<CandidateObject> &candidateObjects) {
    const int gridCellCount = tensor.gridWidth * tensor.gridHeight;
    const int *anchors = getYolov5AnchorsForStride(tensor.stridePixels);
    const int8_t thresholdInt8 = quantizeFromFloat(boxThreshold, tensor.quantInfo);

    for (int anchorIndex = 0; anchorIndex < kYolov5AnchorCountPerGrid; anchorIndex++) {
        // NCHW：同一个“通道”（比如所有格子的 x）在内存里连续，下一个通道隔 gridCellCount 个字节
        const int8_t *anchorBase = tensor.data + kYolov5ValuesPerAnchor * anchorIndex * gridCellCount;
        for (int gridY = 0; gridY < tensor.gridHeight; gridY++) {
            for (int gridX = 0; gridX < tensor.gridWidth; gridX++) {
                const int8_t *cellValues = anchorBase + gridY * tensor.gridWidth + gridX;
                auto valueAt = [&](int channelIndex) { return cellValues[channelIndex * gridCellCount]; };

                int8_t objectConfidenceInt8 = valueAt(4);
                if (objectConfidenceInt8 < thresholdInt8) {
                    continue;
                }
                int bestClassIndex = 0;
                int8_t bestClassScoreInt8 = valueAt(5);
                for (int classIndex = 1; classIndex < kYolov5ClassCount; classIndex++) {
                    int8_t classScoreInt8 = valueAt(5 + classIndex);
                    if (classScoreInt8 > bestClassScoreInt8) {
                        bestClassScoreInt8 = classScoreInt8;
                        bestClassIndex = classIndex;
                    }
                }
                if (bestClassScoreInt8 <= thresholdInt8) {
                    continue;
                }

                // 输出已经是 sigmoid 之后的值（推断：demo 定义了 sigmoid() 但 process() 里没调用），这里只做 YOLOv5 的框解码公式：
                // 中心 = (2·σ - 0.5 + 格子下标) × stride，宽高 = (2·σ)² × anchor
                float centerX = (dequantizeToFloat(valueAt(0), tensor.quantInfo) * 2.0f - 0.5f + gridX) *
                                static_cast<float>(tensor.stridePixels);
                float centerY = (dequantizeToFloat(valueAt(1), tensor.quantInfo) * 2.0f - 0.5f + gridY) *
                                static_cast<float>(tensor.stridePixels);
                float widthFactor = dequantizeToFloat(valueAt(2), tensor.quantInfo) * 2.0f;
                float heightFactor = dequantizeToFloat(valueAt(3), tensor.quantInfo) * 2.0f;
                float boxWidth = widthFactor * widthFactor * static_cast<float>(anchors[anchorIndex * 2]);
                float boxHeight = heightFactor * heightFactor * static_cast<float>(anchors[anchorIndex * 2 + 1]);

                CandidateObject candidateObject;
                candidateObject.classIndex = bestClassIndex;
                candidateObject.confidence = dequantizeToFloat(bestClassScoreInt8, tensor.quantInfo) *
                                             dequantizeToFloat(objectConfidenceInt8, tensor.quantInfo);
                candidateObject.left = centerX - boxWidth / 2.0f;
                candidateObject.top = centerY - boxHeight / 2.0f;
                candidateObject.right = candidateObject.left + boxWidth;
                candidateObject.bottom = candidateObject.top + boxHeight;
                candidateObjects.push_back(candidateObject);
            }
        }
    }
}

bool decodeYolov5OutputTensors(const std::vector<Yolov5OutputTensor> &outputTensors, float boxThreshold,
                               std::vector<CandidateObject> &candidateObjects) {
    candidateObjects.clear();
    for (const Yolov5OutputTensor &tensor : outputTensors) {
        if (tensor.data == nullptr || tensor.gridWidth <= 0 || tensor.gridHeight <= 0 ||
            getYolov5AnchorsForStride(tensor.stridePixels) == nullptr) {
            return false;
        }
        decodeOneTensor(tensor, boxThreshold, candidateObjects);
    }
    return true;
}

bool runYolov5PostProcess(const std::vector<Yolov5OutputTensor> &outputTensors, const LetterboxInfo &letterboxInfo,
                          int sourceWidth, int sourceHeight, float boxThreshold, float nmsThreshold,
                          std::vector<DetectedObject> &detectedObjects) {
    detectedObjects.clear();
    std::vector<CandidateObject> candidateObjects;
    if (!decodeYolov5OutputTensors(outputTensors, boxThreshold, candidateObjects)) {
        return false;
    }
    suppressOverlappingBoxes(candidateObjects, nmsThreshold);
    for (const CandidateObject &candidateObject : candidateObjects) {
        DetectedObject detectedObject;
        detectedObject.classIndex = candidateObject.classIndex;
        detectedObject.confidence = candidateObject.confidence;
        detectedObject.box = mapBoxToSourcePicture(candidateObject, letterboxInfo, sourceWidth, sourceHeight);
        detectedObjects.push_back(detectedObject);
    }
    return true;
}
