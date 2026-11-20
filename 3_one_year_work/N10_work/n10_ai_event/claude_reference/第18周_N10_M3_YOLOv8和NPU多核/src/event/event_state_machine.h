#pragma once

#include <cstdint>

// Idle（没事件）──连续 N 次检测到人──► Recording（录制中）──一次没检测到人──► PostRoll（收尾中）
//   PostRoll ──又检测到人──► Recording（同一个事件继续，不开新文件：避免人进进出出产生一堆碎文件）
//   PostRoll ──最后一次看到人之后过了 postRollDuration──► shouldCloseEvent() = true，录像方关文件后调 markEventClosed() 回 Idle
// 时间全用码流的 pts（微秒），不用墙钟：检测慢了、文件输入快放时也对得上。
enum class EventState { Idle, Recording, PostRoll };

const char *getEventStateName(EventState state);

struct EventStateConfig {
    int startConfirmDetectionCount = 3;          // 去抖：连续 3 次检测到人才算开始（单帧误检不触发）
    int64_t postRollDurationUs = 10'000'000;     // 人消失后继续录 10 秒
    int64_t maxEventDurationUs = 600'000'000;    // 一个事件最长 10 分钟，到了强制关（人一直在也切开，文件别太大）
};

class EventStateMachine {
public:
    explicit EventStateMachine(const EventStateConfig &requestedConfig) : config(requestedConfig) {}

    EventState onDetectionResult(int personCount, int64_t ptsUs);  // 返回新状态
    bool shouldCloseEvent(int64_t currentPtsUs) const;
    void markEventClosed();  // 回到 Idle，计数清零

    EventState getState() const { return state; }
    bool isEventActive() const { return state != EventState::Idle; }
    int64_t getEventStartPtsUs() const { return eventStartPtsUs; }     // 连续检测的第一次（不是确认的那一次）
    int64_t getLastPersonPtsUs() const { return lastPersonPtsUs; }
    int getMaxPersonCount() const { return maxPersonCount; }

private:
    EventStateConfig config;
    EventState state = EventState::Idle;
    int consecutiveDetectionCount = 0;
    int64_t streakStartPtsUs = 0;
    int64_t eventStartPtsUs = 0;
    int64_t lastPersonPtsUs = 0;
    int maxPersonCount = 0;
};
