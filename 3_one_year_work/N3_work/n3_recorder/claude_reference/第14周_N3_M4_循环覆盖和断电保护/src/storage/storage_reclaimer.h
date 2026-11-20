#pragma once

#include <cstdint>
#include <string>

#include "storage/record_index.h"

struct StorageQuota {
    int64_t minFreeBytes = 0;           // 分区剩余低于它就删
    int64_t maxTotalRecordingBytes = 0; // 录像总大小（索引里 size_bytes 之和）高于它就删；0 = 不限
};

// 循环覆盖：空间不够时按开始时间从早到晚删段（文件 + 记录），所有通道一起排
class StorageReclaimer {
public:
    StorageReclaimer(RecordIndex &requestedRecordIndex, StorageQuota requestedQuota, std::string requestedRecordRootPath);

    int64_t queryFreeBytes() const; // statvfs：f_bavail × f_frsize；-1 = 出错
    // 给下一段腾出 upcomingSegmentBytes：返回删了几段；-1 = 出错。删光了还不够也返回已删的段数（打警告）
    int reclaimUntilEnough(int64_t upcomingSegmentBytes);
    int64_t getDeletedBytes() const { return deletedBytes; }

private:
    bool isEnough(int64_t upcomingSegmentBytes, int64_t totalRecordingBytes) const;
    void removeEmptyDateDirectory(const std::string &filePath) const;

    RecordIndex &recordIndex;
    StorageQuota quota;
    std::string recordRootPath;
    int64_t deletedBytes = 0;
};
