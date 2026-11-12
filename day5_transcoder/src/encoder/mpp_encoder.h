//
// Created by DEV on 2026/11/12.
//
#pragma once

#include <rockchip/rk_mpi.h>
#include <vector>

struct EncoderConfig {
    int width = 1920;
    int height = 1080;
    int horStride = 0;
    int verStride = 0;
    MppCodingType codingType = MPP_VIDEO_CodingAVC;
    // 码率控制模式
    MppEncRcMode rcMode = MPP_ENC_RC_MODE_CBR;
    int bps = 4 * 1000 * 1000;
    int fps = 30;
    int gop = 60;
};

struct EncodedPacket {
    std::vector<uint8_t> data;
    bool isKeyFrame = false;
    bool isEos = false;
};

class MppEncoder {

public:
    MppEncoder()=default;
    MppEncoder(const MppEncoder &) = delete;
    MppEncoder &operator=(const MppEncoder &) = delete;

    ~MppEncoder();

    bool init(const EncoderConfig &requestedConfig);
    bool getHeader(std::vector<uint8_t> &header);
    bool encode(MppBuffer frameBuffer, EncodedPacket &packet, bool isInputEos);

private:
    MppCtx encoderCtx = nullptr;
    MppApi *encoderApi = nullptr;
    MppEncCfg encoderCfg = nullptr;
    EncoderConfig config;
    int horStride = 0;
    int verStride = 0;
};
