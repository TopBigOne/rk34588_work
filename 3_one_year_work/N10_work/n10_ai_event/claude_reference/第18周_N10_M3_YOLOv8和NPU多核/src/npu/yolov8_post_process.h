#pragma once

#include <cstdint>
#include <vector>

#include "common/detection_types.h"
#include "npu/candidate_object.h"
#include "npu/letterbox.h"
#include "npu/tensor_quant_info.h"

// rknn_model_zoo（airockchip 改过的 ultralytics_yolov8）导出的 YOLOv8：每个尺度一组输出（int8，NCHW），3 个尺度共 6 或 9 个：
//   框      [1, 4 × dflBinCount, H, W]  每条边（左、上、右、下到格子中心的距离）是 16 个 bin 的分布，要 softmax 再求期望（DFL）
//   类别分数 [1, 80, H, W]              已经过 sigmoid（推断：demo 直接反量化就当分数用，没调 sigmoid）
//   分数和   [1, 1, H, W]               可选：80 个分数之和，用来快速跳过空格子；没有这个输出时 scoreSumData = nullptr
// 和 YOLOv5 的区别：没有 anchor、没有目标置信度（objectness），框不是中心 + 宽高而是到四条边的距离。
struct Yolov8OutputBranch {
    const int8_t *boxData = nullptr;
    TensorQuantInfo boxQuantInfo;
    const int8_t *classScoreData = nullptr;
    TensorQuantInfo classScoreQuantInfo;
    const int8_t *scoreSumData = nullptr;
    TensorQuantInfo scoreSumQuantInfo;
    int gridWidth = 0;   // 80 / 40 / 20（640 输入时）
    int gridHeight = 0;
    int stridePixels = 0;  // 8 / 16 / 32
    int dflBinCount = 0;   // 框输出通道数 / 4，官方模型是 16
};

constexpr int kYolov8ClassCount = 80;
constexpr int kYolov8MaxDflBinCount = 32;

// 把 3 组输出解码成候选框（模型坐标），只做阈值过滤，不做 NMS。
// 过滤规则和 model_zoo 的 process_i8 一样：分数和 ≥ 阈值（有这个输出时）且 最大类别分数 > 阈值，都在 int8 上比。
// 返回 false = 参数不对（空指针、dflBinCount 超范围）。
bool decodeYolov8OutputBranches(const std::vector<Yolov8OutputBranch> &outputBranches, float boxThreshold,
                                std::vector<CandidateObject> &candidateObjects);

// 一条边的 DFL：dflBinCount 个 float（已反量化）→ softmax → Σ 概率 × bin 下标，单位是“格子”
float computeDflDistance(const float *binValues, int dflBinCount);

// 完整后处理：decode → suppressOverlappingBoxes → mapBoxToSourcePicture，和 runYolov5PostProcess 一样的出口
bool runYolov8PostProcess(const std::vector<Yolov8OutputBranch> &outputBranches, const LetterboxInfo &letterboxInfo,
                          int sourceWidth, int sourceHeight, float boxThreshold, float nmsThreshold,
                          std::vector<DetectedObject> &detectedObjects);
