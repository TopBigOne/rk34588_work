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

// M2
constexpr const char *kEventDirectory = "/userdata/n10/events";            // 事件文件：<目录>/ch0/20270315_143005.mp4
constexpr const char *kEventDatabasePath = "/userdata/n10/record_index.db";  // 和 N3 的录像索引同一个库（events 表）
constexpr int64_t kPreRecordDurationUs = 10'000'000;   // 事件开始前保留 10 秒
constexpr int64_t kPostRollDurationUs = 10'000'000;    // 人消失后再录 10 秒
constexpr int kStartConfirmDetectionCount = 3;         // 连续 3 次检测到人才算事件开始
constexpr int kBoxLineWidthPixels = 4;
constexpr int kDumpBoxedFrameCount = 3;                // 存几帧画好框的 NV12
constexpr int kDumpEveryNthPersonFrame = 30;           // 每隔多少个“有人的检测帧”存一帧
constexpr const char *kDumpBoxedPathPattern = "/userdata/n10/m2_boxed_%d_%dx%d.nv12";

// M3
constexpr const char *kDefaultYolov8ModelPath = "/userdata/n10/model/yolov8n.rknn";  // rk-dev 转出来的或正点原子例程自带的
constexpr int kBenchmarkFrameCount = 300;        // 每种配置跑多少帧（从文件头开始解码）
constexpr int kBenchmarkQueueCapacityFrameCount = 8;  // 3 个 worker 时队列要比 worker 多，不然 worker 会空等
constexpr float kCandidateCompareTolerancePixels = 0.5f;  // v8 解码和 demo 对比（模型坐标、float）
constexpr int kFaultReleaseLoopCount = 300;      // m3fault norelease：连续跑多少次不 release
