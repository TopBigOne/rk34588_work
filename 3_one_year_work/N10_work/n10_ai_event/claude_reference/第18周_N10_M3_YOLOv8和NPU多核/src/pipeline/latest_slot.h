#pragma once

#include <condition_variable>
#include <mutex>
#include <utility>

// 只放一样东西的“槽”：新的放进来，旧的还没被取走就直接扔掉（droppedCount + 1）。
// 用在实时输入：检测比解码慢时，排队只会让结果越来越滞后；只留最新一帧，检测结果始终对应最新画面。
// 模板：板子上放 MppFramePtr（LatestFrameSlot），Mac 单测放 std::unique_ptr<int>。T 要能移动、能判空。
template <typename T>
class LatestSlot {
public:
    void put(T item) {
        std::lock_guard<std::mutex> lock(mutex);
        if (isClosed) {
            return;
        }
        if (latestItem) {
            droppedCount++;  // 旧的那个在这里被新值覆盖、析构（MppFramePtr 就是把 buffer 还给解码器）
        }
        latestItem = std::move(item);
        putCount++;
        itemReady.notify_one();
    }

    // 阻塞到有东西或者被关；false = 已关闭而且槽是空的
    bool take(T &outputItem) {
        std::unique_lock<std::mutex> lock(mutex);
        itemReady.wait(lock, [this] { return latestItem || isClosed; });
        if (!latestItem) {
            return false;
        }
        outputItem = std::move(latestItem);
        latestItem = T();
        return true;
    }

    void close() {
        std::lock_guard<std::mutex> lock(mutex);
        isClosed = true;
        itemReady.notify_all();
    }

    int getDroppedCount() const {
        std::lock_guard<std::mutex> lock(mutex);
        return droppedCount;
    }

    int getPutCount() const {
        std::lock_guard<std::mutex> lock(mutex);
        return putCount;
    }

private:
    mutable std::mutex mutex;
    std::condition_variable itemReady;
    T latestItem{};
    bool isClosed = false;
    int droppedCount = 0;
    int putCount = 0;
};
