#pragma once

#include <cstdint>
#include <functional>
#include <vector>

// 检测结果的公开结构：N5 预览画框、事件录像都只认这里的类型（计划第 7.2 节）

struct DetectionBox {
    int left = 0;  // 原图像素坐标（已经从模型坐标换算回来）
    int top = 0;
    int right = 0;
    int bottom = 0;
};

struct DetectedObject {
    int classIndex = 0;  // COCO 80 类的下标，0 = person
    float confidence = 0.0f;
    DetectionBox box;
};

struct DetectionTiming {
    int64_t preprocessUs = 0;
    int64_t inferenceUs = 0;
    int64_t postprocessUs = 0;
};

struct DetectionResult {
    int64_t ptsUs = 0;
    int sourceWidth = 0;
    int sourceHeight = 0;
    std::vector<DetectedObject> objects;
    DetectionTiming timing;
};

using DetectionCallback = std::function<void(const DetectionResult &detectionResult)>;

constexpr int kPersonClassIndex = 0;
