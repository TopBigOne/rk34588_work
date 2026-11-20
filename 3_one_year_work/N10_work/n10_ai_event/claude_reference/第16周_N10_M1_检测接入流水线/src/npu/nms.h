#pragma once

#include <vector>

#include "npu/candidate_object.h"

// 交并比：两个框重叠面积 / 两个框合起来的面积，0 = 不相交，1 = 完全重合
float computeIoU(const CandidateObject &firstObject, const CandidateObject &secondObject);

// 按类别做非极大值抑制：按 confidence 从高到低，同一类别里和已保留的框 IoU > iouThreshold 的删掉。
// 不同类别的框重叠不互相抑制（人站在车前面，两个框都要留）。结果按 confidence 从高到低排好。
void suppressOverlappingBoxes(std::vector<CandidateObject> &candidateObjects, float iouThreshold);
