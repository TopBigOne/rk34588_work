#pragma once

#include <string>
#include <vector>

#include "common/detection_types.h"
#include "npu/letterbox.h"
#include "npu/yolov5_post_process.h"

// 自己的后处理 vs SDK demo 的 postprocess.cc，同一组输出张量对比（计划 M1 验收：框数、类别相同，坐标误差 < 2 像素）。
// 不依赖 RKNN：板子上的 m1cmp 和 Mac 单测共用。

// 调 demo 的 post_process，把结果转成 DetectedObject。demo 只认 3 个输出，结果最多 64 个
bool runDemoPostProcess(const std::vector<Yolov5OutputTensor> &outputTensors, int modelWidth, int modelHeight,
                        const LetterboxInfo &letterboxInfo, std::vector<DetectedObject> &demoObjects,
                        std::vector<std::string> *labelNames = nullptr);

struct PostProcessComparison {
    int ownObjectCount = 0;
    int demoObjectCount = 0;
    int matchedObjectCount = 0;  // 类别相同、四条边误差都 ≤ 容差的配对数
    int maxCoordinateDiffPixels = 0;  // 配上的那些里，四条边最大的差
    bool isSame = false;          // 两边个数相同，而且全部配上
};

PostProcessComparison comparePostProcessResults(const std::vector<DetectedObject> &ownObjects,
                                                const std::vector<DetectedObject> &demoObjects,
                                                int tolerancePixels);
