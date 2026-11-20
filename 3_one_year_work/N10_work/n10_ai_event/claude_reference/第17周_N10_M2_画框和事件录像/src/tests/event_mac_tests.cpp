// M2 的 Mac 单测：预录缓冲、状态机、只留最新一帧的槽、NV12 画框、事件索引、事件录像端到端（真码流 aaa.264）
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "common/detection_types.h"
#include "event/event_index.h"
#include "event/event_recorder.h"
#include "event/event_state_machine.h"
#include "event/pre_record_buffer.h"
#include "overlay/nv12_box_drawer.h"
#include "pipeline/latest_slot.h"
#include "source/ffmpeg_frame_source.h"
#include "storage/mp4_probe.h"
#include "tests/check.h"

static const char *const kTestStreamPath = "/Users/dev/Documents/AV/rk_test_data/aaa.264";
constexpr int64_t k90kHzPerSecond = 90000;

static EncodedFrame makeFakeFrame(int frameIndex, int framesPerSecond, int gopFrameCount) {
    EncodedFrame frame;
    frame.frameIndex = frameIndex;
    frame.extendedRtpTimestamp = static_cast<int64_t>(frameIndex) * k90kHzPerSecond / framesPerSecond;
    frame.isKeyFrame = frameIndex % gopFrameCount == 0;
    frame.annexBData.assign(100, 0);
    return frame;
}

static void testPreRecordBuffer() {
    printf("预录缓冲\n");
    // 计划 M2 验收：塞 30 秒（30 fps、GOP 60 = 2 秒），留 10 秒 → 缓冲 10～12 秒，第一帧是 IDR
    PreRecordBuffer buffer(10 * k90kHzPerSecond);
    for (int frameIndex = 0; frameIndex < 30 * 30; frameIndex++) {
        buffer.pushFrame(makeFakeFrame(frameIndex, 30, 60));
    }
    double bufferedSeconds = static_cast<double>(buffer.getBufferedDurationIn90kHz()) / k90kHzPerSecond;
    printf("  缓冲 %.2f 秒，%zu 帧\n", bufferedSeconds, buffer.getFrameCount());
    CHECK(bufferedSeconds >= 10.0 && bufferedSeconds < 12.0);
    CHECK(buffer.startsWithKeyFrame());
    CHECK(buffer.getBufferedBytes() == buffer.getFrameCount() * 100);

    std::vector<EncodedFrame> takenFrames = buffer.takeAll();
    CHECK(!takenFrames.empty() && takenFrames.front().isKeyFrame);
    CHECK(buffer.getFrameCount() == 0 && buffer.getBufferedBytes() == 0);

    // 取走之后，下一个关键帧之前的帧都不收
    buffer.pushFrame(makeFakeFrame(901, 30, 60));
    CHECK(buffer.getFrameCount() == 0);
    buffer.pushFrame(makeFakeFrame(960, 30, 60));
    CHECK(buffer.getFrameCount() == 1 && buffer.startsWithKeyFrame());
}

static void testEventStateMachine() {
    printf("事件状态机\n");
    EventStateConfig stateConfig;
    stateConfig.startConfirmDetectionCount = 3;
    stateConfig.postRollDurationUs = 10'000'000;
    stateConfig.maxEventDurationUs = 60'000'000;
    EventStateMachine stateMachine(stateConfig);

    // 去抖：有人、没人、有人 → 不触发
    stateMachine.onDetectionResult(1, 1'000'000);
    stateMachine.onDetectionResult(0, 1'100'000);
    stateMachine.onDetectionResult(1, 1'200'000);
    CHECK(stateMachine.getState() == EventState::Idle);
    // 连续 3 次 → Recording，开始时间是这一串的第一次
    stateMachine.onDetectionResult(2, 1'300'000);
    CHECK(stateMachine.onDetectionResult(1, 1'400'000) == EventState::Recording);
    CHECK(stateMachine.getEventStartPtsUs() == 1'200'000);
    CHECK(stateMachine.getMaxPersonCount() == 2);
    // 没人 → PostRoll；没到 10 秒不关，又来人 → 回到 Recording（同一个事件）
    CHECK(stateMachine.onDetectionResult(0, 2'000'000) == EventState::PostRoll);
    CHECK(!stateMachine.shouldCloseEvent(11'000'000));
    CHECK(stateMachine.onDetectionResult(1, 11'000'000) == EventState::Recording);
    CHECK(!stateMachine.shouldCloseEvent(30'000'000));  // Recording 时人在，不关
    stateMachine.onDetectionResult(0, 12'000'000);
    CHECK(!stateMachine.shouldCloseEvent(20'999'999));
    CHECK(stateMachine.shouldCloseEvent(21'000'000));    // 最后见到人 11 秒 + 10 秒
    stateMachine.markEventClosed();
    CHECK(stateMachine.getState() == EventState::Idle && stateMachine.getMaxPersonCount() == 0);

    // 人一直在：到最长时长强制关
    for (int detectionIndex = 0; detectionIndex < 3; detectionIndex++) {
        stateMachine.onDetectionResult(1, 100'000'000 + detectionIndex);
    }
    CHECK(!stateMachine.shouldCloseEvent(159'000'000));
    CHECK(stateMachine.shouldCloseEvent(160'000'000));
}

