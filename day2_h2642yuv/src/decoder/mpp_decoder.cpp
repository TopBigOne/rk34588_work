#include "decoder/mpp_decoder.h"

#include <cstdio>  // printf
#include <utility> // std::move

/**
 * @brief 析构：释放解码参数和解码器实例；内存池由成员 frameGroup_ 最后释放
 *
 * 顺序：mpp_dec_cfg_deinit → mpp_destroy →（函数体执行完）frameGroup_ 析构 → mpp_buffer_group_put。
 * init 中途失败也能正确释放：没申请过的句柄还是 nullptr，直接跳过。
 *
 * @note 【精读】析构函数体先执行，成员后析构，所以内存池一定在解码器销毁之后才释放。
 *       检验：C++ 里析构函数体和成员析构，谁先执行？如果把 ctx 也做成成员、声明在 frameGroup_ 后面，会怎样？
 */
MppDecoder::~MppDecoder() {
    if (decoderCfg_) {
        mpp_dec_cfg_deinit(decoderCfg_); // 直接传句柄，不要 &（deinit 不需要改你的变量）
    }
    if (decoderCtx_) {
        mpp_destroy(decoderCtx_);
    }
    // frameGroup_ 在函数体执行完后自动 mpp_buffer_group_put
}

/**
 * @brief 开机：创建解码器实例，初始化成解码器，打开内部分帧
 *
 * mpp_create → mpp_init(MPP_CTX_DEC, type) → mpp_dec_cfg_init → GET_CFG → base:split_parse = 1 → SET_CFG
 *
 * @param type  编码格式：MPP_VIDEO_CodingAVC（H.264）/ MPP_VIDEO_CodingHEVC（H.265）
 * @return 成功返回 true；任何一步失败打印出错的那一句，返回 false（已申请的资源由析构函数释放）
 *
 * @note 【手抄】MPP 核心流程。默写时注意哪个参数传 &（create、cfg_init 要把新对象写回），哪个不传（control、set_u32）
 */
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

/**
 * @brief 送一块码流：mpp_packet_init → decode_put_packet → mpp_packet_deinit
 *
 * 每次都新建一个 packet、送完就释放：packet 不带 MppBuffer 时，MPP 收下会把数据拷走。
 *
 * @param data  码流数据（MPP 只读，不会改）
 * @param size  字节数
 * @param eos   true = 这是最后一块，给 packet 打上 EOS
 * @return FeedResult::Sent  送进去了；
 *         FeedResult::Full  输入队列满，MPP 没收下，调用方保留 data、等 1ms 原样再送；
 *         FeedResult::Error 出错（已打印原因）
 *
 * @note 【精读】为什么每次都新建 packet，而不是像面条版那样一直保留同一个。
 *       检验：返回 Full 时 MPP 收下数据了吗？依据是 mpp.cpp 的哪几行？（提示：第 420 行判断队列满，第 433–440 行才拷贝）
 */
FeedResult MppDecoder::feed(const uint8_t* data, size_t size, bool eos) {
    // 【精读】const_cast：mpp_packet_init 的参数是 void*（不带 const），MPP 只读这块数据，所以去掉 const 是安全的
    //   检验：MPP 会写这块内存吗？如果会，这里会出什么问题？
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

/**
 * @brief 取一个输出帧（非阻塞）；遇到 info change 在这里处理掉，接着再取
 *
 * @param frame  输出参数：拿到的帧（正常图像 / 错误帧 / 只带 EOS 的空帧），离开作用域自动 mpp_frame_deinit
 * @return GetResult::Frame 拿到一帧；GetResult::None 现在没有输出；GetResult::Error 出错（已打印原因）
 *
 * @note 【精读】所有权：一拿到 rawFrame 就交给 MppFramePtr；info change 时 continue，帧自动释放；
 *       正常帧用 std::move 把所有权交给调用方。
 *       检验：如果去掉 framePtr、直接返回 rawFrame，哪种情况下会泄漏？
 */
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

/**
 * @brief 处理 info change：打印格式信息，配置内存池，通知解码器继续
 *
 * 第一次：mpp_buffer_group_get_internal 建池子；以后（码流中途换分辨率）：mpp_buffer_group_clear，池子复用
 * → mpp_buffer_group_limit_config(buf_size, 24) → SET_EXT_BUF_GROUP → SET_INFO_CHANGE_READY
 *
 * @param frame  info change 帧（只带宽高、stride、buf_size，没有图像）
 * @return 成功返回 true；失败打印出错的那一句，返回 false
 *
 * @note 【手抄】MPP 核心流程。默写时注意为什么第一次建池子、之后用 clear（见 doc/mpp_buffer_group_limit_config.md）
 */
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
