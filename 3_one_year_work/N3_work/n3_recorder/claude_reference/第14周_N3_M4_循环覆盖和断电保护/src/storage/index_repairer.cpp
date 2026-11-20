#include "storage/index_repairer.h"

#include <filesystem>
#include <system_error>

#include "common/log.h"
#include "storage/mp4_probe.h"

static const char *const kTag = "IndexRepairer";

int repairUnfinishedRecordings(RecordIndex &recordIndex) {
    int repairedCount = 0;
    for (const RecordingEntry &entry : recordIndex.listAllRecordings()) {
        std::error_code errorCode;
        const bool isFileExisting = std::filesystem::exists(entry.filePath, errorCode);
        if (!isFileExisting) {
            if (!recordIndex.deleteRecording(entry.recordingId)) {
                return -1;
            }
            LOGW(kTag, "文件不在了，删记录 %lld：%s", static_cast<long long>(entry.recordingId), entry.filePath.c_str());
            repairedCount++;
            continue;
        }
        if (entry.endTimeMs != kUnfinishedEndTimeMs) {
            continue;
        }
        const int64_t durationMs = probeMp4DurationMs(entry.filePath);
        if (durationMs <= 0) {
            // 只有头、没有一个完整片段（刚开段就被杀），或者是普通 MP4（没有 moov 读不了）：没法播，删掉
            std::filesystem::remove(entry.filePath, errorCode);
            if (!recordIndex.deleteRecording(entry.recordingId)) {
                return -1;
            }
            LOGW(kTag, "读不出帧，删文件和记录 %lld：%s", static_cast<long long>(entry.recordingId), entry.filePath.c_str());
            repairedCount++;
            continue;
        }
        const int64_t fileSizeBytes = static_cast<int64_t>(std::filesystem::file_size(entry.filePath, errorCode));
        if (!recordIndex.finishRecording(entry.recordingId, entry.startTimeMs + durationMs, errorCode ? 0 : fileSizeBytes)) {
            return -1;
        }
        LOGI(kTag, "补结束时间 %lld：%s，时长 %lld ms", static_cast<long long>(entry.recordingId), entry.filePath.c_str(),
             static_cast<long long>(durationMs));
        repairedCount++;
    }
    return repairedCount;
}
