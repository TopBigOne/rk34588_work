/*
 * MppEncoder 的实现。每个函数对应原来 main() 里的一段（见 guide/03 第 3 节的对照表）
 */
#include "encoder/mpp_encoder.h"

#include "common/mpp_utils.h"

// 这个文件里的函数都返回 bool：MPP 调用失败就打印并 return false
#define CHECK(expr) MPP_CHECK(expr, false)

// 设置一个编码参数。key 是字符串，拼错时 mpp_enc_cfg_set_s32 返回 MPP_NOK，
// 用 CHECK 包起来就能立刻看到是哪一行出错
#define CFG_SET(key, val) CHECK(mpp_enc_cfg_set_s32(encoderCfg_, key, val))

// ==================== 析构：⑥ 收拾干净 ====================
// 不管 init 走到哪一步失败，还是正常用完，都在这里释放。没申请过的（还是 nullptr）就跳过
MppEncoder::~MppEncoder() {
    if (encoderCfg_) {
        mpp_enc_cfg_deinit(encoderCfg_); // 参数对象不属于 encoderCtx_，要自己释放
    }
    if (encoderCtx_) {
        mpp_destroy(encoderCtx_); // 销毁编码器实例。encoderApi_ 指向全局函数表，不用释放
    }
}

// ==================== ① 开一台编码机 + ② 告诉它要什么效果 ====================
bool MppEncoder::init(const EncoderConfig& cfg) {
    if (encoderCtx_) {
        printf("[FAIL] MppEncoder::init 只能调用一次\n");
        return false;
    }
    cfg_ = cfg;

    // stride：硬件编码器要求按 16 对齐。1920 → 1920（本来就是 16 的倍数），1080 → 1088
    horStride_ = ALIGN(cfg.width, 16);
    verStride_ = ALIGN(cfg.height, 16);

    // 码率下限：CBR 要求码率稳定，下限只比目标低 1/16；VBR / AVBR 允许码率随画面变化，下限放宽到目标的 1/16
    const int bpsMin = (cfg.rcMode == MPP_ENC_RC_MODE_CBR) ? cfg.bps * 15 / 16 : cfg.bps / 16;
    // MppPollType：get_packet 拿不到结果时怎么办。
    // MPP_POLL_BLOCK = 一直等到有结果；MPP_POLL_NON_BLOCK = 立刻返回；正数 = 最多等多少毫秒
    MppPollType outputTimeout = MPP_POLL_BLOCK;
    // MppEncHeaderMode：EACH_IDR = 每个 IDR 帧前面都自动带一份 SPS/PPS，播放器从中间开始播也能解码
    MppEncHeaderMode headerMode = MPP_ENC_HEADER_MODE_EACH_IDR;

    // 1. create and init：创建实例，拿到 encoderCtx_（句柄）和 encoderApi_（函数表）
    // 原型 mpp_create(MppCtx* ctx, MppApi** mpi)：两个参数都是"输出参数"，
    // 所以传 &encoderCtx_、&encoderApi_，函数把结果写回来
    CHECK(mpp_create(&encoderCtx_, &encoderApi_));
    // encoderApi_->control(encoderCtx_, 命令, 参数)：类似 ioctl，第 2 个参数（MpiCmd）决定干什么，
    // 第 3 个参数（MppParam = void*）的类型由命令决定。这条命令要传 MppPollType 的地址。
    // 按官方示例的顺序，要在 mpp_init 之前设置
    CHECK(encoderApi_->control(encoderCtx_, MPP_SET_OUTPUT_TIMEOUT, &outputTimeout));
    // mpp_init：把实例初始化成编码器（MPP_CTX_ENC），并决定编 H.264 还是 H.265
    CHECK(mpp_init(encoderCtx_, MPP_CTX_ENC, cfg.type));

    // 2. 先拿默认配置，再改
    // mpp_enc_cfg_init：创建一个空的参数对象；MPP_ENC_GET_CFG：把编码器当前的默认参数读进来
    CHECK(mpp_enc_cfg_init(&encoderCfg_));
    CHECK(encoderApi_->control(encoderCtx_, MPP_ENC_GET_CFG, encoderCfg_));
    // prep: 开头的参数描述"送进来的图像长什么样"，必须和 encode() 里 MppFrame 上设置的 5 个值一致
    CFG_SET("prep:width", cfg.width);
    CFG_SET("prep:height", cfg.height);
    CFG_SET("prep:hor_stride", horStride_);
    CFG_SET("prep:ver_stride", verStride_);
    CFG_SET("prep:format", MPP_FMT_YUV420SP); // nv12（MPP_FMT_YUV420SP = NV12，SP = Semi-Planar）
    // rc: 开头的参数是码率控制（Rate Control）
    CFG_SET("rc:mode", cfg.rcMode);

    // 帧率：输入、输出都是 -fps 指定的值（flex = 0 表示固定帧率）
    // 帧率用分数表示：num / denom，30 / 1 = 30fps
    CFG_SET("rc:fps_in_flex", 0);
    CFG_SET("rc:fps_in_num", cfg.fps);
    CFG_SET("rc:fps_in_denom", 1);

    CFG_SET("rc:fps_out_flex", 0);
    CFG_SET("rc:fps_out_num", cfg.fps);
    CFG_SET("rc:fps_out_denom", 1);
    // 码率上下限：CBR 为目标的 ±1/16；VBR / AVBR 下限放宽到 1/16（bpsMin 在前面算好）
    CFG_SET("rc:bps_target", cfg.bps);
    CFG_SET("rc:bps_max", cfg.bps * 17 / 16);
    CFG_SET("rc:bps_min", bpsMin);

    // GOP：每隔多少帧插一个 I 帧。I 帧能独立解码，P 帧要参考前面的帧
    CFG_SET("rc:gop", cfg.gop);
    CFG_SET("codec:type", cfg.type);
    // h264 专属参数（h264: 开头的 key 只有 H.264 认识，所以放在 if 里；H.265 用编码器的默认值）
    if (cfg.type == MPP_VIDEO_CodingAVC) {
        CFG_SET("h264:profile", 100); // High Profile（66 = Baseline，77 = Main，100 = High）
        // level：MPP 只会按分辨率自动调高 level，不看帧率（h264e_sps.c 139～159 行），
        // 1080p 会停在 4.0。帧率超过 30 时要自己设：1080p60 → 4.2，720p60 → 3.2
        if (cfg.fps > 30) {
            CFG_SET("h264:level", cfg.height > 720 ? 42 : 32);
        }
        CFG_SET("h264:cabac_en", 1); // 开启cabac ,profile 是Main以上才能使用（CABAC 比 CAVLC 压缩率更高）
        CFG_SET("h264:cabac_idc", 0); // CABAC 初始化表编号，取值 0～2
    }

    // 3. 参数真正生效
    // MPP_ENC_SET_CFG：把 encoderCfg_ 里的参数交给编码器。不调这一句，前面的 CFG_SET 全部白设
    CHECK(encoderApi_->control(encoderCtx_, MPP_ENC_SET_CFG, encoderCfg_));
    // 每个IDR帧前，都带SPS/PPS（这条命令要传 MppEncHeaderMode 的地址）
    CHECK(encoderApi_->control(encoderCtx_, MPP_ENC_SET_HEADER_MODE, &headerMode));
    return true;
}

