#pragma once

// M4：分段录像 + 索引 + 循环覆盖 + fMP4。启动先修索引。shouldPaceRawFile：裸流文件按实时速度读。返回 0 = 成功
int runM4LoopRecord(const char *inputUrl, const char *recordRootPath, int totalDurationSeconds,
                    int segmentDurationSeconds, int quotaMegabytes, bool shouldPaceRawFile);

// m4repair：只跑启动修复。返回 0 = 成功
int runM4Repair(const char *recordRootPath);
