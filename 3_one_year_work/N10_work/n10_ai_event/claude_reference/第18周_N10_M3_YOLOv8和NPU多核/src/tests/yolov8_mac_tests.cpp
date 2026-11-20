// YOLOv8 后处理的 Mac 单测：DFL、造的 int8 输出解码、和 model_zoo process_i8 对比、6 输出（没有分数和）的模型
#include <cmath>
#include <cstdio>
#include <vector>

#include "app/post_process_comparison.h"
#include "npu/letterbox.h"
#include "npu/yolov8_post_process.h"
#include "tests/check.h"
#include "third_party/rknn_model_zoo_yolov8/yolov8_demo_decode.h"

static bool isNearV8(float first, float second, float tolerance) {
    return std::fabs(first - second) <= tolerance;
}

static void testDflDistance() {
    printf("YOLOv8 DFL\n");
    float equalBins[16] = {};
    CHECK(isNearV8(computeDflDistance(equalBins, 16), 7.5f, 1e-4f));  // 均匀分布：期望 = (0 + 15) / 2
    float peakedBins[16] = {};
    peakedBins[3] = 20.0f;
    CHECK(isNearV8(computeDflDistance(peakedBins, 16), 3.0f, 1e-3f));
    float twoPeakBins[16] = {};
    twoPeakBins[2] = 30.0f;
    twoPeakBins[4] = 30.0f;
    CHECK(isNearV8(computeDflDistance(twoPeakBins, 16), 3.0f, 1e-3f));  // 两个 bin 各一半：落在中间
    float hugeBins[16] = {};
    hugeBins[5] = 1000.0f;  // 不减最大值时 exp(1000) = inf，结果是 NaN
    CHECK(isNearV8(computeDflDistance(hugeBins, 16), 5.0f, 1e-3f));
}

// 造 3 组 int8 输出（NCHW）：框 [1, 64, H, W]、分数 [1, 80, H, W]、分数和 [1, 1, H, W]
struct FakeYolov8Outputs {
    std::vector<std::vector<int8_t>> boxBuffers;
    std::vector<std::vector<int8_t>> scoreBuffers;
    std::vector<std::vector<int8_t>> scoreSumBuffers;
    std::vector<Yolov8OutputBranch> branches;
};

static const TensorQuantInfo kBoxQuant{0, 0.1f};            // int8 100 → 10.0
static const TensorQuantInfo kScoreQuant{-128, 1.0f / 255};  // int8 -128 → 0.0，127 → 1.0

static int8_t quantizeScore(float score) {
    return static_cast<int8_t>(std::lround(score * 255.0f) - 128);
}

static FakeYolov8Outputs makeEmptyYolov8Outputs() {
    FakeYolov8Outputs outputs;
    const int gridSizes[3] = {8, 4, 2};
    const int strides[3] = {8, 16, 32};
    for (int branchIndex = 0; branchIndex < 3; branchIndex++) {
        int cellCount = gridSizes[branchIndex] * gridSizes[branchIndex];
        outputs.boxBuffers.emplace_back(64 * cellCount, 0);
        outputs.scoreBuffers.emplace_back(kYolov8ClassCount * cellCount, -128);
        outputs.scoreSumBuffers.emplace_back(cellCount, -128);
    }
    for (int branchIndex = 0; branchIndex < 3; branchIndex++) {  // 先把 vector 都建好再取指针，不怕扩容搬家
        Yolov8OutputBranch branch;
        branch.boxData = outputs.boxBuffers[branchIndex].data();
        branch.boxQuantInfo = kBoxQuant;
        branch.classScoreData = outputs.scoreBuffers[branchIndex].data();
        branch.classScoreQuantInfo = kScoreQuant;
        branch.scoreSumData = outputs.scoreSumBuffers[branchIndex].data();
        branch.scoreSumQuantInfo = kScoreQuant;
        branch.gridWidth = gridSizes[branchIndex];
        branch.gridHeight = gridSizes[branchIndex];
        branch.stridePixels = strides[branchIndex];
        branch.dflBinCount = 16;
        outputs.branches.push_back(branch);
    }
    return outputs;
}

// 在某个格子放一个目标：类别分数、分数和、四条边（左上右下，单位：格子）各在一个 bin 上放 10.0
static void putObject(FakeYolov8Outputs &outputs, int branchIndex, int gridX, int gridY, int classIndex, float score,
                      float scoreSum, const int sideBinIndexes[4]) {
    const Yolov8OutputBranch &branch = outputs.branches[branchIndex];
    int cellCount = branch.gridWidth * branch.gridHeight;
    int cellOffset = gridY * branch.gridWidth + gridX;
    outputs.scoreBuffers[branchIndex][classIndex * cellCount + cellOffset] = quantizeScore(score);
    outputs.scoreSumBuffers[branchIndex][cellOffset] = quantizeScore(scoreSum);
    for (int sideIndex = 0; sideIndex < 4; sideIndex++) {
        int channelIndex = sideIndex * 16 + sideBinIndexes[sideIndex];
        outputs.boxBuffers[branchIndex][channelIndex * cellCount + cellOffset] = 100;
    }
}

