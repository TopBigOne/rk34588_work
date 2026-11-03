//
// Created by DEV on 2026/11/3.
//
#pragma once
#include <rockchip/rk_mpi.h>
#include <vector>

/**
 * 编码参数
 */
struct EncoderConfigArg {
    int width = 1920;
    int height = 1080;
    MppCodingType type = MPP_VIDEO_CodingAVC; // H.264
    MppEncRcMode rcMode = MPP_ENC_RC_MODE_CBR; // cbr 恒定码率
    int bps = 4 * 1000 * 1000;
    int fps = 30;
    int gop = 60;

    int horStride = 0;
    int verStride = 0;
    int rotation = 0;
};

struct EncodedPacket {
    std::vector<uint8_t> data;
    bool isKeyFrame = false;
    bool isEos = false;
};

class MppEncoder {

private:
    MppCtx encoderCtx = nullptr;
    MppApi *encoderApi = nullptr;
    MppEncCfg encoderCfg = nullptr;
    EncoderConfigArg encoderArg;

    RK_S32 horStride = 0;
    RK_S32 verStride = 0;

public:
    MppEncoder() = default;
    ~MppEncoder();
    /**
     * 禁止拷贝
     */
    MppEncoder(const MppEncoder &) = delete;

    MppEncoder &operator=(const MppEncoder &) = delete;

    /**
     *开启一台编码机
     * -> mpp_create()
     *    SET_OUTPUT_TIMEOUT
     * -> mpp_init()
     * -> mpp_enc_cfg_init()
     * -> GET_CFG x N
     * -> SET_CFG
     * -> SET_HEADER_MODE
     * @param encoderConfigArg
     * @return
     */
    bool init(const EncoderConfigArg &encoderConfigArg);

    RK_S32 getHorStride() const { return horStride; }

    RK_S32 getVerStride() const { return verStride; }


    /**
     * 获取sps、pps，（h265 vps）
     * @param header
     * @return
     */
    bool getHeader(std::vector<uint8_t> &header);

    bool encode(MppBuffer frameBuffer, bool inputEos, EncodedPacket &packet);
};
