#include "event/event_state_machine.h"

#include <algorithm>

const char *getEventStateName(EventState state) {
    switch (state) {
        case EventState::Idle:
            return "Idle";
        case EventState::Recording:
            return "Recording";
        case EventState::PostRoll:
            return "PostRoll";
    }
    return "?";
}

EventState EventStateMachine::onDetectionResult(int personCount, int64_t ptsUs) {
    const bool hasPerson = personCount > 0;
    switch (state) {
        case EventState::Idle:
            if (!hasPerson) {
                consecutiveDetectionCount = 0;
                break;
            }
            if (consecutiveDetectionCount == 0) {
                streakStartPtsUs = ptsUs;
            }
            consecutiveDetectionCount++;
            maxPersonCount = std::max(maxPersonCount, personCount);
            if (consecutiveDetectionCount >= config.startConfirmDetectionCount) {
                state = EventState::Recording;
                eventStartPtsUs = streakStartPtsUs;
                lastPersonPtsUs = ptsUs;
            }
            break;
        case EventState::Recording:
        case EventState::PostRoll:
            if (hasPerson) {
                state = EventState::Recording;
                lastPersonPtsUs = ptsUs;
                maxPersonCount = std::max(maxPersonCount, personCount);
            } else {
                state = EventState::PostRoll;
            }
            break;
    }
    return state;
}

bool EventStateMachine::shouldCloseEvent(int64_t currentPtsUs) const {
    if (state == EventState::Idle) {
        return false;
    }
    if (currentPtsUs - eventStartPtsUs >= config.maxEventDurationUs) {
        return true;
    }
    return state == EventState::PostRoll && currentPtsUs - lastPersonPtsUs >= config.postRollDurationUs;
}

void EventStateMachine::markEventClosed() {
    state = EventState::Idle;
    consecutiveDetectionCount = 0;
    maxPersonCount = 0;
}
