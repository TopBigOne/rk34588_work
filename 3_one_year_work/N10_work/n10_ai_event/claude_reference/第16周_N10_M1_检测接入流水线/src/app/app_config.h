#pragma once

#include <cstdint>

// 板子上的路径：run_on_board_with_mac.sh 把 SDK demo 的 model/ 下的文件 push 到 /userdata/n10/model
constexpr const char *kDefaultModelPath = "/userdata/n10/model/yolov5s-640-640.rknn";  // SDK demo 自带
constexpr const char *kDefaultImagePath = "/userdata/n10/model/bus.jpg";
// 标签文件路径写在 third_party/rknn_yolov5_demo/postprocess.cc 里（/userdata/n10/model/coco_80_labels_list.txt）

constexpr float kBoxThreshold = 0.25f;  // demo postprocess.h 的 BOX_THRESH
constexpr float kNmsThreshold = 0.45f;  // demo postprocess.h 的 NMS_THRESH
constexpr uint8_t kLetterboxPadValue = 114;  // YOLOv5 训练时 letterbox 补边用的灰色
constexpr int kBenchmarkRunCount = 10;   // 和 demo 的 test_count 一样

// M1
constexpr const char *kDefaultStreamPath = "/userdata/n10/people_1080p.h264";  // 有人走动的 1080p H.264 裸流（待准备）
constexpr int kInputFramesPerSecond = 30;    // 裸流没有时间戳，DecodeStage 按这个帧率给每帧打 pts
constexpr int kDetectQueueCapacityFrameCount = 4;
constexpr int kTimingSummaryFrameCount = 100;  // 耗时表统计前多少帧（第 1 帧含预热，单独列出、不算进平均）
constexpr int kDumpRgbFrameIndex = 30;         // 第几帧（从 0 数）的前处理结果存成 .rgb
constexpr const char *kDumpRgbPathPattern = "/userdata/n10/m1_frame%d_%dx%d.rgb";
constexpr int kCompareTolerancePixels = 2;     // 和 demo 后处理对比：四条边误差 ≤ 2 像素算一致
