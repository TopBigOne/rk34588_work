#pragma once

// M2：一路码流 → 按时长在 IDR 处切成多个 MP4（<根>/ch0/日期/时间.mp4）。返回 0 = 成功
int runM2SegmentedRecord(const char *inputUrl, const char *recordRootPath, int totalDurationSeconds,
                         int segmentDurationSeconds);

class RecordIndex;

// M2、M3 共用的录像流程：recordIndex 为 nullptr 就是 M2；tag 是日志标签
int runSegmentedRecordWithIndex(const char *inputUrl, const char *recordRootPath, int totalDurationSeconds,
                                int segmentDurationSeconds, RecordIndex *recordIndex, const char *tag);
