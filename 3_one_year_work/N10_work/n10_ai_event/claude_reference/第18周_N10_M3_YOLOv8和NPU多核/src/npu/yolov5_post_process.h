#pragma once

#include <cstdint>
#include <vector>

#include "common/detection_types.h"
#include "npu/candidate_object.h"
#include "npu/letterbox.h"
#include "npu/tensor_quant_info.h"

// YOLOv5 的一个输出张量（int8，NCHW）：[1, 3 × (5 + 类别数), gridHeight, gridWidth]
// 3 = 每个格子 3 个 anchor；5 = x、y、w、h、目标置信度
struct Yolov5OutputTensor {
    const int8_t *data = nullptr;
    TensorQuantInfo quantInfo;
    int gridWidth = 0;   // 80 / 40 / 20（640 输入时）
    int gridHeight = 0;
    int stridePixels = 0;  // 一个格子对应模型输入多少像素：8 / 16 / 32
};

constexpr int kYolov5ClassCount = 80;
constexpr int kYolov5AnchorCountPerGrid = 3;
constexpr int kYolov5ValuesPerAnchor = 5 + kYolov5ClassCount;

// 把 3 个输出解码成候选框（模型坐标），只做阈值过滤，不做 NMS。
// 过滤规则和 demo postprocess.cc 的 process() 一样：目标置信度 ≥ 阈值 且 最大类别分数 > 阈值（都在 int8 上比），
// 输出 confidence = 类别分数 × 目标置信度（可能低于阈值，demo 也不再过滤）。
// 返回 false = 张量形状不对。
bool decodeYolov5OutputTensors(const std::vector<Yolov5OutputTensor> &outputTensors, float boxThreshold,
                               std::vector<CandidateObject> &candidateObjects);

// 按 stride 取官方 COCO 预训练模型的 anchor（宽、高各 3 个，单位：模型输入像素）
const int *getYolov5AnchorsForStride(int stridePixels);

// 完整后处理：decodeYolov5OutputTensors → suppressOverlappingBoxes → mapBoxToSourcePicture。
// 输出原图坐标，按 confidence 从高到低。ObjectDetector 和 m1cmp（与 demo 对比）都走这一个函数。
bool runYolov5PostProcess(const std::vector<Yolov5OutputTensor> &outputTensors, const LetterboxInfo &letterboxInfo,
                          int sourceWidth, int sourceHeight, float boxThreshold, float nmsThreshold,
                          std::vector<DetectedObject> &detectedObjects);
