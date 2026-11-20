#include "npu/yolov8_post_process.h"

#include <algorithm>
#include <cmath>

#include "npu/nms.h"

static float dequantizeToFloat(int8_t quantizedValue, const TensorQuantInfo &quantInfo) {
    return (static_cast<float>(quantizedValue) - static_cast<float>(quantInfo.zeroPoint)) * quantInfo.scale;
}

static int8_t quantizeFromFloat(float floatValue, const TensorQuantInfo &quantInfo) {
    float quantizedValue = floatValue / quantInfo.scale + static_cast<float>(quantInfo.zeroPoint);
    return static_cast<int8_t>(std::clamp(quantizedValue, -128.0f, 127.0f));
}

float computeDflDistance(const float *binValues, int dflBinCount) {
    // 先减最大值再 exp：结果不变，避免 exp 溢出（demo 没减，int8 反量化后的值不大，所以也没出事）
    float maxValue = *std::max_element(binValues, binValues + dflBinCount);
    float expSum = 0.0f;
    float weightedSum = 0.0f;
    for (int binIndex = 0; binIndex < dflBinCount; binIndex++) {
        float expValue = std::exp(binValues[binIndex] - maxValue);
        expSum += expValue;
        weightedSum += expValue * static_cast<float>(binIndex);
    }
    return weightedSum / expSum;
}

static void decodeOneBranch(const Yolov8OutputBranch &branch, float boxThreshold,
                            std::vector<CandidateObject> &candidateObjects) {
    const int gridCellCount = branch.gridWidth * branch.gridHeight;
    const int8_t scoreThresholdInt8 = quantizeFromFloat(boxThreshold, branch.classScoreQuantInfo);
    const int8_t scoreSumThresholdInt8 = quantizeFromFloat(boxThreshold, branch.scoreSumQuantInfo);
    float binValues[4 * kYolov8MaxDflBinCount];
    const float stride = static_cast<float>(branch.stridePixels);

    for (int gridY = 0; gridY < branch.gridHeight; gridY++) {
        for (int gridX = 0; gridX < branch.gridWidth; gridX++) {
            const int cellOffset = gridY * branch.gridWidth + gridX;
            // 分数和都不到阈值，单个类别更不可能到：整格跳过（80×80 的格子大多是背景）
            if (branch.scoreSumData != nullptr && branch.scoreSumData[cellOffset] < scoreSumThresholdInt8) {
                continue;
            }
            // NCHW：第 c 类的分数在 classScoreData[c × gridCellCount + cellOffset]
            int bestClassIndex = 0;
            int8_t bestScoreInt8 = branch.classScoreData[cellOffset];
            for (int classIndex = 1; classIndex < kYolov8ClassCount; classIndex++) {
                int8_t scoreInt8 = branch.classScoreData[classIndex * gridCellCount + cellOffset];
                if (scoreInt8 > bestScoreInt8) {
                    bestScoreInt8 = scoreInt8;
                    bestClassIndex = classIndex;
                }
            }
            if (bestScoreInt8 <= scoreThresholdInt8) {
                continue;
            }

            // 框：4 条边 × dflBinCount 个通道，顺序是 左、上、右、下（和 demo 的 box[0..3] 一样）
            for (int channelIndex = 0; channelIndex < 4 * branch.dflBinCount; channelIndex++) {
                binValues[channelIndex] =
                        dequantizeToFloat(branch.boxData[channelIndex * gridCellCount + cellOffset], branch.boxQuantInfo);
            }
            float leftDistance = computeDflDistance(binValues, branch.dflBinCount);
            float topDistance = computeDflDistance(binValues + branch.dflBinCount, branch.dflBinCount);
            float rightDistance = computeDflDistance(binValues + 2 * branch.dflBinCount, branch.dflBinCount);
            float bottomDistance = computeDflDistance(binValues + 3 * branch.dflBinCount, branch.dflBinCount);

            // 格子中心 (gridX + 0.5, gridY + 0.5)，往四个方向各伸出一段距离，再乘 stride 变成模型像素
            float centerX = static_cast<float>(gridX) + 0.5f;
            float centerY = static_cast<float>(gridY) + 0.5f;
            CandidateObject candidateObject;
            candidateObject.classIndex = bestClassIndex;
            candidateObject.confidence = dequantizeToFloat(bestScoreInt8, branch.classScoreQuantInfo);
            candidateObject.left = (centerX - leftDistance) * stride;
            candidateObject.top = (centerY - topDistance) * stride;
            candidateObject.right = (centerX + rightDistance) * stride;
            candidateObject.bottom = (centerY + bottomDistance) * stride;
            candidateObjects.push_back(candidateObject);
        }
    }
}

bool decodeYolov8OutputBranches(const std::vector<Yolov8OutputBranch> &outputBranches, float boxThreshold,
                                std::vector<CandidateObject> &candidateObjects) {
    candidateObjects.clear();
    for (const Yolov8OutputBranch &branch : outputBranches) {
        if (branch.boxData == nullptr || branch.classScoreData == nullptr || branch.gridWidth <= 0 ||
            branch.gridHeight <= 0 || branch.stridePixels <= 0 || branch.dflBinCount <= 0 ||
            branch.dflBinCount > kYolov8MaxDflBinCount) {
            return false;
        }
        decodeOneBranch(branch, boxThreshold, candidateObjects);
    }
    return true;
}

bool runYolov8PostProcess(const std::vector<Yolov8OutputBranch> &outputBranches, const LetterboxInfo &letterboxInfo,
                          int sourceWidth, int sourceHeight, float boxThreshold, float nmsThreshold,
                          std::vector<DetectedObject> &detectedObjects) {
    detectedObjects.clear();
    std::vector<CandidateObject> candidateObjects;
    if (!decodeYolov8OutputBranches(outputBranches, boxThreshold, candidateObjects)) {
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
