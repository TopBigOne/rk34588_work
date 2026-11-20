#include "app/m3_record_with_index.h"

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>

#include "app/app_config.h"
#include "app/m2_segmented_record.h"
#include "common/log.h"
#include "storage/record_index.h"

static const char *const kTag = "M3";

static std::string buildIndexFilePath(const char *recordRootPath) {
    return (std::filesystem::path(recordRootPath) / kRecordIndexFileName).string();
}

// Unix 毫秒 → "2027-02-03 14:05:00.123"（本地时间）
static std::string formatLocalTime(int64_t timeMs) {
    const time_t timeSeconds = static_cast<time_t>(timeMs / 1000);
    tm localTime{};
    localtime_r(&timeSeconds, &localTime);
    char text[32] = {0};
    strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &localTime);
    char textWithMs[40] = {0};
    std::snprintf(textWithMs, sizeof(textWithMs), "%s.%03d", text, static_cast<int>(timeMs % 1000));
    return textWithMs;
}

int runM3RecordWithIndex(const char *inputUrl, const char *recordRootPath, int totalDurationSeconds,
                         int segmentDurationSeconds) {
    std::error_code errorCode;
    std::filesystem::create_directories(recordRootPath, errorCode);
    RecordIndex recordIndex;
    if (!recordIndex.open(buildIndexFilePath(recordRootPath))) {
        return 1;
    }
    return runSegmentedRecordWithIndex(inputUrl, recordRootPath, totalDurationSeconds, segmentDurationSeconds,
                                       &recordIndex, kTag);
}

int runM3Query(const char *recordRootPath, int channelIndex, int64_t rangeStartMs, int64_t rangeEndMs) {
    const std::string indexFilePath = buildIndexFilePath(recordRootPath);
    if (!std::filesystem::exists(indexFilePath)) {
        LOGE(kTag, "%s 不存在，先跑 m3", indexFilePath.c_str());
        return 1;
    }
    RecordIndex recordIndex;
    if (!recordIndex.open(indexFilePath)) {
        return 1;
    }
    for (const std::string &planLine : recordIndex.explainOverlappingQueryPlan()) {
        LOGI(kTag, "查询计划：%s", planLine.c_str());
    }
    LOGI(kTag, "查 ch%d [%s, %s)", channelIndex, formatLocalTime(rangeStartMs).c_str(),
         formatLocalTime(rangeEndMs).c_str());
    const std::vector<RecordingEntry> entries = recordIndex.queryOverlapping(channelIndex, rangeStartMs, rangeEndMs);
    for (const RecordingEntry &entry : entries) {
        std::printf("%lld  %s  ~  %s  %lld 字节  %s\n", static_cast<long long>(entry.recordingId),
                    formatLocalTime(entry.startTimeMs).c_str(),
                    entry.endTimeMs == kUnfinishedEndTimeMs ? "（没结束）" : formatLocalTime(entry.endTimeMs).c_str(),
                    static_cast<long long>(entry.sizeBytes), entry.filePath.c_str());
    }
    LOGI(kTag, "共 %zu 段", entries.size());
    return 0;
}
