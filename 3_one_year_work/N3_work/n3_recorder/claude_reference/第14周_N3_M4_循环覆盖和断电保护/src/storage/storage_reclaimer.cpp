#include "storage/storage_reclaimer.h"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <sys/statvfs.h>
#include <system_error>
#include <utility>

#include "common/log.h"

static const char *const kTag = "Reclaimer";

StorageReclaimer::StorageReclaimer(RecordIndex &requestedRecordIndex, StorageQuota requestedQuota,
                                   std::string requestedRecordRootPath) :
    recordIndex(requestedRecordIndex), quota(requestedQuota), recordRootPath(std::move(requestedRecordRootPath)) {}

int64_t StorageReclaimer::queryFreeBytes() const {
    struct statvfs fileSystemInfo{};
    if (statvfs(recordRootPath.c_str(), &fileSystemInfo) != 0) {
        LOGE(kTag, "statvfs %s 失败：%s", recordRootPath.c_str(), std::strerror(errno));
        return -1;
    }
    // f_bavail：普通用户能用的块数（不含 root 保留块）；f_frsize：块大小
    return static_cast<int64_t>(fileSystemInfo.f_bavail) * static_cast<int64_t>(fileSystemInfo.f_frsize);
}

bool StorageReclaimer::isEnough(int64_t upcomingSegmentBytes, int64_t totalRecordingBytes) const {
    const int64_t freeBytes = queryFreeBytes();
    if (freeBytes >= 0 && freeBytes - upcomingSegmentBytes < quota.minFreeBytes) {
        return false;
    }
    return quota.maxTotalRecordingBytes <= 0 || totalRecordingBytes + upcomingSegmentBytes <= quota.maxTotalRecordingBytes;
}

int StorageReclaimer::reclaimUntilEnough(int64_t upcomingSegmentBytes) {
    int deletedSegmentCount = 0;
    while (true) {
        const int64_t totalRecordingBytes = recordIndex.sumRecordingSizeBytes();
        if (totalRecordingBytes < 0) {
            return -1;
        }
        if (isEnough(upcomingSegmentBytes, totalRecordingBytes)) {
            return deletedSegmentCount;
        }
        RecordingEntry oldestEntry;
        if (!recordIndex.findOldestRecording(oldestEntry)) {
            LOGW(kTag, "录像都删光了空间还是不够（剩余 %lld 字节）", static_cast<long long>(queryFreeBytes()));
            return deletedSegmentCount;
        }
        // 先删文件再删记录：反过来的话，删完记录、删文件前断电，这个文件就再也没人管了（占着空间）
        std::error_code errorCode;
        std::filesystem::remove(oldestEntry.filePath, errorCode); // 文件已经不在也算删掉了
        if (errorCode) {
            LOGE(kTag, "删 %s 失败：%s", oldestEntry.filePath.c_str(), errorCode.message().c_str());
            return -1;
        }
        if (!recordIndex.deleteRecording(oldestEntry.recordingId)) {
            return -1;
        }
        removeEmptyDateDirectory(oldestEntry.filePath);
        deletedBytes += oldestEntry.sizeBytes;
        deletedSegmentCount++;
        LOGI(kTag, "删掉最早的段 %s（%lld 字节）", oldestEntry.filePath.c_str(), static_cast<long long>(oldestEntry.sizeBytes));
    }
}

void StorageReclaimer::removeEmptyDateDirectory(const std::string &filePath) const {
    // 日期目录删空了就把目录也删掉；remove 只删空目录，不空会失败，失败就不管
    std::error_code errorCode;
    std::filesystem::remove(std::filesystem::path(filePath).parent_path(), errorCode);
}
