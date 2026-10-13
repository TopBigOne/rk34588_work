/*
 * MppDecoder：把 MPP 硬件解码器包成一个类（设计见 guide/03_重构成类.md 第 3.1 节）
 *
 * 类只管"解码器"本身：创建、打开内部分帧、送码流、取图像、销毁。
 * info change 和内存池也在类里处理掉：外面调用 get_frame 永远拿不到 info change 帧，
 * 只会拿到正常图像、错误帧，或者只带 EOS 的空帧（见 doc/decode_get_frame拿到的几种帧.md）。
 *
 * 用法：
 *   MppDecoder decoder;
 *   if (!decoder.init(MPP_VIDEO_CodingAVC)) return -1;      // M1
 *   decoder.feed(data, size, isLast);                         // 返回 Full 时数据留着，等 1ms 原样再送
 *   MppFramePtr frame;
 *   while (decoder.get_frame(frame) == GetResult::Frame) {   // 把现在能取的帧全取出来
 *       ...用 frame.get()...                                  // frame 离开作用域自动 mpp_frame_deinit
 *   }
 *   // 不用手动释放：decoder 析构时自动 mpp_dec_cfg_deinit → mpp_destroy → 释放内存池
 */
#pragma once

#include "common/mpp_utils.h" // MppBufferGroupPtr、MppFramePtr
#include <cstddef> // size_t
#include <cstdint> // uint8_t
#include <rockchip/rk_mpi.h>

// feed() 的结果
enum class FeedResult {
    Sent, // 送进去了，可以读下一块
    Full, // 输入队列满了：这块数据留着，等一会儿原样再送（正常的流量控制，不是错误）
    Error, // 出错（已经打印原因）
};

// get_frame() 的结果
enum class GetResult {
    Frame, // 拿到一个 MppFrame（正常图像 / 错误帧 / 只带 EOS 的空帧，调用方再细分）
    None, // 现在没有输出（非阻塞），回去继续送码流
    Error, // 出错（已经打印原因）
};

class MppDecoder {
public:
    MppDecoder() = default;

    /**
     * @brief 析构：mpp_dec_cfg_deinit → mpp_destroy；内存池 frameGroup_ 是成员，函数体执行完后才释放
     */
    ~MppDecoder();

    /**
     * @brief 禁止拷贝：解码器实例只能有一个主人
     * @note 【精读】检验：拷贝一个 MppDecoder 之后，两个对象析构时会发生什么？
     */
    MppDecoder(const MppDecoder&)            = delete;
    MppDecoder& operator=(const MppDecoder&) = delete;

    /**
     * @brief M1：mpp_create()
     *     → mpp_init(MPP_CTX_DEC, type)
     *     → mpp_dec_cfg_init → GET_CFG → base:split_parse = 1 → SET_CFG
     * 打开内部分帧：我们按固定长度读文件，不是一帧一帧读，让 MPP 自己找帧边界
     * @param type  编码格式：MPP_VIDEO_CodingAVC（H.264）/ MPP_VIDEO_CodingHEVC（H.265）
     * @return  成功返回 true；失败打印出错的那一句，返回 false（已申请的资源由析构函数释放）
     */
    bool init(MppCodingType type);

    /**
     * @brief 送一块码流：mpp_packet_init → decode_put_packet → mpp_packet_deinit
     * packet 不带 MppBuffer 时，MPP 收下会把数据拷走（mpp.cpp 第 433–440 行），
     * 所以每次都新建一个 packet、送完就释放；返回 Full 时 MPP 没有收下，调用方保留数据原样再送即可
     * @param data  码流数据
     * @param size  字节数
     * @param eos   true = 这是最后一块，给 packet 打上 EOS
     * @return  Sent / Full / Error
     */
    FeedResult feed(const uint8_t* data, size_t size, bool eos);

    /**
     * @brief 取一个输出帧：decode_get_frame（非阻塞）
     * 取到 info change 帧时在这里处理掉（handle_info_change），然后接着取，直到拿到别的帧或者没有输出
     * @param frame  输出参数：拿到的帧（RAII，离开作用域自动 mpp_frame_deinit，buffer 回到内存池）
     * @return  Frame / None / Error
     */
    GetResult get_frame(MppFramePtr& frame);

    /**
     * @brief 获取 info change 的次数
     * @return 到目前为止遇到了几次 info change（统计用；aaa.264 是 1 次）
     */
    RK_S32 info_change_count() const {
        return infoChangeCount_;
    }

private:
    /**
     * @brief 处理 info change：打印宽高、stride、buf_size，配置内存池，通知解码器继续
     *   第一次：mpp_buffer_group_get_internal 建池子
     *   以后（码流中途换分辨率）：mpp_buffer_group_clear 清掉旧 buffer，池子复用
     *   → mpp_buffer_group_limit_config(buf_size, 24) → SET_EXT_BUF_GROUP → SET_INFO_CHANGE_READY
     * 内存池和 count 的设计见 doc/mpp_buffer_group_limit_config.md
     * @param frame  info change 帧（只带宽高、stride、buf_size，没有图像）
     * @return  成功返回 true；失败打印出错的那一句，返回 false
     */
    bool handle_info_change(MppFrame frame);

    // 【精读】为什么内存池是成员 MppBufferGroupPtr，而 ctx / cfg 是裸句柄、在析构函数体里手动释放
    // ⚠️ 内存池必须在解码器销毁之后才释放（解码器销毁前，参考帧还在用池子里的 buffer）：
    //    ctx / cfg 在析构函数体里释放，函数体先执行；frameGroup_ 是成员，函数体执行完才析构 → 顺序正确
    MppBufferGroupPtr frameGroup_; // 解码输出用的内存池（第一次 info change 时创建）
    MppCtx decoderCtx_      = nullptr; // 一个解码器实例。所有 decoderApi_->xxx(decoderCtx_, ...) 都要把它传回去
    MppApi* decoderApi_     = nullptr; // 操作解码器的函数表：control / decode_put_packet / decode_get_frame ...
    MppDecCfg decoderCfg_   = nullptr; // 解码参数（只用来设 split_parse）
    RK_S32 infoChangeCount_ = 0;

    // 内存池最多多少块 buffer。H.264 / H.265 的 DPB 最多 16 帧，再加正在解的、排队的、调用方手里的，24 块足够
    static constexpr RK_S32 kFrameBufferCount = 24;
};
