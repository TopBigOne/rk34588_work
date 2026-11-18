//
// Created by DEV on 2026/11/12.
//

#include "mpp_encoder.h"
#include "common/log.h"
#include "common/mpp_utils.h"
static const char *const kTag = "MppEncoder";

#define SET_CFG_S32(key, value) MPP_CHECK(mpp_enc_cfg_set_s32(encoderCfg, key, value), false)


bool MppEncoder::init(const EncoderConfig &requestedConfig) {
    if (encoderCtx) {
        LOGE(kTag, "init 只能调用一次");
        return false;
    }
    config = requestedConfig;
    // step 1: 创建实例
    MPP_CHECK(mpp_create(&encoderCtx, &encoderApi), false);
    MppPollType outputTimeout = MPP_POLL_BLOCK;
    MPP_CHECK(encoderApi->control(encoderCtx, MPP_SET_OUTPUT_TIMEOUT, &outputTimeout), false);
    MPP_CHECK(mpp_init(encoderCtx, MPP_CTX_ENC, config.codingType), false);

    // step 2: 取出默认参数
    MPP_CHECK(mpp_enc_cfg_init(&encoderCfg), false);
    MPP_CHECK(encoderApi->control(encoderCtx, MPP_ENC_GET_CFG, encoderCfg), false);

    // step 3: 改参数
    horStride = config.horStride > 0 ? config.horStride : ALIGN(config.width, 16);
    verStride = config.verStride > 0 ? config.verStride : ALIGN(config.height, 16);
    // prep: 输入画面什么样
    SET_CFG_S32("prep:width", config.width);
    SET_CFG_S32("prep:height", config.height);
    SET_CFG_S32("prep:hor_stride", horStride);
    SET_CFG_S32("prep:ver_stride", verStride);
    SET_CFG_S32("prep:format", MPP_FMT_YUV420SP);

    // rc: 码率控制
    SET_CFG_S32("rc:mode", config.rcMode);
    SET_CFG_S32("rc:fps_in_flex", 0);
    SET_CFG_S32("rc:fps_in_num", config.fps);
    SET_CFG_S32("rc:fps_in_denom", 1);
    SET_CFG_S32("rc:fps_out_flex", 0);
    SET_CFG_S32("rc:fps_out_num", config.fps);
    SET_CFG_S32("rc:fps_out_denom", 1);
    // 码率上下限：CBR 为目标的 ±1/16；其他模式下限放宽到 1/16
    const int bpsMin = config.rcMode == MPP_ENC_RC_MODE_CBR ? config.bps * 15 / 16 : config.bps / 16;
    SET_CFG_S32("rc:bps_target", config.bps);
    SET_CFG_S32("rc:bps_max", config.bps * 17 / 16);
    SET_CFG_S32("rc:bps_min", bpsMin);
    // GOP：每隔多少帧一个 I 帧。I 帧能独立解码，P 帧要参考前面的帧
    SET_CFG_S32("rc:gop", config.gop);

    SET_CFG_S32("codec:type", config.codingType);
    if (config.codingType == MPP_VIDEO_CodingAVC) {
        SET_CFG_S32("h264:profile", 100);
        SET_CFG_S32("h264:cabac_en", 1);
        SET_CFG_S32("h264:cabac_idc", 0);
    }

    // step 4:
    MPP_CHECK(encoderApi->control(encoderCtx, MPP_ENC_SET_CFG, encoderCfg), false);
    MppEncHeaderMode headerMode = MPP_ENC_HEADER_MODE_EACH_IDR;
    MPP_CHECK(encoderApi->control(encoderCtx, MPP_ENC_SET_HEADER_MODE, &headerMode), false);
    return true;
}

bool MppEncoder::getHeader(std::vector<uint8_t> &header) {
    std::vector<uint8_t> container(64 * 1024);
    MppPacket rawPacket = nullptr;
    MPP_CHECK(mpp_packet_init(&rawPacket, container.data(), container.size()), false);
    MppPacketPtr packetPtr(rawPacket);
    mpp_packet_set_length(packetPtr.get(), 0);
    // MPP_ENC_GET_HDR_SYNC：让编码器立刻生成 SPS、PPS（H.264 的序列参数集、图像参数集，含分辨率、profile 等），
    // 写进 packetPtr 指向的 packet，调用返回时已经写好（同步）。
    // 用途：写在 .h264 文件开头；M3 做 RTSP 时放进 SDP 的 sprop-parameter-sets
    MPP_CHECK(encoderApi->control(encoderCtx, MPP_ENC_GET_HDR_SYNC, packetPtr.get()), false);
    const uint8_t *headerStart = static_cast<const uint8_t *>(mpp_packet_get_pos(packetPtr.get()));
    header.assign(headerStart, headerStart + mpp_packet_get_length(packetPtr.get()));
    return true;
}

bool MppEncoder::encode(MppBuffer frameBuffer, EncodedPacket &encodedPacket, bool isInputEos) {
    encodedPacket.data.clear();
    encodedPacket.isEos = false;
    encodedPacket.isKeyFrame = false;

    // step 1:
    MppFrame rawFrame = nullptr;
    MPP_CHECK(mpp_frame_init(&rawFrame), false);
    MppFramePtr framePtr(rawFrame);
    mpp_frame_set_width(framePtr.get(), config.width);
    mpp_frame_set_height(framePtr.get(), config.height);
    mpp_frame_set_hor_stride(framePtr.get(), horStride);
    mpp_frame_set_ver_stride(framePtr.get(), verStride);
    mpp_frame_set_fmt(framePtr.get(), MPP_FMT_YUV420SP);
    mpp_frame_set_eos(framePtr.get(), isInputEos ? 1 : 0);
    mpp_frame_set_buffer(framePtr.get(), isInputEos ? nullptr : frameBuffer);

    // step 2:
    MPP_CHECK(encoderApi->encode_put_frame(encoderCtx, framePtr.get()), false);
    framePtr.reset();

    // step 3:
    MppPacket rawPacket = nullptr;
    MPP_CHECK(encoderApi->encode_get_packet(encoderCtx, &rawPacket), false);

    if (!rawPacket) {
        return true;
    }
    MppPacketPtr packetPtr(rawPacket);
    encodedPacket.isEos = mpp_packet_get_eos(packetPtr.get());
    const size_t packetLength = mpp_packet_get_length(packetPtr.get());
    if (packetLength > 0) {
        RK_S32 isIntra = 0;
        if (mpp_packet_has_meta(packetPtr.get())) {
            MppMeta mppMeta = mpp_packet_get_meta(packetPtr.get());
            mpp_meta_get_s32(mppMeta, KEY_OUTPUT_INTRA, &isIntra);
        }
        encodedPacket.isKeyFrame = isIntra != 0;
        const uint8_t *packetStart = static_cast<const uint8_t *>(mpp_packet_get_pos(packetPtr.get()));
        encodedPacket.data.assign(packetStart, packetStart + packetLength);
    }

    return true;
}


MppEncoder::~MppEncoder() {
    if (encoderCfg) {
        mpp_enc_cfg_deinit(encoderCfg);
    }
    if (encoderCtx) {
        mpp_destroy(encoderCtx);
    }
}
