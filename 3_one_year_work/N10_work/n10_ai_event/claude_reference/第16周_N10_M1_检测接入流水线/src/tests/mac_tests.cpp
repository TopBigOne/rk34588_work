// Mac 上跑的单测：只测不依赖 MPP / RGA / RKNN 的部分（letterbox 几何、IoU、NMS、YOLOv5 解码、和 demo 后处理对比）
#include <cmath>
#include <cstdio>
#include <vector>

#include "app/post_process_comparison.h"
#include "npu/letterbox.h"
#include "npu/nms.h"
#include "npu/yolov5_post_process.h"
#include "third_party/rknn_yolov5_demo/postprocess.h"

static int g_failedCheckCount = 0;
static int g_passedCheckCount = 0;

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (condition) {                                                                                               \
            g_passedCheckCount++;                                                                                      \
        } else {                                                                                                       \
            g_failedCheckCount++;                                                                                      \
            printf("  [FAIL] %s:%d  %s\n", __FILE__, __LINE__, #condition);                                            \
        }                                                                                                              \
    } while (0)

static bool isNear(float first, float second, float tolerance = 1e-3f) {
    return std::fabs(first - second) <= tolerance;
}

static void testLetterboxGeometry() {
    printf("letterbox 几何\n");
    LetterboxInfo letterboxInfo = computeLetterboxInfo(1920, 1080, 640, 640, true);
    CHECK(isNear(letterboxInfo.scaleX, 1.0f / 3) && isNear(letterboxInfo.scaleY, 1.0f / 3));
    CHECK(letterboxInfo.resizedWidth == 640 && letterboxInfo.resizedHeight == 360);
    CHECK(letterboxInfo.padLeftPixels == 0 && letterboxInfo.padTopPixels == 140);

    LetterboxInfo stretchInfo = computeLetterboxInfo(1920, 1080, 640, 640, false);
    CHECK(isNear(stretchInfo.scaleX, 1.0f / 3) && isNear(stretchInfo.scaleY, 640.0f / 1080));
    CHECK(stretchInfo.padLeftPixels == 0 && stretchInfo.padTopPixels == 0);

    // 竖图：左右补边
    LetterboxInfo portraitInfo = computeLetterboxInfo(810, 1080, 640, 640, true);
    CHECK(portraitInfo.resizedHeight == 640 && portraitInfo.resizedWidth == 480 && portraitInfo.padLeftPixels == 80);

    // 模型坐标 (100, 240)～(200, 340) → 原图：x × 3，(y - 140) × 3
    CandidateObject modelObject;
    modelObject.left = 100;
    modelObject.top = 240;
    modelObject.right = 200;
    modelObject.bottom = 340;
    DetectionBox sourceBox = mapBoxToSourcePicture(modelObject, letterboxInfo, 1920, 1080);
    CHECK(sourceBox.left == 300 && sourceBox.top == 300 && sourceBox.right == 600 && sourceBox.bottom == 600);
    // 落在灰边里的部分夹到图片范围内
    modelObject.top = 100;
    modelObject.bottom = 520;
    sourceBox = mapBoxToSourcePicture(modelObject, letterboxInfo, 1920, 1080);
    CHECK(sourceBox.top == 0 && sourceBox.bottom == 1079);
    // 拉伸：y 按 1080/640 放大
    modelObject.top = 320;
    modelObject.bottom = 640;
    sourceBox = mapBoxToSourcePicture(modelObject, stretchInfo, 1920, 1080);
    CHECK(sourceBox.top == 540 && sourceBox.bottom == 1079);
}

static void testCpuLetterboxPixels() {
    printf("CPU letterbox 像素\n");
    // 4×2 的图放进 4×4：scale = 1，上下各补 1 行灰
    std::vector<uint8_t> sourceRgb(4 * 2 * 3);
    for (size_t byteIndex = 0; byteIndex < sourceRgb.size(); byteIndex++) {
        sourceRgb[byteIndex] = static_cast<uint8_t>(byteIndex * 10);
    }
    LetterboxInfo letterboxInfo = computeLetterboxInfo(4, 2, 4, 4, true);
    CHECK(letterboxInfo.padTopPixels == 1 && letterboxInfo.resizedHeight == 2);
    std::vector<uint8_t> modelInputRgb;
    letterboxRgbOnCpu(sourceRgb.data(), 4, 2, letterboxInfo, 4, 4, 114, modelInputRgb);
    CHECK(modelInputRgb.size() == 4 * 4 * 3);
    bool isPadRowGrey = true;
    for (int byteIndex = 0; byteIndex < 12; byteIndex++) {
        isPadRowGrey = isPadRowGrey && modelInputRgb[byteIndex] == 114 && modelInputRgb[36 + byteIndex] == 114;
    }
    CHECK(isPadRowGrey);
    bool isImageCopiedExactly = true;  // scale = 1 时双线性的采样点正好落在原像素中心，结果应该原样
    for (int byteIndex = 0; byteIndex < 24; byteIndex++) {
        isImageCopiedExactly = isImageCopiedExactly && modelInputRgb[12 + byteIndex] == sourceRgb[byteIndex];
    }
    CHECK(isImageCopiedExactly);
}

