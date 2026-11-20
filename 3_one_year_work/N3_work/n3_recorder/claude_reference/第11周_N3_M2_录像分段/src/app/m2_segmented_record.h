#pragma once

// M2：一路码流 → 按时长在 IDR 处切成多个 MP4（<根>/ch0/日期/时间.mp4）。返回 0 = 成功
int runM2SegmentedRecord(const char *inputUrl, const char *recordRootPath, int totalDurationSeconds,
                         int segmentDurationSeconds);
