#pragma once

#include <cstdint>
#include <string>

// <根>/ch<通道>/YYYY-MM-DD/HH-MM-SS.mp4，按本地时间（TZ）。目录不存在就 mkdir -p；
// 同一秒已经有文件（比如断流后马上重开）就加 _1、_2……
std::string buildSegmentFilePath(const std::string &recordRootPath, int channelIndex, int64_t segmentStartTimeMs);

// 墙上时间：1970-01-01 起的毫秒数
int64_t getWallClockMs();