static CandidateObject makeCandidate(int classIndex, float confidence, float left, float top, float right,
                                     float bottom) {
    CandidateObject candidateObject;
    candidateObject.classIndex = classIndex;
    candidateObject.confidence = confidence;
    candidateObject.left = left;
    candidateObject.top = top;
    candidateObject.right = right;
    candidateObject.bottom = bottom;
    return candidateObject;
}

static void testIoUAndNms() {
    printf("IoU 和 NMS\n");
    CandidateObject boxA = makeCandidate(0, 0.9f, 0, 0, 10, 10);
    CHECK(isNear(computeIoU(boxA, boxA), 1.0f));
    CHECK(isNear(computeIoU(boxA, makeCandidate(0, 0.5f, 20, 20, 30, 30)), 0.0f));
    CHECK(isNear(computeIoU(boxA, makeCandidate(0, 0.5f, 10, 0, 20, 10)), 0.0f));  // 只碰边，不算重叠
    CHECK(isNear(computeIoU(boxA, makeCandidate(0, 0.5f, 5, 0, 15, 10)), 50.0f / 150.0f));

    std::vector<CandidateObject> candidateObjects = {
            makeCandidate(0, 0.6f, 1, 0, 11, 10),    // 和最高分的 person IoU 0.82 → 删
            makeCandidate(0, 0.9f, 0, 0, 10, 10),    // 最高分 person → 留
            makeCandidate(2, 0.7f, 0, 0, 10, 10),    // 车，和 person 完全重合，但类别不同 → 留
            makeCandidate(0, 0.8f, 50, 50, 60, 60),  // 另一个 person，不重叠 → 留
            makeCandidate(2, 0.3f, 0, 1, 10, 11),    // 和车 IoU 0.82 → 删
    };
    suppressOverlappingBoxes(candidateObjects, 0.45f);
    CHECK(candidateObjects.size() == 3);
    if (candidateObjects.size() == 3) {
        CHECK(isNear(candidateObjects[0].confidence, 0.9f) && candidateObjects[0].classIndex == 0);
        CHECK(isNear(candidateObjects[1].confidence, 0.8f) && candidateObjects[1].classIndex == 0);
        CHECK(isNear(candidateObjects[2].confidence, 0.7f) && candidateObjects[2].classIndex == 2);
    }
}

// 造 YOLOv5 的 3 个 int8 输出：全部填 0 概率，再在指定格子放目标
struct FakeYolov5Outputs {
    static constexpr int32_t kZeroPoint = -128;
    static constexpr float kScale = 1.0f / 255;  // int8 -128～127 对应 0～1
    std::vector<int8_t> tensorBytes[3];
    std::vector<Yolov5OutputTensor> tensors;

    FakeYolov5Outputs() {
        const int strides[3] = {8, 16, 32};
        for (int outputIndex = 0; outputIndex < 3; outputIndex++) {
            int gridSize = 640 / strides[outputIndex];
            tensorBytes[outputIndex].assign(static_cast<size_t>(kYolov5AnchorCountPerGrid) * kYolov5ValuesPerAnchor *
                                                    gridSize * gridSize,
                                            quantize(0.0f));
            Yolov5OutputTensor tensor;
            tensor.data = tensorBytes[outputIndex].data();
            tensor.quantInfo.zeroPoint = kZeroPoint;
            tensor.quantInfo.scale = kScale;
            tensor.gridWidth = gridSize;
            tensor.gridHeight = gridSize;
            tensor.stridePixels = strides[outputIndex];
            tensors.push_back(tensor);
        }
    }

    static int8_t quantize(float value) { return static_cast<int8_t>(std::lround(value / kScale) + kZeroPoint); }

    // sigma 取 0.5：中心 = (格子 + 0.5) × stride，宽高 = anchor 大小
    void placeObject(int outputIndex, int anchorIndex, int gridX, int gridY, int classIndex, float objectConfidence,
                     float classScore) {
        int gridSize = tensors[outputIndex].gridWidth;
        int gridCellCount = gridSize * gridSize;
        int8_t *anchorBase = tensorBytes[outputIndex].data() + kYolov5ValuesPerAnchor * anchorIndex * gridCellCount;
        auto setValue = [&](int channelIndex, float value) {
            anchorBase[channelIndex * gridCellCount + gridY * gridSize + gridX] = quantize(value);
        };
        for (int channelIndex = 0; channelIndex < 4; channelIndex++) {
            setValue(channelIndex, 0.5f);
        }
        setValue(4, objectConfidence);
        setValue(5 + classIndex, classScore);
    }
};

