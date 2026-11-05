//
// Created by DEV on 2026/11/3.
//

#include "mpp_encoder.h"

#include "../common/mpp_utils.h" // MPP_CHECK、ALIGN、RAII 句柄；它已经 include 了 log.h

// 日志标签：这个文件里所有 LOGx 的第一个参数都用它，输出里显示成 "MppEncoder: ..."
constexpr const char *kTag = "MppEncoder";

#define CHECK(expr) MPP_CHECK(expr, false)

#define CFG_SET(key, val) CHECK(mpp_enc_cfg_set_s32(encoderCfg, key, val))


bool MppEncoder::init(const EncoderConfigArg &encodeArg) {
    if (encoderCtx) {
        LOGE(kTag, "[FAIL] MppEncoder::init 只能调用一次");
        return false;
    }
    this->encoderArg = encodeArg;
    const int bpsMin =
            (this->encoderArg.rcMode == MPP_ENC_RC_MODE_CBR) ? encoderArg.bps * 15 / 16 : encoderArg.bps / 16;

    MppPollType outputTimeout = MPP_POLL_BLOCK;
    CHECK(mpp_create(&encoderCtx, &encoderApi));
    CHECK(encoderApi->control(encoderCtx, MPP_SET_OUTPUT_TIMEOUT, &outputTimeout));
    CHECK(mpp_init(encoderCtx, MPP_CTX_ENC, encodeArg.type));

    // 创建一个编码器配置对象
    CHECK(mpp_enc_cfg_init(&encoderCfg));
    // 先拿默认配置，再改
    CHECK(encoderApi->control(encoderCtx, MPP_ENC_GET_CFG, encoderCfg));
    // stride：调用方给了就用调用方的；没给（0）就按 16 对齐算：1920 → 1920，1080 → 1088
    horStride = encodeArg.horStride > 0 ? encodeArg.horStride : ALIGN(encodeArg.width, 16);
    verStride = encodeArg.verStride > 0 ? encodeArg.verStride : ALIGN(encodeArg.height, 16);
    // 开始改参数
    CFG_SET("prep:width", encodeArg.width);
    CFG_SET("prep:height", encodeArg.height);
    CFG_SET("prep:hor_stride", horStride);
    CFG_SET("prep:ver_stride", verStride);
    CFG_SET("prep:format", MPP_FMT_YUV420SP);
    CFG_SET("prep:rotation", encodeArg.rotation);
    CFG_SET("rc:mode", encodeArg.rcMode);
    // 帧率：输入、输出都是 -fps 指定的值（flex = 0 表示固定帧率）
    // 帧率用分数表示：num / denom，30 / 1 = 30fps
    CFG_SET("rc:fps_in_flex", 0);
    CFG_SET("rc:fps_in_num", encodeArg.fps);
    CFG_SET("rc:fps_in_denom", 1);

    CFG_SET("rc:fps_out_flex", 0);
    CFG_SET("rc:fps_out_num", encodeArg.fps);
    CFG_SET("rc:fps_out_denom", 1);
    // 码率上下限：CBR 为目标的 ±1/16；VBR / AVBR 下限放宽到 1/16（bpsMin 在前面算好）
    CFG_SET("rc:bps_target", encodeArg.bps);
    CFG_SET("rc:bps_max", encodeArg.bps * 17 / 16);
    CFG_SET("rc:bps_min", bpsMin);

    // GOP：每隔多少帧插一个 I 帧。I 帧能独立解码，P 帧要参考前面的帧
    CFG_SET("rc:gop", encodeArg.gop);
    CFG_SET("codec:type", encodeArg.type);
    if (encodeArg.type == MPP_VIDEO_CodingAVC) {
        // 100: high profile
        // 77 : Main profile
        // 66 : Baseline profile
        CFG_SET("h264:profile", 100);
        if (encoderArg.fps > 30) {
            CFG_SET("h264:level", encodeArg.height > 720 ? 42 : 32);
        }

        // 开启cabac，profile是Main 以上才能使用，CABAC比cavlc压缩率更高
        CFG_SET("h264:cabac_en", 1);
        CFG_SET("h264:cabac_idc", 0);
    }

    // 3. 参数开始真正生效
    CHECK(encoderApi->control(encoderCtx, MPP_ENC_SET_CFG, encoderCfg));
    // 每个IDR帧 前面都自动带一份sps/pps ，播放器从中间开始播放也能解码
    MppEncHeaderMode headerMode = MPP_ENC_HEADER_MODE_EACH_IDR;
    CHECK(encoderApi->control(encoderCtx, MPP_ENC_SET_HEADER_MODE, &headerMode));
    return true;
}

