#include "npu/nms.h"

#include <algorithm>

float computeIoU(const CandidateObject &firstObject, const CandidateObject &secondObject) {
    float overlapWidth = std::min(firstObject.right, secondObject.right) - std::max(firstObject.left, secondObject.left);
    float overlapHeight = std::min(firstObject.bottom, secondObject.bottom) - std::max(firstObject.top, secondObject.top);
    if (overlapWidth <= 0 || overlapHeight <= 0) {
        return 0.0f;
    }
    float overlapArea = overlapWidth * overlapHeight;
    float firstArea = (firstObject.right - firstObject.left) * (firstObject.bottom - firstObject.top);
    float secondArea = (secondObject.right - secondObject.left) * (secondObject.bottom - secondObject.top);
    float unionArea = firstArea + secondArea - overlapArea;
    // demo 的 CalculateOverlap 宽高各 +1（把坐标当成“包含两端的像素下标”），这里按连续坐标算，不加 1；
    // 差别只在 IoU 恰好贴着阈值时才会影响结果
    return unionArea <= 0 ? 0.0f : overlapArea / unionArea;
}

void suppressOverlappingBoxes(std::vector<CandidateObject> &candidateObjects, float iouThreshold) {
    // stable_sort：分数相同时保持原来的先后，结果可复现
    std::stable_sort(candidateObjects.begin(), candidateObjects.end(),
                     [](const CandidateObject &first, const CandidateObject &second) {
                         return first.confidence > second.confidence;
                     });
    std::vector<bool> isSuppressed(candidateObjects.size(), false);
    std::vector<CandidateObject> keptObjects;
    for (size_t keptIndex = 0; keptIndex < candidateObjects.size(); keptIndex++) {
        if (isSuppressed[keptIndex]) {
            continue;
        }
        const CandidateObject &keptObject = candidateObjects[keptIndex];
        keptObjects.push_back(keptObject);
        for (size_t laterIndex = keptIndex + 1; laterIndex < candidateObjects.size(); laterIndex++) {
            if (isSuppressed[laterIndex] || candidateObjects[laterIndex].classIndex != keptObject.classIndex) {
                continue;
            }
            if (computeIoU(keptObject, candidateObjects[laterIndex]) > iouThreshold) {
                isSuppressed[laterIndex] = true;
            }
        }
    }
    candidateObjects.swap(keptObjects);
}
