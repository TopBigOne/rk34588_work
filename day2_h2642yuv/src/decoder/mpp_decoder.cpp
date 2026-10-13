#include "decoder/mpp_decoder.h"

#include <cstdio>  // printf
#include <utility> // std::move

MppDecoder::~MppDecoder() {
    if (decoderCfg_) {
        mpp_dec_cfg_deinit(decoderCfg_); // 直接传句柄，不要 &（deinit 不需要改你的变量）
    }
    if (decoderCtx_) {
        mpp_destroy(decoderCtx_);
    }
    // frameGroup_ 在函数体执行完后自动 mpp_buffer_group_put
}

bool MppDecoder::init(MppCodingType type) {
    MPP_CHECK(mpp_create(&decoderCtx_, &decoderApi_), false);
    MPP_CHECK(mpp_init(decoderCtx_, MPP_CTX_DEC, type), false);

    // 打开内部分帧：取出默认参数 → 改 split_parse → 写回
    MPP_CHECK(mpp_dec_cfg_init(&decoderCfg_), false);
    MPP_CHECK(decoderApi_->control(decoderCtx_, MPP_DEC_GET_CFG, decoderCfg_), false);
    MPP_CHECK(mpp_dec_cfg_set_u32(decoderCfg_, "base:split_parse", 1), false);
    MPP_CHECK(decoderApi_->control(decoderCtx_, MPP_DEC_SET_CFG, decoderCfg_), false);
    return true;
}

FeedResult MppDecoder::feed(const uint8_t* data, size_t size, bool eos) {
    // mpp_packet_init 的参数是 void*（不带 const），MPP 只读这块数据，所以 const_cast 是安全的
    MppPacket packet = nullptr;
    MPP_CHECK(mpp_packet_init(&packet, const_cast<uint8_t*>(data), size), FeedResult::Error);
    MppPacketPtr packetPtr(packet); // RAII：函数返回时自动 mpp_packet_deinit
    if (eos) {
        mpp_packet_set_eos(packet);
    }

    MPP_RET ret = decoderApi_->decode_put_packet(decoderCtx_, packet);
    if (ret == MPP_OK) {
        return FeedResult::Sent;
    }
    if (ret == MPP_ERR_BUFFER_FULL) {
        return FeedResult::Full; // 队列满：MPP 没有收下，调用方等 1ms 原样再送
    }
    printf("    [FAIL] decode_put_packet ret=%d\n", ret);
    return FeedResult::Error;
}

GetResult MppDecoder::get_frame(MppFramePtr& frame) {
    while (true) {
        MppFrame rawFrame = nullptr;
        MPP_CHECK(decoderApi_->decode_get_frame(decoderCtx_, &rawFrame), GetResult::Error);
        if (!rawFrame) {
            return GetResult::None; // 非阻塞：现在没有输出
        }
        MppFramePtr framePtr(rawFrame); // 先交给 RAII，后面不管从哪 return 都会 deinit

        if (mpp_frame_get_info_change(rawFrame)) {
            if (!handle_info_change(rawFrame)) {
                return GetResult::Error;
            }
            continue; // info change 帧没有图像，处理完接着取（framePtr 在这里析构，帧被释放）
        }

        frame = std::move(framePtr); // 所有权交给调用方
        return GetResult::Frame;
    }
}

bool MppDecoder::handle_info_change(MppFrame frame) {
    const RK_U32 width      = mpp_frame_get_width(frame);
    const RK_U32 height     = mpp_frame_get_height(frame);
    const RK_U32 horStride  = mpp_frame_get_hor_stride(frame);
    const RK_U32 verStride  = mpp_frame_get_ver_stride(frame);
    const size_t bufferSize = mpp_frame_get_buf_size(frame); // 每帧需要的缓冲区大小（字节），1080p 是 4,177,920
    infoChangeCount_++;
    printf("|            info change: %ux%u stride %ux%u buf_size %zu\n", width, height, horStride, verStride,
        bufferSize);

    if (!frameGroup_) {
        // 第一次：建一个"内部分配"的空池子（buffer 按需创建，不是一下子建 24 块）
        MppBufferGroup group = nullptr;
        MPP_CHECK(mpp_buffer_group_get_internal(&group, MPP_BUFFER_TYPE_DRM), false);
        frameGroup_.reset(group);
    } else {
        // 码流中途换分辨率：池子复用，旧尺寸的 buffer 清掉（空闲的马上销毁，正在用的用完后销毁）
        MPP_CHECK(mpp_buffer_group_clear(frameGroup_.get()), false);
    }
    MPP_CHECK(mpp_buffer_group_limit_config(frameGroup_.get(), bufferSize, kFrameBufferCount), false);
    MPP_CHECK(decoderApi_->control(decoderCtx_, MPP_DEC_SET_EXT_BUF_GROUP, frameGroup_.get()), false);
    MPP_CHECK(decoderApi_->control(decoderCtx_, MPP_DEC_SET_INFO_CHANGE_READY, nullptr), false);
    return true;
}
