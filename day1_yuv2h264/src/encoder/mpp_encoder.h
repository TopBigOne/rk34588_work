/*
 * MppEncoder：把 MPP 硬件编码器包成一个类（设计见 guide/03 第 3 节）
 *
 * 类只管"编码器"本身：创建、设参数、取 SPS/PPS、送一帧取一包、销毁。
 * 输入缓冲区（MppBufferGroup + MppBuffer）和文件读写不在类里，由调用方（main）管理：
 * Day 3 接摄像头时，输入换成摄像头的 DMA-BUF，类不用改。
 *
 * 用法：
 *   MppEncoder encoder;
 *   if (!encoder.init(cfg)) return -1;          // ① 开机 + ② 设参数
 *   encoder.get_header(header);                  // ③ SPS/PPS
 *   while (!packet.eos) {
 *       ...往 frameBuffer 里填一帧（按 hor_stride() / ver_stride() 排布）...
 *       encoder.encode(frameBuffer, inputEos, packet);   // ⑤ 送一帧、取一包
 *   }
 *   // ⑥ 不用手动释放：encoder 离开作用域时，析构函数自动 mpp_enc_cfg_deinit + mpp_destroy
 */
#pragma once

#include <cstdint> // uint8_t
#include <rockchip/rk_mpi.h>
#include <vector>

// 编码参数（从命令行参数 Args 转过来）
struct EncoderConfig {
    int width  = 1920;
    int height = 1080;
    // MppCodingType：编码格式。
    MppCodingType type = MPP_VIDEO_CodingAVC; // MPP_VIDEO_CodingAVC = H.264，MPP_VIDEO_CodingHEVC = H.265
    // MppEncRcMode：码率控制模式。
    MppEncRcMode rcMode = MPP_ENC_RC_MODE_CBR; // CBR 恒定码率 / VBR 可变码率 / AVBR 自适应可变码率
    int bps             = 4 * 1000 * 1000;     // 目标码率，单位 bit/s
    int fps             = 30;
    int gop             = 60; // 每隔多少帧插一个 I 帧
};

// encode() 的输出：一段压缩好的码流
struct EncodedPacket {
    std::vector<uint8_t> data; // 码流数据（从 MppPacket 的 pos + length 拷出来）。可能是空的，见 encode()
    bool isKeyFrame = false;   // 是不是 I 帧（来自 MppMeta 的 KEY_OUTPUT_INTRA）
    bool eos        = false;   // 编码器说"码流全给你了"，调用方据此结束循环
};

class MppEncoder {
public:
    MppEncoder() = default;
    // 析构：释放 encoderCfg_ 和 encoderCtx_（init 中途失败也能正确释放已经申请的部分）
    ~MppEncoder();

    // 编码器实例只能有一个主人：禁止拷贝（拷贝后两个对象析构时会 mpp_destroy 同一个 ctx 两次）
    MppEncoder(const MppEncoder&)            = delete;
    MppEncoder& operator=(const MppEncoder&) = delete;

    /**
     * ① 开一台编码机 + ② 告诉它要什么效果
     * mpp_create → SET_OUTPUT_TIMEOUT
     *            → mpp_init
     *            → mpp_enc_cfg_init
     *            → GET_CFG
     *            → CFG_SET × N
     *            → SET_CFG
     *            → SET_HEADER_MODE
     * @return  成功返回 true；失败打印出错的那一句，返回 false（已申请的资源由析构函数释放）
     */
    bool init(const EncoderConfig& cfg);

    // 输入缓冲区的排布：调用方往 MppBuffer 里填数据时要按这个 stride 放（init 之后才有值）
    RK_S32 hor_stride() const {
        return horStride;
    }
    RK_S32 ver_stride() const {
        return verStride;
    }

    /**
     * ③ 取 SPS/PPS（H.265 还有 VPS）：MPP_ENC_GET_HDR_SYNC
     * 解码器必须先拿到它们才能解码，所以要写在输出文件的最开头
     * @param header  输出参数：头信息的字节
     */
    bool get_header(std::vector<uint8_t>& header);

    /**
     * ⑤ 送一帧、取一包：encode_put_frame + encode_get_packet
     * @param frameBuffer  装着一帧 NV12 的硬件缓冲区（按 hor_stride / ver_stride 排布，CPU 写完要 sync_end）
     * @param inputEos     true = 输入读完了：这次不送图像，只送一个带 EOS 标志的空帧（frameBuffer 被忽略）
     * @param encodedPacket       输出参数：这次取到的码流。data 可能为空（EOS 时的最后一个包可能是空的）
     * @return  成功返回 true；put_frame / get_packet 失败返回 false
     */
    bool encode(MppBuffer frameBuffer, bool inputEos, EncodedPacket& encodedPacket);

private:
    MppCtx encoderCtx_    = nullptr; // 一个编码器实例（句柄）。所有 encoderApi_->xxx(encoderCtx_, ...) 都要把它传回去
    MppApi* encoderApi_   = nullptr; // 操作编码器的函数表：control / encode_put_frame / encode_get_packet ...
    MppEncCfg encoderCfg_ = nullptr; // 编码参数集合（一张"订单"）。留着它，以后运行中改码率时还能用
    EncoderConfig cfg_;              // init 时传进来的参数，encode 时要用宽高

    // 输入缓冲区的排布，init 时按 16 对齐算出来（1920x1080 → 1920 / 1088）
    RK_S32 horStride = 0; // 水平跨距：内存里一行占多少【字节】（含行尾填充），不是像素数
    RK_S32 verStride = 0; // 垂直跨距：内存里 Y 平面占多少【行】（含底部填充），UV 平面从这一行开始
};