static void testLatestSlot() {
    printf("只留最新一帧的槽\n");
    LatestSlot<std::unique_ptr<int>> slot;
    slot.put(std::make_unique<int>(1));
    slot.put(std::make_unique<int>(2));
    slot.put(std::make_unique<int>(3));
    CHECK(slot.getDroppedCount() == 2 && slot.getPutCount() == 3);
    std::unique_ptr<int> takenItem;
    CHECK(slot.take(takenItem) && takenItem && *takenItem == 3);
    slot.close();
    CHECK(!slot.take(takenItem));
    slot.put(std::make_unique<int>(4));  // 关了以后放不进去
    CHECK(slot.getPutCount() == 3);
}

static void testNv12BoxDrawer() {
    printf("NV12 画框\n");
    const int width = 64;
    const int height = 32;
    const int horStride = 80;  // 比宽多 16 字节，验证按 stride 寻址、不写出界
    std::vector<uint8_t> frameBytes(static_cast<size_t>(horStride) * height * 3 / 2, 0);
    Nv12PlaneView picture;
    picture.yPlane = frameBytes.data();
    picture.uvPlane = frameBytes.data() + static_cast<size_t>(horStride) * height;
    picture.width = width;
    picture.height = height;
    picture.horStride = horStride;

    // 奇数坐标 (11, 5)～(20, 14) → 对齐成 [10, 22) × [4, 16)
    drawNv12Rectangle(picture, DetectionBox{11, 5, 20, 14}, kNv12Green, 2);
    auto lumaAt = [&](int x, int y) { return picture.yPlane[y * horStride + x]; };
    CHECK(lumaAt(10, 4) == kNv12Green.lumaValue && lumaAt(21, 4) == kNv12Green.lumaValue);
    CHECK(lumaAt(10, 15) == kNv12Green.lumaValue && lumaAt(21, 15) == kNv12Green.lumaValue);
    CHECK(lumaAt(11, 10) == kNv12Green.lumaValue);  // 左边线宽 2：x = 10、11
    CHECK(lumaAt(12, 10) == 0);                     // 框里面不动
    CHECK(lumaAt(9, 4) == 0 && lumaAt(22, 4) == 0 && lumaAt(10, 3) == 0 && lumaAt(10, 16) == 0);  // 框外不动
    const uint8_t *uvRowStart = picture.uvPlane + 2 * horStride;  // Y 第 4、5 行对应 UV 第 2 行
    CHECK(uvRowStart[10] == kNv12Green.chromaUValue && uvRowStart[11] == kNv12Green.chromaVValue);

    // 超出画面的框：裁到画面内，stride 多出来的字节不写
    drawNv12Rectangle(picture, DetectionBox{50, 20, 200, 200}, kNv12Red, 4);
    bool isPaddingUntouched = true;
    for (int row = 0; row < height; row++) {
        for (int x = width; x < horStride; x++) {
            isPaddingUntouched = isPaddingUntouched && picture.yPlane[row * horStride + x] == 0;
        }
    }
    CHECK(isPaddingUntouched);
    CHECK(lumaAt(63, 31) == kNv12Red.lumaValue);
}

static void testEventIndex(const std::string &testDirectory) {
    printf("事件索引（SQLite）\n");
    EventIndex eventIndex;
    CHECK(eventIndex.open(testDirectory + "/index_test.db"));
    EventRecord eventRecord;
    eventRecord.channelIndex = 0;
    eventRecord.startTimeMs = 2000;
    eventRecord.endTimeMs = 5000;
    eventRecord.maxPersonCount = 2;
    eventRecord.filePath = "b.mp4";
    CHECK(eventIndex.insertEvent(eventRecord) > 0);
    eventRecord.startTimeMs = 1000;
    eventRecord.filePath = "a.mp4";
    CHECK(eventIndex.insertEvent(eventRecord) > 0);
    eventRecord.channelIndex = 1;
    CHECK(eventIndex.insertEvent(eventRecord) > 0);
    std::vector<EventRecord> channelEvents = eventIndex.listEvents(0);
    CHECK(channelEvents.size() == 2);
    if (channelEvents.size() == 2) {
        CHECK(channelEvents[0].filePath == "a.mp4" && channelEvents[1].filePath == "b.mp4");
        CHECK(channelEvents[1].maxPersonCount == 2 && channelEvents[1].endTimeMs == 5000);
    }
}

// 有人的时间段（秒）：在这些时间里检测结果给 1 个 person
struct PersonWindow {
    double startSeconds;
    double endSeconds;
};

