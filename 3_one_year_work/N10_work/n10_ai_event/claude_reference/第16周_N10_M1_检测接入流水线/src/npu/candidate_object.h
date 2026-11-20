#pragma once

// 后处理中间结果：还在模型输入坐标系（640×640）里、用 float 存的框。
// 最后一步 mapBoxToSourcePicture 才换算成原图整数坐标（DetectionBox）。全程用 float，避免中途取整再放大 3 倍。
struct CandidateObject {
    int classIndex = 0;
    float confidence = 0.0f;  // 类别分数 × 目标置信度
    float left = 0.0f;        // 模型坐标，像素
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};