static void testYolov5DecodeAgainstDemo() {
    printf("YOLOv5 解码 + 和 demo 后处理对比\n");
    LetterboxInfo letterboxInfo = computeLetterboxInfo(1920, 1080, 640, 640, true);

    // 场景 1：只有 person。stride 32 两个相邻格子（同一个人被检了两次，IoU 约 0.66）+ stride 16、stride 8 各一个
    FakeYolov5Outputs singleClassOutputs;
    singleClassOutputs.placeObject(2, 1, 5, 9, 0, 0.9f, 0.9f);
    singleClassOutputs.placeObject(2, 1, 6, 9, 0, 0.8f, 0.8f);
    singleClassOutputs.placeObject(1, 2, 10, 20, 0, 0.8f, 0.7f);
    singleClassOutputs.placeObject(0, 0, 60, 40, 0, 0.6f, 0.5f);
    std::vector<CandidateObject> candidateObjects;
    CHECK(decodeYolov5OutputTensors(singleClassOutputs.tensors, 0.25f, candidateObjects));
    CHECK(candidateObjects.size() == 4);
    // stride 32 格子 (5, 9) anchor 1 = 156×198：中心约 (176, 304)。
    // 0.5 量化成 int8 再还原是 128/255 = 0.502，中心偏 0.25 像素、宽高大 0.8%，所以容差给 1.5 像素
    bool hasExpectedBox = false;
    for (const CandidateObject &candidateObject : candidateObjects) {
        if (isNear(candidateObject.left, 176 - 78, 1.5f) && isNear(candidateObject.top, 304 - 99, 1.5f) &&
            isNear(candidateObject.right, 176 + 78, 1.5f) && isNear(candidateObject.bottom, 304 + 99, 1.5f)) {
            hasExpectedBox = true;
            CHECK(isNear(candidateObject.confidence, 0.81f, 0.01f));
        }
    }
    CHECK(hasExpectedBox);

    std::vector<DetectedObject> ownObjects;
    std::vector<DetectedObject> demoObjects;
    CHECK(runYolov5PostProcess(singleClassOutputs.tensors, letterboxInfo, 1920, 1080, 0.25f, 0.45f, ownObjects));
    CHECK(runDemoPostProcess(singleClassOutputs.tensors, 640, 640, letterboxInfo, demoObjects));
    PostProcessComparison comparison = comparePostProcessResults(ownObjects, demoObjects, 2);
    printf("  场景 1：自己 %d 个，demo %d 个，配上 %d 个，最大误差 %d 像素\n", comparison.ownObjectCount,
           comparison.demoObjectCount, comparison.matchedObjectCount, comparison.maxCoordinateDiffPixels);
    CHECK(comparison.ownObjectCount == 3);
    CHECK(comparison.isSame);

    // 场景 2：相邻两个格子分别是 person 和 bicycle（类别不同、IoU 约 0.66）。按类别 NMS 应该两个都留
    FakeYolov5Outputs twoClassOutputs;
    twoClassOutputs.placeObject(2, 1, 5, 9, 0, 0.9f, 0.9f);
    twoClassOutputs.placeObject(2, 1, 6, 9, 1, 0.8f, 0.8f);
    CHECK(runYolov5PostProcess(twoClassOutputs.tensors, letterboxInfo, 1920, 1080, 0.25f, 0.45f, ownObjects));
    CHECK(ownObjects.size() == 2);
    CHECK(runDemoPostProcess(twoClassOutputs.tensors, 640, 640, letterboxInfo, demoObjects));
    printf("  场景 2（不同类别重叠）：自己 %zu 个，demo %zu 个（只打印，不判断）\n", ownObjects.size(),
           demoObjects.size());

    // 形状不对要报错
    std::vector<Yolov5OutputTensor> badTensors = singleClassOutputs.tensors;
    badTensors[0].stridePixels = 12;
    CHECK(!decodeYolov5OutputTensors(badTensors, 0.25f, candidateObjects));
}

int main() {
    testLetterboxGeometry();
    testCpuLetterboxPixels();
    testIoUAndNms();
    testYolov5DecodeAgainstDemo();
    deinitPostProcess();
    printf("通过 %d 项，失败 %d 项\n", g_passedCheckCount, g_failedCheckCount);
    return g_failedCheckCount == 0 ? 0 : 1;
}
