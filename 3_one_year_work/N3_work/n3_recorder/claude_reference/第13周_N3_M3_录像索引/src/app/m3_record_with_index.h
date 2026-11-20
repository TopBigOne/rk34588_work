#pragma once

#include <cstdint>

// M3：M2 的分段录像 + SQLite 索引（<根>/record_index.db）。返回 0 = 成功
int runM3RecordWithIndex(const char *inputUrl, const char *recordRootPath, int totalDurationSeconds,
                         int segmentDurationSeconds);

// m3query：按通道 + 时间段（Unix 毫秒，[开始, 结束)）查有重叠的段并打印。返回 0 = 成功
int runM3Query(const char *recordRootPath, int channelIndex, int64_t rangeStartMs, int64_t rangeEndMs);