static void testYolov8DecodeAgainstDemo() {
    printf("YOLOv8 解码 + 和 model_zoo 对比\n");
    FakeYolov8Outputs outputs = makeEmptyYolov8Outputs();
    const int personSides[4] = {2, 1, 3, 4};
    putObject(outputs, 0, 3, 2, 0, 0.9f, 0.9f, personSides);      // stride 8 格子 (3, 2) 一个 person
    const int bicycleSides[4] = {1, 1, 1, 1};
    putObject(outputs, 2, 0, 1, 1, 0.8f, 0.85f, bicycleSides);    // stride 32 格子 (0, 1) 一个 bicycle
    const int lowSides[4] = {1, 1, 1, 1};
    putObject(outputs, 1, 1, 1, 2, 0.2f, 0.2f, lowSides);         // 分数 0.2 < 阈值 0.25：不要
    putObject(outputs, 1, 2, 2, 3, 0.6f, 0.1f, lowSides);         // 分数够，但分数和 0.1 太低：整格跳过（demo 也是）

    std::vector<CandidateObject> ownCandidates;
    std::vector<CandidateObject> demoCandidates;
    CHECK(decodeYolov8OutputBranches(outputs.branches, 0.25f, ownCandidates));
    CHECK(runDemoYolov8Decode(outputs.branches, 0.25f, demoCandidates));
    CHECK(ownCandidates.size() == 2);
    PostProcessComparison comparison = compareCandidateObjects(ownCandidates, demoCandidates, 0.01f);
    CHECK(comparison.isSame);
    printf("  自己 %d 个，model_zoo %d 个，配上 %d 个\n", comparison.ownObjectCount, comparison.demoObjectCount,
           comparison.matchedObjectCount);

    // person：中心 (3.5, 2.5) 格，左 2、上 1、右 3、下 4 → (12, 12)～(52, 52)；bin 不是严格 one-hot，差 0.02 像素左右
    if (ownCandidates.size() == 2) {
        const CandidateObject &person = ownCandidates[0].classIndex == 0 ? ownCandidates[0] : ownCandidates[1];
        CHECK(person.classIndex == 0);
        CHECK(isNearV8(person.confidence, 0.9f, 0.01f));
        CHECK(isNearV8(person.left, 12.0f, 0.1f) && isNearV8(person.top, 12.0f, 0.1f));
        CHECK(isNearV8(person.right, 52.0f, 0.1f) && isNearV8(person.bottom, 52.0f, 0.1f));
        // bicycle：stride 32，中心 (0.5, 1.5) 格，四边各 1 → (-16, 16)～(48, 80)。
        // 其余 15 个 bin 是 0 不是负无穷：距离 = (1·e^10 + 119) / (e^10 + 15) ≈ 1.0047 格，× 32 ≈ 偏 0.15 像素
        const CandidateObject &bicycle = ownCandidates[0].classIndex == 1 ? ownCandidates[0] : ownCandidates[1];
        CHECK(isNearV8(bicycle.left, -16.0f, 0.5f) && isNearV8(bicycle.bottom, 80.0f, 0.5f));
    }

    // 完整后处理：模型 64×64 → 原图 128×128（scale 0.5），按分数排好，框夹到图片里
    LetterboxInfo letterboxInfo = computeLetterboxInfo(128, 128, 64, 64, true);
    std::vector<DetectedObject> detectedObjects;
    CHECK(runYolov8PostProcess(outputs.branches, letterboxInfo, 128, 128, 0.25f, 0.45f, detectedObjects));
    CHECK(detectedObjects.size() == 2);
    if (detectedObjects.size() == 2) {
        CHECK(detectedObjects[0].classIndex == 0 && detectedObjects[1].classIndex == 1);
        CHECK(std::abs(detectedObjects[0].box.left - 24) <= 1 && std::abs(detectedObjects[0].box.right - 104) <= 1);
        CHECK(detectedObjects[1].box.left == 0);  // -16 × 2 夹到 0
    }

    // 6 个输出的模型（没有分数和）：分数和低的那一格不再被跳过，多出 class 3
    for (Yolov8OutputBranch &branch : outputs.branches) {
        branch.scoreSumData = nullptr;
    }
    CHECK(decodeYolov8OutputBranches(outputs.branches, 0.25f, ownCandidates));
    CHECK(runDemoYolov8Decode(outputs.branches, 0.25f, demoCandidates));
    CHECK(ownCandidates.size() == 3 && compareCandidateObjects(ownCandidates, demoCandidates, 0.01f).isSame);

    // 参数不对
    outputs.branches[0].dflBinCount = 64;
    CHECK(!decodeYolov8OutputBranches(outputs.branches, 0.25f, ownCandidates));
}

void runYolov8MacTests() {
    testDflDistance();
    testYolov8DecodeAgainstDemo();
}
