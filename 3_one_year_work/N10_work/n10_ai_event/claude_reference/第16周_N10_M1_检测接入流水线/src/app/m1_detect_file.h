#pragma once

// M1：H.264 文件 → [解码线程] MPP 解码 → FrameQueue → [检测线程] RGA 前处理 → RKNN → 自己的后处理
// 每帧打印一行结果和分段耗时，结束时打印耗时表。参数传 nullptr 用 app_config.h 的默认值。
int runM1DetectFile(const char *requestedStreamPath, const char *requestedModelPath, bool isLetterboxEnabled);

// M1 对比：同一张图、同一次推理输出，自己的后处理和 SDK demo 的后处理结果比一比
int runM1ComparePostProcess(const char *requestedModelPath, const char *requestedImagePath);
