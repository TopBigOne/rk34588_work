//
// DualStreamEncoder：M2、M3 共用的"一路画面 → 主码流 + 子码流"
// 管着两个编码器、子码流的 buffer（RGA 的输出）、两个码流文件，析构时自动释放
//
#pragma once

#include <cstddef>

#include "common/mpp_utils.h"
#include "common/nv12_picture.h"
#include "encoder/mpp_encoder.h"

/**
 * @brief 双码流编码：主码流直接编输入画面；子码流先用 RGA 把输入缩成 subPicture_，再编 subPicture_
 * @note 每一帧分两步调用：encode_main_and_resize_to_sub() → encode_sub()。
 *       拆成两步是为了 M3：两步之间，输入画面（摄像头的 buffer）已经没人读了，可以先 QBUF 还给摄像头
 */
class DualStreamEncoder {
public:
    DualStreamEncoder() = default;
    ~DualStreamEncoder() = default; // 成员都是 RAII（unique_ptr、MppEncoder），按声明的反序自动释放
    DualStreamEncoder(const DualStreamEncoder &) = delete;
    DualStreamEncoder &operator=(const DualStreamEncoder &) = delete;

    /**
     * @brief 准备好双码流：申请子码流 buffer、打开两个码流文件、初始化两个编码器并把 SPS/PPS 写到文件开头
     * @param[in] mainCfg         主码流编码参数（宽高、stride 要和输入画面一致）
     * @param[in] mainStreamPath  主码流文件路径，例如 "/userdata/av/main_1080p.h264"
     * @param[in] subCfg          子码流编码参数；子码流 buffer 的宽高、stride 也按它来
     * @param[in] subStreamPath   子码流文件路径
     * @return true = 都准备好了；false = 某一步失败（已打印原因，已拿到的资源由析构函数释放）
     * @note 只能调用一次
     */
    bool open(const EncoderConfigArg &mainCfg, const char *mainStreamPath, const EncoderConfigArg &subCfg,
              const char *subStreamPath);

    /**
     * @brief 一帧的前半：编码器 #1 编 mainPicture，码流写进主码流文件；RGA 把 mainPicture 缩进子码流 buffer
     * @param[in] mainPicture  输入画面，宽高、stride 要和 open() 时的 mainCfg 一致
     * @return true = 成功；false = 编码、写文件或 RGA 失败
     * @note ⭐ 返回时编码器 #1 和 RGA 都已经读完 mainPicture（两个调用都是同步的），mainPicture 的 buffer 可以交还了
     */
    bool encode_main_and_resize_to_sub(const Nv12Picture &mainPicture);

    /**
     * @brief 一帧的后半：编码器 #2 编子码流 buffer（RGA 刚缩好的那一帧），码流写进子码流文件
     * @return true = 成功；false = 编码或写文件失败
     * @note 必须在 encode_main_and_resize_to_sub() 成功以后调用；它只读子码流 buffer，不读输入画面
     */
    bool encode_sub();

    /**
     * @brief 收尾：两个编码器都送 EOS，把内部还没输出的码流取完写进文件
     * @return true = 两路都排空了；false = 编码或写文件失败
     * @note 送完最后一帧后调用一次
     */
    bool drain_both();

    /** @brief 已经写进主码流文件的字节数（不含开头的 SPS/PPS） @return 字节数 */
    size_t get_main_stream_bytes() const { return mainStreamBytes_; }
    /** @brief 已经写进子码流文件的字节数（不含开头的 SPS/PPS） @return 字节数 */
    size_t get_sub_stream_bytes() const { return subStreamBytes_; }
    /** @brief RGA 缩放一共花了多少毫秒（所有帧累加） @return 毫秒 */
    double get_resize_total_ms() const { return resizeTotalMs_; }

private:
    // ⚠️ 声明顺序就是释放的反序：先关编码器，再关文件，再还子码流 buffer，最后释放 group
    MppBufferGroupPtr bufferGroup_;  // 子码流 buffer 从这里申请
    Nv12Picture subPicture_;         // 子码流画面：RGA 的输出，编码器 #2 的输入
    MppBufferPtr subBufOwner_;       // 管 subPicture_.mppBuf 的释放
    FilePtr mainStreamFile_;
    FilePtr subStreamFile_;
    MppEncoder mainEncoder_;
    MppEncoder subEncoder_;
    EncodedPacket mainPacket_;       // 每帧复用，避免每帧重新分配 vector
    EncodedPacket subPacket_;
    size_t mainStreamBytes_ = 0;
    size_t subStreamBytes_ = 0;
    double resizeTotalMs_ = 0;
};
