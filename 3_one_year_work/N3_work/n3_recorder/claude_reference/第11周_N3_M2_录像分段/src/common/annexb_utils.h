#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "common/video_codec.h"

// 一个 NAL 单元在缓冲区里的位置（不含起始码）
struct NalUnitView {
    const uint8_t *data = nullptr;
    size_t sizeBytes = 0;
};

// 按起始码（00 00 01 或 00 00 00 01）把 Annex-B 数据切成一个个 NAL
std::vector<NalUnitView> splitAnnexBNalUnits(const uint8_t *data, size_t sizeBytes);

// NAL 头里的类型：H.264 是第 1 字节低 5 位，H.265 是第 1 字节的第 1～6 位
int getNalUnitType(VideoCodec codec, const NalUnitView &nalUnit);

bool containsKeyFrameNal(VideoCodec codec, const uint8_t *data, size_t sizeBytes);

// 从 Annex-B 的 extradata 里挑出 VPS/SPS/PPS（各取第一个）
ParameterSets extractParameterSets(VideoCodec codec, const uint8_t *data, size_t sizeBytes);

// 反过来：把 VPS/SPS/PPS 各自前面补 00 00 00 01 拼起来
std::vector<uint8_t> buildAnnexBExtradata(const ParameterSets &parameterSets);
