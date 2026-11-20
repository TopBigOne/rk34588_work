#pragma once

// M0：一张图片 → CPU letterbox 成模型输入 → RKNN 推理 → 官方 demo 的后处理 → 打印框和耗时
// requestedModelPath / requestedImagePath 传 nullptr 用 app_config.h 的默认值
int runM0DetectImage(const char *requestedModelPath, const char *requestedImagePath);
