#include "encoder/mpp_encoder.h"

#include "common/log.h"
#include "common/mpp_utils.h"

static const char *const kTag = "MppEncoder";

// 设置一项 int 参数，失败就 return false。只在 init 里用
#define SET_CFG_S32(key, value) MPP_CHECK(mpp_enc_cfg_set_s32(encoderCfg, key, value), false)

/**
 * @brief 创建编码器，设置参数
 * @param[in] requestedConfig  编码参数
 * @return true = 成功；false = 失败
 * @note 第 1 步 创建，设置输出阻塞；第 2 步 初始化成编码器；第 3 步 prep 输入画面；第 4 步 rc 码率控制；
 *       第 5 步 codec 编码格式；第 6 步 写回配置，设置码流头模式
 */
bool MppEncoder::init(const EncoderConfig &requestedConfig) {
    if (encoderCtx) {
        LOGE(kTag, "init 只能调用一次");
        return false;
    }
    config = requestedConfig;

    // 第 1 步 创建。MPP_SET_OUTPUT_TIMEOUT 要在 mpp_init 之前设：MPP_POLL_BLOCK = encode_get_packet 一直等到有包
    MPP_CHECK(mpp_create(&encoderCtx, &encoderApi), false);
    MppPollType outputTimeout = MPP_POLL_BLOCK;
    MPP_CHECK(encoderApi->control(encoderCtx, MPP_SET_OUTPUT_TIMEOUT, &outputTimeout), false);

    // 第 2 步 初始化成编码器
    MPP_CHECK(mpp_init(encoderCtx, MPP_CTX_ENC, config.codingType), false);
    MPP_CHECK(mpp_enc_cfg_init(&encoderCfg), false);
    MPP_CHECK(encoderApi->control(encoderCtx, MPP_ENC_GET_CFG, encoderCfg), false); // 先读出默认配置，再改

    // 第 3 步 prep：输入画面的宽高、stride、格式。stride 必须和输入 buffer 的实际排布一致
    horStride = config.horStride > 0 ? config.horStride : ALIGN(config.width, 16);
    verStride = config.verStride > 0 ? config.verStride : ALIGN(config.height, 16);
    SET_CFG_S32("prep:width", config.width);
    SET_CFG_S32("prep:height", config.height);
    SET_CFG_S32("prep:hor_stride", horStride);
    SET_CFG_S32("prep:ver_stride", verStride);
    SET_CFG_S32("prep:format", MPP_FMT_YUV420SP);

    // 第 4 步 rc：码率控制。输入、输出帧率一样（flex = 0 表示固定帧率），码率在目标值上下浮动
    SET_CFG_S32("rc:mode", config.rcMode);
    SET_CFG_S32("rc:fps_in_flex", 0);
    SET_CFG_S32("rc:fps_in_num", config.fps);
    SET_CFG_S32("rc:fps_in_denom", 1);
    SET_CFG_S32("rc:fps_out_flex", 0);
    SET_CFG_S32("rc:fps_out_num", config.fps);
    SET_CFG_S32("rc:fps_out_denom", 1);
    const int bpsMin = config.rcMode == MPP_ENC_RC_MODE_CBR ? config.bps * 15 / 16 : config.bps / 16;
    SET_CFG_S32("rc:bps_target", config.bps);
    SET_CFG_S32("rc:bps_max", config.bps * 17 / 16);
    SET_CFG_S32("rc:bps_min", bpsMin);
    SET_CFG_S32("rc:gop", config.gop);

    // 第 5 步 codec：编码格式。h264:* 只对 H.264 有效；H.265 的 profile、level 不设置就用默认值
    SET_CFG_S32("codec:type", config.codingType);
    if (config.codingType == MPP_VIDEO_CodingAVC) {
        SET_CFG_S32("h264:profile", 100); // 100 = High profile
        SET_CFG_S32("h264:cabac_en", 1);
        SET_CFG_S32("h264:cabac_idc", 0);
    }

    // 第 6 步 写回配置。EACH_IDR：每个 I 帧前面都带一份码流头，播放器从中间开始看也能解
    MPP_CHECK(encoderApi->control(encoderCtx, MPP_ENC_SET_CFG, encoderCfg), false);
    MppEncHeaderMode headerMode = MPP_ENC_HEADER_MODE_EACH_IDR;
    MPP_CHECK(encoderApi->control(encoderCtx, MPP_ENC_SET_HEADER_MODE, &headerMode), false);
    return true;
}