// ==================== ③ 取 SPS/PPS ====================
bool MppEncoder::get_header(std::vector<uint8_t>& header) {
    // 编码器把头信息 memcpy 到我们给的容器里（mpp_enc_impl.cpp 977～999 行的 mpp_packet_copy），
    // 是 CPU 拷贝，所以普通内存就行，不需要从 DRM 内存池申请。
    // ⚠️ mpp_packet_copy 不检查容器大小，容器要给够：SPS/PPS/VPS 一共只有几十到一两百字节，64KB 绰绰有余
    std::vector<uint8_t> container(64 * 1024);
    MppPacket rawPacket = nullptr;
    CHECK(mpp_packet_init(&rawPacket, container.data(), container.size()));
    MppPacketPtr headerPacket(rawPacket); // 交给 RAII 句柄：函数返回时自动 mpp_packet_deinit
    // ⚠️ 一定要清零：mpp_packet_init 会把 length 设成整个容器的大小（mpp_packet.cpp 75 行），
    // 不清零编码器会以为容器已经装满了
    mpp_packet_set_length(headerPacket.get(), 0);
    // MPP_ENC_GET_HDR_SYNC：编码器把 SPS/PPS（H.265 还有 VPS）写进 headerPacket
    CHECK(encoderApi_->control(encoderCtx_, MPP_ENC_GET_HDR_SYNC, headerPacket.get()));

    // 读码流永远用 pos（有效数据从哪开始）+ length（有多长），不要用 data + size（那是整个容器）
    const auto* pos = static_cast<const uint8_t*>(mpp_packet_get_pos(headerPacket.get()));
    header.assign(pos, pos + mpp_packet_get_length(headerPacket.get()));
    return true;
}

