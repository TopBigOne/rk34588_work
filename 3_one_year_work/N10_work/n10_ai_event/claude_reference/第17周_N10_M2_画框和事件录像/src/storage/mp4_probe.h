// 来源：3_one_year_work/N3_work/n3_recorder/claude_reference/第14周_N3_M4_循环覆盖和断电保护/src/storage/mp4_probe.h
#pragma once

#include <cstdint>
#include <string>

// 读完一个 MP4 / fMP4 的所有包，返回最后一帧的 (pts + duration)，单位毫秒。-1 = 打不开；0 = 一帧都没有。
// 不用 moov 里的时长：kill -9 留下的 fMP4 没有 mfra，片段才是真的
int64_t probeMp4DurationMs(const std::string &filePath);
