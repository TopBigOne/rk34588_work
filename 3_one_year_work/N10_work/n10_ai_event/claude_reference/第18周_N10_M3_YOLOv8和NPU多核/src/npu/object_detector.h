#pragma once

#include <cstdint>
#include <vector>

#include <rockchip/rk_mpi.h>

#include "common/detection_types.h"
#include "npu/rknn_model.h"
#include "npu/yolov5_post_process.h"
#include "npu/yolov8_post_process.h"
#include "rga/rga_preprocessor.h"

enum class YoloVersion { V5, V8 };

struct ObjectDetectorConfig {
    YoloVersion yoloVersion = YoloVersion::V5;
    bool isLetterboxEnabled = true;  // false = 直接拉伸（人会被压扁，但不浪费上下灰边的算力）
    float boxThreshold = 0.25f;
    float nmsThreshold = 0.45f;
    uint8_t letterboxPadValue = 114;  // YOLOv5 训练时 letterbox 补边用的灰色
};

// 解码帧进、检测结果出：RgaPreprocessor + RknnModel + 后处理。一个对象只在一个线程里用。
class ObjectDetector {
public:
    bool init(const char *requestedModelPath, const ObjectDetectorConfig &requestedConfig);
    // 和 sourceDetector 共用模型权重（rknn_dup_context），配置照抄；RGA 目标 buffer 自己一份。sourceDetector 要活得更久
    bool initSharingModel(ObjectDetector &sourceDetector);
    bool setNpuCoreMask(rknn_core_mask coreMask);
    bool detect(MppFrame decodedFrame, DetectionResult &detectionResult);  // 填 objects、timing、ptsUs、宽高
    bool saveLastModelInput(const char *filePath) const;  // 存最近一次 detect 的 640×640 RGB（检测线程里调）
    const RknnModel &getModel() const { return model; }
    // 把当前输出包装成 3 个 Yolov5OutputTensor；形状不对返回 false。要在 run 之后、releaseOutputs 之前调
    static bool buildYolov5OutputTensors(const RknnModel &model, std::vector<Yolov5OutputTensor> &outputTensors);
    // 6 或 9 个输出（每个尺度 框 / 类别分数 / 可选的分数和），按 stride 8 / 16 / 32 排；形状不对返回 false
    static bool buildYolov8OutputBranches(const RknnModel &model, std::vector<Yolov8OutputBranch> &outputBranches);

private:
    bool runPostProcess(const LetterboxInfo &letterboxInfo, DetectionResult &detectionResult);

    ObjectDetectorConfig config;
    RknnModel model;
    RgaPreprocessor preprocessor;
};
