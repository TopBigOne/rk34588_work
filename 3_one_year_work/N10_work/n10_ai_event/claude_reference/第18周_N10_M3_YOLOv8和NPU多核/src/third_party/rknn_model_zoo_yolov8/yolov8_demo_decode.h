#pragma once

#include <vector>

#include "npu/candidate_object.h"
#include "npu/yolov8_post_process.h"

// model_zoo 的 process_i8（原样复制）跑一遍 3 组输出，结果转成 CandidateObject（模型坐标、NMS 之前）。
// 用途：Mac 单测和板子上的 m3cmp，和自己的 decodeYolov8OutputBranches 逐个比。
bool runDemoYolov8Decode(const std::vector<Yolov8OutputBranch> &outputBranches, float boxThreshold,
                         std::vector<CandidateObject> &candidateObjects);
