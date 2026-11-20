#include "app/m5_self_test.h"

#include <cstdint>  // int64_t
#include <unistd.h> // usleep

#include "common/log.h"
#include "common/mpp_utils.h"
#include "pipeline/frame_queue.h"
#include "sync/master_clock.h"

static const char *const kTag = "M5Test";

/**
 * @brief 检查一个条件，不成立就打印出来
 * @param[in]     isPassed     条件是否成立
 * @param[in]     description  这条检查在查什么
 * @param[in]     actualValue  实际值（打印用）
 * @param[in,out] failedCount  不成立时加 1
 * @return 无
 * @note 成立时也打印一行，方便看到跑了哪些检查
 */
static void checkCondition(bool isPassed, const char *description, int64_t actualValue, int &failedCount) {
    if (isPassed) {
        LOGI(kTag, "通过：%s（实际 %lld）", description, static_cast<long long>(actualValue));
        return;
    }
    LOGE(kTag, "失败：%s（实际 %lld）", description, static_cast<long long>(actualValue));
    failedCount++;
}

/**
 * @brief 造一个只带时间戳、不带画面的 MppFrame
 * @param[in] ptsUs  时间戳，单位微秒
 * @return 帧；失败时是空的
 * @note mpp_frame_init：分配一个空的帧描述（不分配画面 buffer）；mpp_frame_set_pts：写时间戳
 */
static MppFramePtr makeFrameWithPts(int64_t ptsUs) {
    MppFrame rawFrame = nullptr;
    if (mpp_frame_init(&rawFrame) != MPP_OK) {
        return MppFramePtr();
    }
    mpp_frame_set_pts(rawFrame, ptsUs);
    return MppFramePtr(rawFrame); // 交给 unique_ptr，析构时 mpp_frame_deinit
}

/**
 * @brief 检查 MasterClock
 * @param[in,out] failedCount  失败条数
 * @return 无
 * @note 睡 100 ms 后位置应在 [100000, 110000] 微秒；sleepUntilPositionUs(200000) 返回后位置 ≥ 200000
 */
static void checkMasterClock(int &failedCount) {
    MasterClock clock;
    checkCondition(clock.getPositionUs() == 0, "没 start 时位置是 0", clock.getPositionUs(), failedCount);
    clock.start();
    usleep(100 * 1000);
    const int64_t positionAfterSleepUs = clock.getPositionUs();
    checkCondition(positionAfterSleepUs >= 100000 && positionAfterSleepUs <= 110000, "睡 100 ms 后位置在 [100000, 110000]",
                   positionAfterSleepUs, failedCount);
    clock.sleepUntilPositionUs(200000);
    const int64_t positionAfterWaitUs = clock.getPositionUs();
    checkCondition(positionAfterWaitUs >= 200000, "sleepUntilPositionUs(200000) 返回后位置 ≥ 200000", positionAfterWaitUs,
                   failedCount);
}

/**
 * @brief 检查 FrameQueue::tryPopIfDue
 * @param[in,out] failedCount  失败条数
 * @return 无
 * @note 3 帧时间戳 0、33333、66666；按 nowUs 一步步取，看每次的结果
 */
static void checkTryPopIfDue(int &failedCount) {
    FrameQueue frameQueue(4);
    for (const int64_t ptsUs : {int64_t{0}, int64_t{33333}, int64_t{66666}}) {
        MppFramePtr framePtr = makeFrameWithPts(ptsUs);
        if (!framePtr) {
            checkCondition(false, "mpp_frame_init 成功", 0, failedCount);
            return;
        }
        frameQueue.push(std::move(framePtr));
    }

    MppFramePtr framePtr;
    TryPopResult result = frameQueue.tryPopIfDue(0, framePtr);
    checkCondition(result == TryPopResult::Frame, "nowUs=0：取到第 1 帧", static_cast<int>(result), failedCount);
    checkCondition(framePtr && mpp_frame_get_pts(framePtr.get()) == 0, "第 1 帧时间戳是 0",
                   framePtr ? mpp_frame_get_pts(framePtr.get()) : -1, failedCount);

    result = frameQueue.tryPopIfDue(0, framePtr);
    checkCondition(result == TryPopResult::NotDue, "nowUs=0 再取：NotDue（33333 还没到）", static_cast<int>(result),
                   failedCount);

    result = frameQueue.tryPopIfDue(70000, framePtr);
    checkCondition(result == TryPopResult::Frame, "nowUs=70000：取到第 2 帧", static_cast<int>(result), failedCount);
    result = frameQueue.tryPopIfDue(70000, framePtr);
    checkCondition(result == TryPopResult::Frame, "nowUs=70000：取到第 3 帧", static_cast<int>(result), failedCount);
    checkCondition(framePtr && mpp_frame_get_pts(framePtr.get()) == 66666, "第 3 帧时间戳是 66666",
                   framePtr ? mpp_frame_get_pts(framePtr.get()) : -1, failedCount);

    result = frameQueue.tryPopIfDue(70000, framePtr);
    checkCondition(result == TryPopResult::Empty, "取空以后：Empty", static_cast<int>(result), failedCount);

    frameQueue.closeInput();
    result = frameQueue.tryPopIfDue(70000, framePtr);
    checkCondition(result == TryPopResult::EndOfStream, "closeInput 以后：EndOfStream", static_cast<int>(result),
                   failedCount);

    FrameQueue abortedQueue(4);
    abortedQueue.abort();
    result = abortedQueue.tryPopIfDue(0, framePtr);
    checkCondition(result == TryPopResult::Aborted, "abort 以后：Aborted", static_cast<int>(result), failedCount);
}

/**
 * @brief M5 自测入口
 * @return 0 = 全部通过；1 = 有失败
 * @note 无
 */
int runM5SelfTest() {
    int failedCount = 0;
    checkMasterClock(failedCount);
    checkTryPopIfDue(failedCount);
    if (failedCount > 0) {
        LOGE(kTag, "M5 自测有 %d 条没通过", failedCount);
        return 1;
    }
    LOGI(kTag, "M5 自测全部通过");
    return 0;
}