/**
 * @brief 取码流头
 * @param[out] header  码流头数据
 * @return true = 成功；false = 失败
 * @note MPP_ENC_GET_HDR_SYNC 要我们自己准备一块内存、包成 MppPacket 交给它，它把头写进来
 */
bool MppEncoder::getHeader(std::vector<uint8_t> &header) {
    std::vector<uint8_t> headerBuffer(64 * 1024); // 64 KB，码流头只有几十字节，足够了
    MppPacket rawPacket = nullptr;
    MPP_CHECK(mpp_packet_init(&rawPacket, headerBuffer.data(), headerBuffer.size()), false);
    MppPacketPtr packetPtr(rawPacket);
    mpp_packet_set_length(packetPtr.get(), 0); // 有效数据长度清零，MPP 从头开始写
    MPP_CHECK(encoderApi->control(encoderCtx, MPP_ENC_GET_HDR_SYNC, packetPtr.get()), false);
    const uint8_t *headerStart = static_cast<const uint8_t *>(mpp_packet_get_pos(packetPtr.get()));
    header.assign(headerStart, headerStart + mpp_packet_get_length(packetPtr.get()));
    return true;
}

/**
 * @brief 编码一帧，取回一个包
 * @param[in]  frameBuffer    画面 buffer
 * @param[out] encodedPacket  编出来的包
 * @param[in]  isInputEos     是不是送 EOS
 * @return true = 成功；false = 失败
 * @note 第 1 步 把 buffer 包成 MppFrame；第 2 步 送进编码器；第 3 步 取包；第 4 步 把包的数据复制出来
 */
bool MppEncoder::encode(MppBuffer frameBuffer, EncodedPacket &encodedPacket, bool isInputEos) {
    encodedPacket.data.clear();
    encodedPacket.isEos = false;
    encodedPacket.isKeyFrame = false;

    // 第 1 步 包成 MppFrame：只记下 buffer 和宽高、stride、格式，不复制像素
    MppFrame rawFrame = nullptr;
    MPP_CHECK(mpp_frame_init(&rawFrame), false);
    MppFramePtr framePtr(rawFrame);
    mpp_frame_set_width(framePtr.get(), config.width);
    mpp_frame_set_height(framePtr.get(), config.height);
    mpp_frame_set_hor_stride(framePtr.get(), horStride);
    mpp_frame_set_ver_stride(framePtr.get(), verStride);
    mpp_frame_set_fmt(framePtr.get(), MPP_FMT_YUV420SP);
    mpp_frame_set_eos(framePtr.get(), isInputEos ? 1 : 0);
    mpp_frame_set_buffer(framePtr.get(), isInputEos ? nullptr : frameBuffer); // EOS 帧不带画面

    // 第 2 步 送进编码器。送完 MppFrame 就可以释放了（编码器内部对 buffer 加了引用）
    MPP_CHECK(encoderApi->encode_put_frame(encoderCtx, framePtr.get()), false);
    framePtr.reset();

    // 第 3 步 取包。设了 MPP_POLL_BLOCK，这里会等到这一帧编完
    MppPacket rawPacket = nullptr;
    MPP_CHECK(encoderApi->encode_get_packet(encoderCtx, &rawPacket), false);
    if (!rawPacket) {
        return true; // 没有包（送 EOS 时可能出现），data 为空
    }
    MppPacketPtr packetPtr(rawPacket);
    encodedPacket.isEos = mpp_packet_get_eos(packetPtr.get());

    // 第 4 步 把包的数据复制出来。是不是 I 帧要从包的 meta 里读 KEY_OUTPUT_INTRA
    const size_t packetLength = mpp_packet_get_length(packetPtr.get());
    if (packetLength > 0) {
        RK_S32 isIntra = 0;
        if (mpp_packet_has_meta(packetPtr.get())) {
            MppMeta packetMeta = mpp_packet_get_meta(packetPtr.get());
            mpp_meta_get_s32(packetMeta, KEY_OUTPUT_INTRA, &isIntra);
        }
        encodedPacket.isKeyFrame = isIntra != 0;
        const uint8_t *packetStart = static_cast<const uint8_t *>(mpp_packet_get_pos(packetPtr.get()));
        encodedPacket.data.assign(packetStart, packetStart + packetLength);
    }
    return true; // packetPtr 在这里析构，包还给编码器
}

/**
 * @brief 销毁编码器
 * @return 无
 * @note 和 init 反过来：先释放配置，再销毁 MppCtx
 */
MppEncoder::~MppEncoder() {
    if (encoderCfg) {
        mpp_enc_cfg_deinit(encoderCfg);
    }
    if (encoderCtx) {
        mpp_destroy(encoderCtx);
    }
}