// 用真码流跑事件录像：检测每 3 帧一次（模拟检测比解码慢），时间段内给出 person。返回完成的事件数
static int runRecorderWithFakeDetections(const std::string &eventDirectory, const std::vector<PersonWindow> &windows,
                                         EventIndex &eventIndex) {
    FfmpegFrameSource frameSource;
    if (!frameSource.open(kTestStreamPath, 30)) {
        return -1;
    }
    EventRecorderConfig recorderConfig;
    recorderConfig.eventDirectory = eventDirectory;
    EventRecorder eventRecorder;
    if (!eventRecorder.init(recorderConfig, frameSource.getStreamInfo(), &eventIndex)) {
        return -1;
    }
    EncodedFrame frame;
    while (frameSource.receiveNextFrame(frame) == ReceiveResult::FrameReady) {
        eventRecorder.onEncodedFrame(frame);
        if (frame.frameIndex % 3 != 0) {
            continue;
        }
        DetectionResult detectionResult;
        detectionResult.ptsUs = frame.extendedRtpTimestamp * 100 / 9;
        double frameSeconds = detectionResult.ptsUs / 1e6;
        for (const PersonWindow &window : windows) {
            if (frameSeconds >= window.startSeconds && frameSeconds <= window.endSeconds) {
                DetectedObject person;
                person.classIndex = kPersonClassIndex;
                person.confidence = 0.9f;
                detectionResult.objects.push_back(person);
                break;
            }
        }
        eventRecorder.onDetectionResult(detectionResult);
    }
    eventRecorder.closeOpenEvent();
    return eventRecorder.getFinishedEventCount();
}

static void testEventRecorderEndToEnd(const std::string &testDirectory) {
    printf("事件录像端到端（%s）\n", kTestStreamPath);
    if (!std::filesystem::exists(kTestStreamPath)) {
        printf("  [SKIP] 没有测试码流\n");
        return;
    }
    // 场景 1：20～25 秒有人 → 1 个事件，大约 [20 - 10 - 不到 1 个 GOP, 25 + 10] 秒
    {
        EventIndex eventIndex;
        CHECK(eventIndex.open(testDirectory + "/scene1.db"));
        int eventCount = runRecorderWithFakeDetections(testDirectory + "/scene1", {{20.0, 25.0}}, eventIndex);
        CHECK(eventCount == 1);
        std::vector<EventRecord> events = eventIndex.listEvents(0);
        CHECK(events.size() == 1);
        if (events.size() == 1) {
            int64_t fileDurationMs = probeMp4DurationMs(events[0].filePath);
            int64_t recordDurationMs = events[0].endTimeMs - events[0].startTimeMs;
            printf("  场景 1：文件 %s 时长 %lld ms，记录时长 %lld ms\n", events[0].filePath.c_str(),
                   static_cast<long long>(fileDurationMs), static_cast<long long>(recordDurationMs));
            CHECK(fileDurationMs >= 24'000 && fileDurationMs <= 27'500);
            CHECK(std::abs(recordDurationMs - fileDurationMs) <= 100);
            CHECK(events[0].maxPersonCount == 1);
        }
    }
    // 场景 2：20～25、30～32（间隔 5 秒 < 收尾 10 秒，算同一个事件）、50～52（新事件，录到文件结束被 closeOpenEvent 收尾）
    {
        EventIndex eventIndex;
        CHECK(eventIndex.open(testDirectory + "/scene2.db"));
        int eventCount = runRecorderWithFakeDetections(testDirectory + "/scene2",
                                                       {{20.0, 25.0}, {30.0, 32.0}, {50.0, 52.0}}, eventIndex);
        CHECK(eventCount == 2);
        std::vector<EventRecord> events = eventIndex.listEvents(0);
        CHECK(events.size() == 2);
        if (events.size() == 2) {
            int64_t firstDurationMs = probeMp4DurationMs(events[0].filePath);
            int64_t secondDurationMs = probeMp4DurationMs(events[1].filePath);
            printf("  场景 2：第 1 个 %lld ms，第 2 个 %lld ms\n", static_cast<long long>(firstDurationMs),
                   static_cast<long long>(secondDurationMs));
            CHECK(firstDurationMs >= 31'000 && firstDurationMs <= 34'500);   // 约 9～10 秒 → 42 秒
            CHECK(secondDurationMs >= 19'000 && secondDurationMs <= 22'000);  // 约 38.5～40 秒 → 60 秒（文件结束）
            CHECK(events[0].filePath != events[1].filePath);
        }
    }
}

void runEventMacTests() {
    std::string testDirectory = (std::filesystem::temp_directory_path() / "n10_m2_mac_tests").string();
    std::filesystem::remove_all(testDirectory);
    std::filesystem::create_directories(testDirectory);
    testPreRecordBuffer();
    testEventStateMachine();
    testLatestSlot();
    testNv12BoxDrawer();
    testEventIndex(testDirectory);
    testEventRecorderEndToEnd(testDirectory);
}