bool MppEncoder::getHeader(std::vector<uint8_t> &header) {
    //  // ⚠️ mpp_packet_copy 不检查容器大小，容器要给够：SPS/PPS/VPS 一共只有几十到一两百字节，64KB 绰绰有余
    std::vector<uint8_t> container(64 * 1024);
    MppPacket rawPacket;
    CHECK(mpp_packet_init(&rawPacket, container.data(), container.size()));
    MppPacketPtr packetPtr(rawPacket);

    // ⚠️ 一定要清零：mpp_packet_init 会把 length 设成整个容器的大小,不清零编码器会以为容器已经装满了
    mpp_packet_set_length(packetPtr.get(), 0);
    CHECK(encoderApi->control(encoderCtx, MPP_ENC_GET_HDR_SYNC, packetPtr.get()));
    uint8_t *packetPos = static_cast<uint8_t *>(mpp_packet_get_pos(packetPtr.get()));
    // 把rawPacket的header中完整的数据给到了传入的参数header
    header.assign(packetPos, packetPos + mpp_packet_get_length(packetPtr.get()));
    return true;
}

/**
 * 将一帧数据frameBuffer，编码成一个EncodedPacket
 * @param frameBuffer
 * @param inputEos
 * @param encodedPacket
 * @return
 */
bool MppEncoder::encode(MppBuffer frameBuffer, bool inputEos, EncodedPacket &encodedPacket) {
    encodedPacket.data.clear();
    encodedPacket.isEos = false;
    encodedPacket.isKeyFrame = false;

    MppFrame rawFrame = nullptr;
    CHECK(mpp_frame_init(&rawFrame));
    MppFramePtr mppFramePtr(rawFrame);
    MppFrame inputFrame = mppFramePtr.get();
    // 这 5 个值必须和 init() 里的 prep:* 参数一致
    mpp_frame_set_width(inputFrame, encoderArg.width);
    mpp_frame_set_height(inputFrame, encoderArg.height);
    mpp_frame_set_hor_stride(inputFrame, horStride);
    mpp_frame_set_ver_stride(inputFrame, verStride);
    mpp_frame_set_fmt(inputFrame, MPP_FMT_YUV420SP);
    mpp_frame_set_eos(inputFrame, inputEos);
    // 最后一帧，送一个空帧，没有图像，只带eos标志
    mpp_frame_set_buffer(inputFrame, inputEos ? nullptr : frameBuffer);

    // 5.2
    // 把一帧原始图像（如 NV12）送进编码器（异步）
    CHECK(encoderApi->encode_put_frame(encoderCtx, inputFrame));
    mppFramePtr.reset();
    inputFrame = nullptr;

    // 从编码器里取packet
    MppPacket rawPacket = nullptr;
    CHECK(encoderApi->encode_get_packet(encoderCtx, &rawPacket));
    if (!rawPacket) {
        return true;
    }
    MppPacketPtr packetPtr(rawPacket);
    MppPacket outputPacket = packetPtr.get();
    encodedPacket.isEos = mpp_packet_get_eos(outputPacket);
    size_t packetLength = mpp_packet_get_length(outputPacket);
    // ⚠️ 是 KEY_OUTPUT_INTRA（整数，1 = I 帧）
    if (packetLength > 0) {
        RK_S32 isIntra = 0;
        if (mpp_packet_has_meta(outputPacket)) {
            mpp_meta_get_s32(mpp_packet_get_meta(outputPacket), KEY_OUTPUT_INTRA, &isIntra);
        }
        encodedPacket.isKeyFrame = isIntra;
        uint8_t *pos = static_cast<uint8_t *>(mpp_packet_get_pos(outputPacket));
        encodedPacket.data.assign(pos, pos + packetLength);
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
