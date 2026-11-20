#include "npu/letterbox.h"

#include <algorithm>
#include <cmath>

LetterboxInfo computeLetterboxInfo(int sourceWidth, int sourceHeight, int modelWidth, int modelHeight,
                                   bool isLetterboxEnabled) {
    LetterboxInfo letterboxInfo;
    if (!isLetterboxEnabled) {
        letterboxInfo.scaleX = static_cast<float>(modelWidth) / sourceWidth;
        letterboxInfo.scaleY = static_cast<float>(modelHeight) / sourceHeight;
        letterboxInfo.resizedWidth = modelWidth;
        letterboxInfo.resizedHeight = modelHeight;
        return letterboxInfo;
    }
    float scale = std::min(static_cast<float>(modelWidth) / sourceWidth, static_cast<float>(modelHeight) / sourceHeight);
    letterboxInfo.scaleX = scale;
    letterboxInfo.scaleY = scale;
    // lround：和 cv::resize(fx, fy) 算目标尺寸的方式一样（四舍五入）
    letterboxInfo.resizedWidth = std::min(modelWidth, static_cast<int>(std::lround(sourceWidth * scale)));
    letterboxInfo.resizedHeight = std::min(modelHeight, static_cast<int>(std::lround(sourceHeight * scale)));
    letterboxInfo.padLeftPixels = (modelWidth - letterboxInfo.resizedWidth) / 2;
    letterboxInfo.padTopPixels = (modelHeight - letterboxInfo.resizedHeight) / 2;
    return letterboxInfo;
}

static int clampToRange(float value, int minValue, int maxValue) {
    return static_cast<int>(std::clamp(value, static_cast<float>(minValue), static_cast<float>(maxValue)));
}

DetectionBox mapBoxToSourcePicture(const CandidateObject &modelObject, const LetterboxInfo &letterboxInfo,
                                   int sourceWidth, int sourceHeight) {
    DetectionBox sourceBox;
    sourceBox.left = clampToRange((modelObject.left - letterboxInfo.padLeftPixels) / letterboxInfo.scaleX, 0,
                                  sourceWidth - 1);
    sourceBox.top = clampToRange((modelObject.top - letterboxInfo.padTopPixels) / letterboxInfo.scaleY, 0,
                                 sourceHeight - 1);
    sourceBox.right = clampToRange((modelObject.right - letterboxInfo.padLeftPixels) / letterboxInfo.scaleX, 0,
                                   sourceWidth - 1);
    sourceBox.bottom = clampToRange((modelObject.bottom - letterboxInfo.padTopPixels) / letterboxInfo.scaleY, 0,
                                    sourceHeight - 1);
    return sourceBox;
}

void letterboxRgbOnCpu(const uint8_t *sourceRgb, int sourceWidth, int sourceHeight, const LetterboxInfo &letterboxInfo,
                       int modelWidth, int modelHeight, uint8_t padValue, std::vector<uint8_t> &modelInputRgb) {
    modelInputRgb.assign(static_cast<size_t>(modelWidth) * modelHeight * 3, padValue);
    const int resizedWidth = letterboxInfo.resizedWidth;
    const int resizedHeight = letterboxInfo.resizedHeight;
    // 双线性：目标像素中心反算到原图（半像素对齐，和 OpenCV INTER_LINEAR 一样），取周围 4 个点加权
    const float sourcePerResizedX = static_cast<float>(sourceWidth) / resizedWidth;
    const float sourcePerResizedY = static_cast<float>(sourceHeight) / resizedHeight;
    for (int resizedY = 0; resizedY < resizedHeight; resizedY++) {
        float sourceY = std::max(0.0f, (resizedY + 0.5f) * sourcePerResizedY - 0.5f);
        int sourceY0 = std::min(static_cast<int>(sourceY), sourceHeight - 1);
        int sourceY1 = std::min(sourceY0 + 1, sourceHeight - 1);
        float weightY = sourceY - sourceY0;
        uint8_t *outputRow = modelInputRgb.data() +
                             (static_cast<size_t>(resizedY + letterboxInfo.padTopPixels) * modelWidth +
                              letterboxInfo.padLeftPixels) * 3;
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
}
