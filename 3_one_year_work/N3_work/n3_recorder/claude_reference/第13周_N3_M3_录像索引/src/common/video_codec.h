// 按 3_one_year_work/0_跨阶段接口.md 第 1 节
#pragma once

#include <cstdint>
#include <vector>

enum class VideoCodec { H264, H265 };

struct ParameterSets {
    std::vector<uint8_t> vps, sps, pps; // 都不带起始码；H.264 的 vps 为空
};
