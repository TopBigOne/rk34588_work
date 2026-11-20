#include "common/annexb_utils.h"

namespace {

constexpr int kH264IdrType = 5;
constexpr int kH264SpsType = 7;
constexpr int kH264PpsType = 8;
constexpr int kH265FirstIrapType = 16; // BLA_W_LP
constexpr int kH265LastIrapType = 21;  // CRA_NUT
constexpr int kH265VpsType = 32;
constexpr int kH265SpsType = 33;
constexpr int kH265PpsType = 34;

// 从 searchStartIndex 起找下一个 00 00 01，返回它第一个 0 的下标；找不到返回 sizeBytes
size_t findStartCode(const uint8_t *data, size_t sizeBytes, size_t searchStartIndex) {
    for (size_t byteIndex = searchStartIndex; byteIndex + 2 < sizeBytes; ++byteIndex) {
        if (data[byteIndex] == 0 && data[byteIndex + 1] == 0 && data[byteIndex + 2] == 1) {
            return byteIndex;
        }
    }
    return sizeBytes;
}

void appendWithStartCode(std::vector<uint8_t> &output, const std::vector<uint8_t> &nalUnit) {
    if (nalUnit.empty()) {
        return;
    }
    const uint8_t startCode[] = {0, 0, 0, 1};
    output.insert(output.end(), startCode, startCode + sizeof(startCode));
    output.insert(output.end(), nalUnit.begin(), nalUnit.end());
}

} // namespace

std::vector<NalUnitView> splitAnnexBNalUnits(const uint8_t *data, size_t sizeBytes) {
    std::vector<NalUnitView> nalUnits;
    size_t startCodeIndex = findStartCode(data, sizeBytes, 0);
    while (startCodeIndex < sizeBytes) {
        const size_t nalStartIndex = startCodeIndex + 3;
        const size_t nextStartCodeIndex = findStartCode(data, sizeBytes, nalStartIndex);
        size_t nalEndIndex = nextStartCodeIndex;
        // 4 字节起始码 00 00 00 01 的第一个 0，以及 NAL 末尾补的 0（trailing_zero），都不算进这个 NAL
        while (nalEndIndex > nalStartIndex && data[nalEndIndex - 1] == 0) {
            --nalEndIndex;
        }
        if (nalEndIndex > nalStartIndex) {
            nalUnits.push_back({data + nalStartIndex, nalEndIndex - nalStartIndex});
        }
        startCodeIndex = nextStartCodeIndex;
    }
    return nalUnits;
}

int getNalUnitType(VideoCodec codec, const NalUnitView &nalUnit) {
    if (nalUnit.sizeBytes == 0) {
        return -1;
    }
    if (codec == VideoCodec::H264) {
        return nalUnit.data[0] & 0x1F;
    }
    return (nalUnit.data[0] >> 1) & 0x3F;
}

bool containsKeyFrameNal(VideoCodec codec, const uint8_t *data, size_t sizeBytes) {
    for (const NalUnitView &nalUnit : splitAnnexBNalUnits(data, sizeBytes)) {
        const int nalType = getNalUnitType(codec, nalUnit);
        if (codec == VideoCodec::H264 && nalType == kH264IdrType) {
            return true;
        }
        if (codec == VideoCodec::H265 && nalType >= kH265FirstIrapType && nalType <= kH265LastIrapType) {
            return true;
        }
    }
    return false;
}

ParameterSets extractParameterSets(VideoCodec codec, const uint8_t *data, size_t sizeBytes) {
    ParameterSets parameterSets;
    for (const NalUnitView &nalUnit : splitAnnexBNalUnits(data, sizeBytes)) {
        const int nalType = getNalUnitType(codec, nalUnit);
        std::vector<uint8_t> *target = nullptr;
        if (codec == VideoCodec::H264) {
            target = nalType == kH264SpsType ? &parameterSets.sps : nalType == kH264PpsType ? &parameterSets.pps : nullptr;
        } else {
            target = nalType == kH265VpsType   ? &parameterSets.vps
                     : nalType == kH265SpsType ? &parameterSets.sps
                     : nalType == kH265PpsType ? &parameterSets.pps
                                               : nullptr;
        }
        if (target != nullptr && target->empty()) {
            target->assign(nalUnit.data, nalUnit.data + nalUnit.sizeBytes);
        }
    }
    return parameterSets;
}

std::vector<uint8_t> buildAnnexBExtradata(const ParameterSets &parameterSets) {
    std::vector<uint8_t> extradata;
    appendWithStartCode(extradata, parameterSets.vps);
    appendWithStartCode(extradata, parameterSets.sps);
    appendWithStartCode(extradata, parameterSets.pps);
    return extradata;
}