// ==================== ⑤ 送一帧、取一包 ====================
bool MppEncoder::encode(MppBuffer frameBuffer, bool inputEos, EncodedPacket& packet) {
    packet.data.clear();
    packet.isKeyFrame = false;
    packet.eos        = false;

    // 5.1 包装成 MppFrame
    // MppFrame 是贴在 frameBuffer 上的"标签"：告诉编码器这块内存里的图像多宽多高、stride 多少、什么格式
    MppFrame rawFrame = nullptr;
    CHECK(mpp_frame_init(&rawFrame));
    MppFramePtr inputFrame(rawFrame); // 交给 RAII 句柄：函数返回时自动 mpp_frame_deinit
    // 这 5 个值必须和 init() 里的 prep:* 参数一致
    mpp_frame_set_width(inputFrame.get(), cfg_.width);
    mpp_frame_set_height(inputFrame.get(), cfg_.height);
    mpp_frame_set_hor_stride(inputFrame.get(), horStride_);
    mpp_frame_set_ver_stride(inputFrame.get(), verStride_);
    mpp_frame_set_fmt(inputFrame.get(), MPP_FMT_YUV420SP);
    // eos = 1：告诉编码器"这是最后一次了，没有图像了"
    mpp_frame_set_eos(inputFrame.get(), inputEos);
    // 最后一次，送一个空帧，没有图像，只带eos标志
    // set_buffer 会给 frameBuffer 的引用计数 +1，所以 inputFrame 释放时只是 -1，frameBuffer 本身不会被释放
    mpp_frame_set_buffer(inputFrame.get(), inputEos ? nullptr : frameBuffer);

    // 5.2 送进去：硬件通过 DMA 读 frameBuffer，开始压缩。
    // 阻塞模式下，等编码器用完这一帧才返回（mpp.cpp 723～746 行），
    // 所以返回后可以马上释放 inputFrame、往 frameBuffer 里写下一帧
    CHECK(encoderApi_->encode_put_frame(encoderCtx_, inputFrame.get()));
    inputFrame.reset(); // 用完马上释放（不 reset 也行，函数返回时会自动释放）

    // 5.3 取出来：拿到一个压缩好的 MppPacket（因为设了 MPP_POLL_BLOCK，会一直等到有结果）。
    // 我们没给编码器准备输出 packet，所以这个 packet 的内存是编码器自己分配的，用完要 deinit
    MppPacket rawPacket = nullptr;
    CHECK(encoderApi_->encode_get_packet(encoderCtx_, &rawPacket));
    if (!rawPacket) {
        return true; // 这次没取到（data 为空），调用方继续下一轮
    }
    MppPacketPtr outputPacket(rawPacket); // 每个 packet 用完都要释放，否则内存一直涨：交给 RAII 句柄

    // 编码器说：这是最后一个，调用方据此结束循环
    packet.eos = mpp_packet_get_eos(outputPacket.get());
    // EOS 对应的最后一个包可能是空的（length = 0），不算一帧，data 保持为空
    const size_t packetLength = mpp_packet_get_length(outputPacket.get());
    if (packetLength > 0) {
        // MppMeta 是挂在 packet 上的"附加信息口袋"，按 key 取值。
        // ⚠️ 是 KEY_OUTPUT_INTRA（整数，1 = I 帧），不是 KEY_OUTPUT_FRAME（存的是 frame 对象，用 get_s32 取会失败）
        RK_S32 isIntra = 0;
        if (mpp_packet_has_meta(outputPacket.get())) {
            mpp_meta_get_s32(mpp_packet_get_meta(outputPacket.get()), KEY_OUTPUT_INTRA, &isIntra);
        }
        packet.isKeyFrame = isIntra != 0;

        // 把码流拷出来：outputPacket 函数返回时就释放了，调用方拿到的是一份独立的副本
        const auto* pos = static_cast<const uint8_t*>(mpp_packet_get_pos(outputPacket.get()));
        packet.data.assign(pos, pos + packetLength);
    }
    return true;
}
