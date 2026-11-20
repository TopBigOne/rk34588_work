#pragma once

// m3bench：同一模型、同一段视频，比 4 种跑法的吞吐：只解码（基准）/ 1 个 context 绑核 0 / 1 个 context 三核合用 / 3 个 context 各绑一核
int runM3NpuCoreBenchmark(const char *requestedStreamPath, const char *requestedModelPath, const char *yoloVersionText);

// m3models：同一段视频，YOLOv5 和 YOLOv8 各跑一遍（1 个 context 绑核 0），比耗时和检出 person 的帧数
int runM3CompareModels(const char *requestedStreamPath, const char *requestedV5ModelPath,
                       const char *requestedV8ModelPath);

// m3cmp：一张图、YOLOv8 模型，自己的解码和 model_zoo 的 process_i8 对比（NMS 之前的候选框）
int runM3CompareYolov8PostProcess(const char *requestedModelPath, const char *requestedImagePath);

// m3fault：YOLOv5 模型、一张图，故意做错一处看现象：none / swap（输出顺序对调）/ zp（忘了减 zeroPoint）/ norelease
int runM3FaultInjection(const char *faultName, const char *requestedModelPath, const char *requestedImagePath);
