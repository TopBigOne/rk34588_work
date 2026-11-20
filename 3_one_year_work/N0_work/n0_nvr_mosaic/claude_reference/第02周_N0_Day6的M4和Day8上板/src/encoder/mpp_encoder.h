#pragma once

#include <cstdint> // uint8_t
#include <rockchip/rk_mpi.h>
#include <vector>

/** 编码参数 */
struct EncoderConfig {
    int width = 1920;     // 画面宽，单位像素
    int height = 1080;    // 画面高，单位像素
    int horStride = 0;    // 输入 buffer 一行占多少字节；0 = 按 ALIGN(width, 16) 算
    int verStride = 0;    // 输入 buffer 的 Y 平面占多少行；0 = 按 ALIGN(height, 16) 算
    MppCodingType codingType = MPP_VIDEO_CodingAVC; // MPP_VIDEO_CodingAVC = H.264，MPP_VIDEO_CodingHEVC = H.265
    MppEncRcMode rcMode = MPP_ENC_RC_MODE_CBR;      // 码率控制：CBR = 固定码率
    int bps = 4 * 1000 * 1000; // 目标码率，单位 bit/s
    int fps = 30;              // 帧率
    int gop = 60;              // 每隔多少帧一个 I 帧
};

/** encode() 交出来的一个编码包 */
struct EncodedPacket {
    std::vector<uint8_t> data; // 码流数据（复制出来的，和 MPP 的 buffer 无关）；可能是空的
    bool isKeyFrame = false;   // 是不是 I 帧
    bool isEos = false;        // 是不是最后一个包
};

/**
 * @brief MPP 编码器：送一帧 NV12，取一个码流包
 * @note 一个对象 = 一个 MppCtx。同步用法：encode 送一帧后马上取包（输出超时设成阻塞），返回时这一帧已经编完
 */
class MppEncoder {
public:
    MppEncoder() = default;
    MppEncoder(const MppEncoder &) = delete;            // 不能复制：两个对象拿着同一个 MppCtx，会 destroy 两次
    MppEncoder &operator=(const MppEncoder &) = delete; // 同上

    /**
     * @brief 销毁编码器，释放配置
     * @return 无
     * @note 先 deinit 配置，再 mpp_destroy
     */
    ~MppEncoder();

    /**
     * @brief 创建编码器，设置 prep（输入画面）、rc（码率控制）、codec（编码格式）三组参数
     * @param[in] requestedConfig  编码参数
     * @return true = 成功；false = 失败（已打印原因）
     * @note 只能调用一次
     */
    bool init(const EncoderConfig &requestedConfig);

    /**
     * @brief 取码流头：H.264 是 SPS + PPS，H.265 是 VPS + SPS + PPS
     * @param[out] header  码流头数据
     * @return true = 成功；false = 失败（已打印原因）
     * @note 取了就必须写进输出文件开头：MPP 认为头已经交给我们了，第一个 I 帧前面不会再带头
     */
    bool getHeader(std::vector<uint8_t> &header);

    /**
     * @brief 编码一帧（或者送 EOS），取回一个包
     * @param[in]  frameBuffer    要编码的画面所在的 MppBuffer；isInputEos 为 true 时不用，传 nullptr
     * @param[out] encodedPacket  编出来的包；data 可能为空
     * @param[in]  isInputEos     true = 不送画面，送一个"结束"标志，把编码器里剩的包取出来
     * @return true = 成功；false = 失败（已打印原因）
     * @note 返回时编码器已经读完 frameBuffer，调用方可以马上改这块 buffer 的内容（比如拼下一张大图）
     */
    bool encode(MppBuffer frameBuffer, EncodedPacket &encodedPacket, bool isInputEos);

private:
    MppCtx encoderCtx = nullptr;   // 编码器实例
    MppApi *encoderApi = nullptr;  // 函数表：encode_put_frame、encode_get_packet、control
    MppEncCfg encoderCfg = nullptr; // 编码配置
    EncoderConfig config;          // init 时传进来的参数
    int horStride = 0;             // 实际用的 stride（config 里是 0 时算出来）
    int verStride = 0;
};
