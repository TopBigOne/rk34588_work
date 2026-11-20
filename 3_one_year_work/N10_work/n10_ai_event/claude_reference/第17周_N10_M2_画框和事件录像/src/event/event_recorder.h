#pragma once

#include <cstdint>
#include <mutex>
#include <string>

#include "common/detection_types.h"
#include "common/encoded_frame.h"
#include "event/event_index.h"
#include "event/event_state_machine.h"
#include "event/pre_record_buffer.h"
#include "storage/mp4_file_writer.h"

struct EventRecorderConfig {
    int channelIndex = 0;
    std::string eventDirectory;               // 例 /userdata/n10/events，文件放 <目录>/ch0/20270315_143005.mp4
    int64_t preRecordDurationUs = 10'000'000;  // 事件开始前保留 10 秒
    EventStateConfig stateConfig;
    bool shouldFragmentMp4 = true;            // fMP4：录到一半断电，已经写下去的片段也能播（N3 M4 的做法）
};

// 两个线程调用（内部一把锁）：
//   接收线程 onEncodedFrame：每帧码流都进预录缓冲；事件开始后打开文件、先写缓冲再写实时帧；到时间关文件、写一条事件记录
//   检测线程 onDetectionResult：只更新状态机，不碰文件
// 录像存的是解码前的码流，不经过检测：检测慢了只会晚几帧发现人，录像本身不丢帧。
// 写文件在接收线程里做，写盘慢会拖住接收（计划风险 6），getMaxOnFrameDurationUs 记录最长一次用时。
class EventRecorder {
public:
    bool init(const EventRecorderConfig &requestedConfig, const StreamInfo &streamInfo, EventIndex *requestedIndex);
    void onEncodedFrame(const EncodedFrame &frame);
    void onDetectionResult(const DetectionResult &detectionResult);
    void closeOpenEvent();  // 程序退出时：正在录的事件收尾（文件写完、记录照写）

    int getFinishedEventCount() const;
    EventState getState() const;
    int64_t getMaxOnFrameDurationUs() const;
    const std::string &getLastEventFilePath() const { return lastEventFilePath; }  // 测试用

private:
    bool openEventFile(int64_t currentPtsIn90kHz);
    void finishEvent(const char *reason);
    std::string buildEventFilePath(int64_t startTimeMs) const;

    mutable std::mutex mutex;
    EventRecorderConfig config;
    VideoTrackParams trackParams;
    EventIndex *eventIndex = nullptr;  // 可以为空（不写数据库）
    EventStateMachine stateMachine{EventStateConfig{}};
    PreRecordBuffer preRecordBuffer{0};
    Mp4FileWriter fileWriter;
    int64_t fileFirstPtsIn90kHz = 0;     // 文件里第一帧的码流时间戳，写入时减掉它，文件内从 0 开始
    int64_t eventStartTimeMs = 0;        // 墙钟：文件第一帧对应的时刻
    int64_t lastFramePtsIn90kHz = 0;
    int finishedEventCount = 0;
    int64_t maxOnFrameDurationUs = 0;
    std::string lastEventFilePath;
};
