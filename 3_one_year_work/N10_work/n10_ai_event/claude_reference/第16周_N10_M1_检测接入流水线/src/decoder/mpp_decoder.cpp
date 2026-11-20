// 来源：3_one_year_work/N0_work/n0_nvr_mosaic/claude_reference/第02周_N0_Day6的M4和Day8上板/src/decoder/mpp_decoder.cpp
#include "decoder/mpp_decoder.h"

#include <utility> // std::move

#include "common/log.h"

static const char *const kTag = "MppDecoder";

/**
 * @brief 创建解码器，打开内部分帧
 * @param[in] codingType        码流格式
 * @param[in] requestedLogName  日志名字
 * @return true = 成功；false = 失败
 * @note 第 1 步 创建；第 2 步 按码流格式初始化；第 3 步 打开内部分帧
 */
bool MppDecoder::init(MppCodingType codingType, const char *requestedLogName) {
    if (decoderCtx) {
        LOGE(kTag, "init 只能调用一次");
        return false;
    }
    logName = requestedLogName;

    MPP_CHECK(mpp_create(&decoderCtx, &decoderApi), false);
    MPP_CHECK(mpp_init(decoderCtx, MPP_CTX_DEC, codingType), false);

    MPP_CHECK(mpp_dec_cfg_init(&decoderCfg), false);
    MPP_CHECK(decoderApi->control(decoderCtx, MPP_DEC_GET_CFG, decoderCfg), false);
    MPP_CHECK(mpp_dec_cfg_set_u32(decoderCfg, "base:split_parse", 1), false);
    MPP_CHECK(decoderApi->control(decoderCtx, MPP_DEC_SET_CFG, decoderCfg), false);
    return true;
}

/**
 * @brief 送一块码流
 * @param[in] data       码流数据
 * @param[in] sizeBytes  字节数
 * @param[in] isEos      是不是最后一块
 * @return Sent / Full / Error
 * @note 第 1 步 把数据包成 MppPacket；第 2 步 送进解码器
 */
FeedResult MppDecoder::feed(const uint8_t *data, size_t sizeBytes, bool isEos) {
    MppPacket rawPacket = nullptr;
    MPP_CHECK(mpp_packet_init(&rawPacket, const_cast<uint8_t *>(data), sizeBytes), FeedResult::Error);
    MppPacketPtr packetPtr(rawPacket); // 函数返回时自动 mpp_packet_deinit
    if (isEos) {
        mpp_packet_set_eos(packetPtr.get());
    }

    const MPP_RET putRet = decoderApi->decode_put_packet(decoderCtx, packetPtr.get());
    if (putRet == MPP_OK) {
        return FeedResult::Sent;
    }
    if (putRet == MPP_ERR_BUFFER_FULL) {
        return FeedResult::Full;
    }
    LOGE(kTag, "[%s] decode_put_packet ret=%d", logName.c_str(), static_cast<int>(putRet));
    return FeedResult::Error;
}

/**
 * @brief 取一个输出帧
 * @param[out] outputFramePtr  取到的帧
 * @return Frame / None / Error
 * @note 循环：取到的是 info change 帧就处理掉，接着取，直到取到真正的帧或者没有帧
 */
GetFrameResult MppDecoder::getFrame(MppFramePtr &outputFramePtr) {
    while (true) {
        MppFrame rawFrame = nullptr;
        MPP_CHECK(decoderApi->decode_get_frame(decoderCtx, &rawFrame), GetFrameResult::Error);
        if (!rawFrame) {
            return GetFrameResult::None; // 现在没有帧：还没解出来，或者还没送够码流
        }
        MppFramePtr framePtr(rawFrame);
        if (mpp_frame_get_info_change(rawFrame)) {
            if (!handleInfoChange(rawFrame)) {
                return GetFrameResult::Error;
            }
            continue; // framePtr 在这里析构，info change 帧被释放
        }
        outputFramePtr = std::move(framePtr); // 所有权交给调用方
        return GetFrameResult::Frame;
    }
}

/**
 * @brief 处理 info change
 * @param[in] infoChangeFrame  info change 帧
 * @return true = 成功；false = 失败
 * @note 第 1 步 读新参数；第 2 步 准备内存池；第 3 步 交给解码器并告诉它"准备好了"
 */
bool MppDecoder::handleInfoChange(MppFrame infoChangeFrame) {
    const RK_U32 width = mpp_frame_get_width(infoChangeFrame);
    const RK_U32 height = mpp_frame_get_height(infoChangeFrame);
    const RK_U32 horStride = mpp_frame_get_hor_stride(infoChangeFrame);
    const RK_U32 verStride = mpp_frame_get_ver_stride(infoChangeFrame);
    const size_t bufSizeBytes = mpp_frame_get_buf_size(infoChangeFrame); // 一帧要多大的 buffer
    infoChangeCount++;
    LOGI(kTag, "[%s] info change：%ux%u，stride %ux%u，buf_size %zu", logName.c_str(), width, height, horStride,
         verStride, bufSizeBytes);

    if (frameGroupPtr) {
        MPP_CHECK(mpp_buffer_group_clear(frameGroupPtr.get()), false);
    } else {
        MppBufferGroup rawBufferGroup = nullptr;
        MPP_CHECK(mpp_buffer_group_get_internal(&rawBufferGroup, MPP_BUFFER_TYPE_DRM), false);
        frameGroupPtr.reset(rawBufferGroup);
    }
    MPP_CHECK(mpp_buffer_group_limit_config(frameGroupPtr.get(), bufSizeBytes, kFrameBufferCount), false);

    MPP_CHECK(decoderApi->control(decoderCtx, MPP_DEC_SET_EXT_BUF_GROUP, frameGroupPtr.get()), false);
    MPP_CHECK(decoderApi->control(decoderCtx, MPP_DEC_SET_INFO_CHANGE_READY, nullptr), false);
    return true;
}

/**
 * @brief 销毁解码器
 * @return 无
 * @note 先释放配置，再销毁 MppCtx；内存池（frameGroupPtr）随后作为成员自动释放
 */
MppDecoder::~MppDecoder() {
    if (decoderCfg) {
        mpp_dec_cfg_deinit(decoderCfg);
    }
    if (decoderCtx) {
        mpp_destroy(decoderCtx);
    }
}
