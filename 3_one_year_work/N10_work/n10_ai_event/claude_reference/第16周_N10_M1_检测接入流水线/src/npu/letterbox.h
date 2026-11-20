#pragma once

#include <cstdint>
#include <vector>

#include "common/detection_types.h"
#include "npu/candidate_object.h"

// 原图 → 模型输入的几何关系。拉伸时 scaleX ≠ scaleY、补边为 0；letterbox 时两个 scale 相等、补边居中。
struct LetterboxInfo {
    float scaleX = 1.0f;  // 模型输入宽 / 原图宽（拉伸）；min(两个比例)（letterbox）
    float scaleY = 1.0f;
    int resizedWidth = 0;  // 原图缩放后在模型输入里占多大
    int resizedHeight = 0;
    int padLeftPixels = 0;
    int padTopPixels = 0;
};

LetterboxInfo computeLetterboxInfo(int sourceWidth, int sourceHeight, int modelWidth, int modelHeight,
                                   bool isLetterboxEnabled);

// 模型坐标 → 原图坐标：先减补边，再除以缩放比例，最后夹到原图范围 [0, 宽-1] × [0, 高-1]
DetectionBox mapBoxToSourcePicture(const CandidateObject &modelObject, const LetterboxInfo &letterboxInfo,
                                   int sourceWidth, int sourceHeight);

// CPU 版前处理（M0 单张图、M1 和 demo 对比时用）：双线性缩放到 letterboxInfo.resized 大小，四周补 padValue。
// 和 demo preprocess.cc 的 cv::resize + copyMakeBorder 同一算法。流水线里用 RGA 版（rga/rga_preprocessor）。
void letterboxRgbOnCpu(const uint8_t *sourceRgb, int sourceWidth, int sourceHeight, const LetterboxInfo &letterboxInfo,
                       int modelWidth, int modelHeight, uint8_t padValue, std::vector<uint8_t> &modelInputRgb);
