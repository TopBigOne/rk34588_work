//
// Created by DEV on 2026/11/10.
//

#include "mpp_decoder.h"
#include <utility> // std::move
#include "common/log.h"

static const char *const kTag = "MppDecoder";

bool MppDecoder::init(MppCodingType codingType) {
    if (decoderCtx) {
        LOGE(kTag, "init 只能调用一次");
        return false;
    }

    // step 1:
    MPP_CHECK(mpp_create(&decoderCtx, &decoderApi), false);
    // step 2:
    MPP_CHECK(mpp_init(decoderCtx, MPP_CTX_DEC, codingType), false);
    // step 3:
    MPP_CHECK(mpp_dec_cfg_init(&decoderCfg), false);
    // step 4:
    MPP_CHECK(decoderApi->control(decoderCtx, MPP_DEC_GET_CFG, decoderCfg), false);
    // step 5:
    MPP_CHECK(mpp_dec_cfg_set_u32(decoderCfg, "base:split_parse", 1), false);
    // step 6:
    MPP_CHECK(decoderApi->control(decoderCtx, MPP_DEC_SET_CFG, decoderCfg), false);
    return true;
}

FeedResult MppDecoder::feed(const uint8_t *data, size_t sizeBytes, bool isEos) {
    MppPacket rawPacket = nullptr;
    MPP_CHECK(mpp_packet_init(&rawPacket, const_cast<uint8_t *>(data), sizeBytes), FeedResult::Error);
    MppPacketPtr packet(rawPacket);
    if (isEos) {
        mpp_packet_set_eos(packet.get());
    }

    auto putRet = decoderApi->decode_put_packet(decoderCtx, packet.get());
    if (putRet == MPP_OK) {
        return FeedResult::Sent;
    }
    if (putRet == MPP_ERR_BUFFER_FULL) {
        return FeedResult::Full;
    }

    LOGE(kTag, "decode_put_packet ret=%d", static_cast<int>(putRet));
    return FeedResult::Error;
}

GetFrameResult MppDecoder::getFrame(MppFramePtr &outputFrame) {
    // LOGD(kTag, "getFrame()");
    while (true) {
        MppFrame rawFrame = nullptr;
        MPP_CHECK(decoderApi->decode_get_frame(decoderCtx, &rawFrame), GetFrameResult::Error);
        if (!rawFrame) {
            //LOGE(kTag, "    decode nothing");
            return GetFrameResult::None;
        }
        MppFramePtr frame(rawFrame);
        if (mpp_frame_get_info_change(rawFrame)) {
            if (!handleInfoChange(rawFrame)) {
                return GetFrameResult::Error;
            }
            LOGE(kTag, "    frame info change");
            continue;
        }

        outputFrame = std::move(frame);
        //LOGI(kTag, "    decode one frame in success ✅✅✅✅");
        //LOGI(kTag, "    ");
        return GetFrameResult::Frame;
    }
}

bool MppDecoder::handleInfoChange(MppFrame infoChangeFrame) {
    const RK_U32 width = mpp_frame_get_width(infoChangeFrame);
    const RK_U32 height = mpp_frame_get_height(infoChangeFrame);
    const RK_U32 horStride = mpp_frame_get_hor_stride(infoChangeFrame);
    const RK_U32 verStride = mpp_frame_get_ver_stride(infoChangeFrame);
    const size_t bufSizeBytes = mpp_frame_get_buf_size(infoChangeFrame);
    infoChangeCount++;
    LOGI(kTag, "        info change：%ux%u，stride %ux%u，buf_size %zu", width, height, horStride, verStride,
         bufSizeBytes);

    if (frameGroupPtr) {
        // 码流中途切换分辨率,旧尺寸的buffer需要清理掉
        MPP_CHECK(mpp_buffer_group_clear(frameGroupPtr.get()), false);
    } else {
        MppBufferGroup rawBufferGroup = nullptr;
        MPP_CHECK(mpp_buffer_group_get_internal(&rawBufferGroup, MPP_BUFFER_TYPE_DRM), false);
        frameGroupPtr.reset(rawBufferGroup);
    }

    // group 设置单块 buffer 的大小上限和 buffer 数量上限（切换到 limit 模式）
    //  // limit_config(group, 每块多大, 最多几块)：池子里最多 kFrameBufferCount 块，每块 bufSizeBytes 字节
    MPP_CHECK(mpp_buffer_group_limit_config(frameGroupPtr.get(), bufSizeBytes, kFrameBufferCount), false);
    // // SET_EXT_BUF_GROUP：告诉解码器"输出的画面放到这个池子里"
    MPP_CHECK(decoderApi->control(decoderCtx, MPP_DEC_SET_EXT_BUF_GROUP, frameGroupPtr.get()), false);
    MPP_CHECK(decoderApi->control(decoderCtx, MPP_DEC_SET_INFO_CHANGE_READY, nullptr), false);
    return true;
}


MppDecoder::~MppDecoder() {
    if (decoderCfg) {
        mpp_dec_cfg_deinit(decoderCfg);
    }
    if (decoderCtx) {
        mpp_destroy(decoderCtx);
    }
}
