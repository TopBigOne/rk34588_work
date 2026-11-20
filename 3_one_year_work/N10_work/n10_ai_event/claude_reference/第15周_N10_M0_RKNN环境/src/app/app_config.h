#pragma once

// 板子上的路径：run_on_board_with_mac.sh 把 model/ 下的文件 push 到 kBoardModelDirectory
constexpr const char *kBoardModelDirectory = "/userdata/n10/model";
constexpr const char *kDefaultModelPath = "/userdata/n10/model/yolov5s-640-640.rknn";  // SDK demo 自带
constexpr const char *kDefaultImagePath = "/userdata/n10/model/bus.jpg";
// 标签文件路径写在 third_party/rknn_yolov5_demo/postprocess.cc 里（/userdata/n10/model/coco_80_labels_list.txt）

constexpr float kBoxThreshold = 0.25f;  // demo postprocess.h 的 BOX_THRESH
constexpr float kNmsThreshold = 0.45f;  // demo postprocess.h 的 NMS_THRESH
constexpr int kLetterboxPadValue = 114;  // YOLOv5 训练时 letterbox 补边用的灰色
constexpr int kBenchmarkRunCount = 10;   // 和 demo 的 test_count 一样
