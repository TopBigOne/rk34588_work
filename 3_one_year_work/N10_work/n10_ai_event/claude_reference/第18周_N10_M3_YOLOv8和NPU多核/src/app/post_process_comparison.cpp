#include "app/post_process_comparison.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>

#include "third_party/rknn_yolov5_demo/postprocess.h"

bool runDemoPostProcess(const std::vector<Yolov5OutputTensor> &outputTensors, int modelWidth, int modelHeight,
                        const LetterboxInfo &letterboxInfo, std::vector<DetectedObject> &demoObjects,
                        std::vector<std::string> *labelNames) {
    demoObjects.clear();
    if (outputTensors.size() != 3) {
        return false;
    }
    std::vector<int32_t> zeroPoints;
    std::vector<float> scales;
    for (const Yolov5OutputTensor &tensor : outputTensors) {
        zeroPoints.push_back(tensor.quantInfo.zeroPoint);
        scales.push_back(tensor.quantInfo.scale);
    }
    BOX_RECT pads;
    pads.left = letterboxInfo.padLeftPixels;
    pads.top = letterboxInfo.padTopPixels;
    pads.right = modelWidth - letterboxInfo.resizedWidth - letterboxInfo.padLeftPixels;
    pads.bottom = modelHeight - letterboxInfo.resizedHeight - letterboxInfo.padTopPixels;
    detect_result_group_t resultGroup;
    // demo 的参数不带 const，只读不写，所以 const_cast
    int ret = post_process(const_cast<int8_t *>(outputTensors[0].data), const_cast<int8_t *>(outputTensors[1].data),
                           const_cast<int8_t *>(outputTensors[2].data), modelHeight, modelWidth, BOX_THRESH,
                           NMS_THRESH, pads, letterboxInfo.scaleX, letterboxInfo.scaleY, zeroPoints, scales,
                           &resultGroup);
    if (ret != 0) {
        return false;
    }
    if (labelNames != nullptr) {
        labelNames->clear();
    }
    for (int resultIndex = 0; resultIndex < resultGroup.count; resultIndex++) {
        const detect_result_t &demoResult = resultGroup.results[resultIndex];
        DetectedObject demoObject;
        demoObject.classIndex = demoResult.class_id;
        demoObject.confidence = demoResult.prop;
        demoObject.box = {demoResult.box.left, demoResult.box.top, demoResult.box.right, demoResult.box.bottom};
        demoObjects.push_back(demoObject);
        if (labelNames != nullptr) {
            labelNames->push_back(demoResult.name);
        }
    }
    return true;
}

static int getMaxEdgeDiffPixels(const DetectionBox &firstBox, const DetectionBox &secondBox) {
    return std::max({std::abs(firstBox.left - secondBox.left), std::abs(firstBox.top - secondBox.top),
                     std::abs(firstBox.right - secondBox.right), std::abs(firstBox.bottom - secondBox.bottom)});
}

PostProcessComparison comparePostProcessResults(const std::vector<DetectedObject> &ownObjects,
                                                const std::vector<DetectedObject> &demoObjects,
                                                int tolerancePixels) {
    PostProcessComparison comparison;
    comparison.ownObjectCount = static_cast<int>(ownObjects.size());
    comparison.demoObjectCount = static_cast<int>(demoObjects.size());
    std::vector<bool> isDemoObjectMatched(demoObjects.size(), false);
    // 每个自己的框，找一个还没配过、类别相同、差最小的 demo 框
    for (const DetectedObject &ownObject : ownObjects) {
        int bestDemoIndex = -1;
        int bestDiffPixels = INT_MAX;
        for (size_t demoIndex = 0; demoIndex < demoObjects.size(); demoIndex++) {
            if (isDemoObjectMatched[demoIndex] || demoObjects[demoIndex].classIndex != ownObject.classIndex) {
                continue;
            }
            int diffPixels = getMaxEdgeDiffPixels(ownObject.box, demoObjects[demoIndex].box);
            if (diffPixels < bestDiffPixels) {
                bestDiffPixels = diffPixels;
                bestDemoIndex = static_cast<int>(demoIndex);
            }
        }
        if (bestDemoIndex >= 0 && bestDiffPixels <= tolerancePixels) {
            isDemoObjectMatched[bestDemoIndex] = true;
            comparison.matchedObjectCount++;
            comparison.maxCoordinateDiffPixels = std::max(comparison.maxCoordinateDiffPixels, bestDiffPixels);
        }
    }
    comparison.isSame = comparison.ownObjectCount == comparison.demoObjectCount &&
                        comparison.matchedObjectCount == comparison.ownObjectCount;
    return comparison;
}

static float getMaxEdgeDiff(const CandidateObject &firstObject, const CandidateObject &secondObject) {
    return std::max({std::fabs(firstObject.left - secondObject.left), std::fabs(firstObject.top - secondObject.top),
                     std::fabs(firstObject.right - secondObject.right),
                     std::fabs(firstObject.bottom - secondObject.bottom)});
}

PostProcessComparison compareCandidateObjects(const std::vector<CandidateObject> &ownObjects,
                                              const std::vector<CandidateObject> &demoObjects, float tolerancePixels) {
    PostProcessComparison comparison;
    comparison.ownObjectCount = static_cast<int>(ownObjects.size());
    comparison.demoObjectCount = static_cast<int>(demoObjects.size());
    std::vector<bool> isDemoObjectMatched(demoObjects.size(), false);
    float maxDiffPixels = 0.0f;
    for (const CandidateObject &ownObject : ownObjects) {
        int bestDemoIndex = -1;
        float bestDiffPixels = 1e9f;
        for (size_t demoIndex = 0; demoIndex < demoObjects.size(); demoIndex++) {
            if (isDemoObjectMatched[demoIndex] || demoObjects[demoIndex].classIndex != ownObject.classIndex) {
                continue;
            }
            float diffPixels = getMaxEdgeDiff(ownObject, demoObjects[demoIndex]);
            if (diffPixels < bestDiffPixels) {
                bestDiffPixels = diffPixels;
                bestDemoIndex = static_cast<int>(demoIndex);
            }
        }
        if (bestDemoIndex >= 0 && bestDiffPixels <= tolerancePixels) {
            isDemoObjectMatched[bestDemoIndex] = true;
            comparison.matchedObjectCount++;
            maxDiffPixels = std::max(maxDiffPixels, bestDiffPixels);
        }
    }
    comparison.maxCoordinateDiffPixels = static_cast<int>(std::ceil(maxDiffPixels));
    comparison.isSame = comparison.ownObjectCount == comparison.demoObjectCount &&
                        comparison.matchedObjectCount == comparison.ownObjectCount;
    return comparison;
}
