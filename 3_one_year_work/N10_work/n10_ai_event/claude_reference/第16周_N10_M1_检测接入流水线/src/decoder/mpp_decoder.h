// 来源：3_one_year_work/N0_work/n0_nvr_mosaic/claude_reference/第02周_N0_M4和M5上板/src/decoder/mpp_decoder.h
#pragma once

#include <cstddef> // size_t
#include <cstdint> // uint8_t
#include <rockchip/rk_mpi.h>
#include <string>

#include "common/mpp_utils.h"

/** feed() 的结果 */
enum class FeedResult {
    Sent,  // 送进去了
    Full,  // 解码器的输入队列满了，这块没送进去，过一会儿再送同一块
    Error, // 出错
};

/** getFrame() 的结果 */
enum class GetFrameResult {
    Frame, // 取到一帧（可能带画面，也可能是只带 EOS 的空帧）
    None,  // 现在没有帧可取
    Error, // 出错
};

/**
 * @brief MPP 解码器：送码流、取帧，内部处理 info change
 * @note 一个对象 = 一个 MppCtx = 一路解码。多路就建多个对象，互相独立，可以放在不同线程里各用各的
 */
class MppDecoder {
public:
    MppDecoder() = default;

    /**
     * @brief 销毁解码器（mpp_destroy），释放配置
     * @return 无
     * @note 成员 frameGroupPtr 在析构函数体执行完以后才析构，所以 buffer 组比 MppCtx 晚释放
     */
    ~MppDecoder();

    MppDecoder(const MppDecoder &) = delete;            // 不能复制：两个对象拿着同一个 MppCtx，会 destroy 两次
    MppDecoder &operator=(const MppDecoder &) = delete; // 同上

    /**
     * @brief 创建解码器，打开内部分帧
     * @param[in] codingType        码流格式：MPP_VIDEO_CodingAVC（H.264）或 MPP_VIDEO_CodingHEVC（H.265）
     * @param[in] requestedLogName  日志里显示的名字，例如 "ch0"；多路同时跑时用它区分是哪一路
     * @return true = 成功；false = 失败（已打印原因）
     * @note 只能调用一次
     */
    bool init(MppCodingType codingType, const char *requestedLogName);

    /**
     * @brief 送一块码流
     * @param[in] data       码流数据
     * @param[in] sizeBytes  字节数，可以是 0（文件最后一块正好读空时）
     * @param[in] isEos      true = 这是最后一块，打上 EOS 标志
     * @return Sent = 送进去了；Full = 输入队列满了，过一会儿再送同一块；Error = 出错
     * @note decode_put_packet 会把数据复制到解码器内部，返回后 data 可以马上复用
     */
    FeedResult feed(const uint8_t *data, size_t sizeBytes, bool isEos);

    /**
     * @brief 取一个输出帧；遇到 info change 在内部处理掉，接着取
     * @param[out] outputFramePtr  取到的帧，只有返回 Frame 时有效；释放它时 buffer 回到解码器内存池
     * @return Frame = 取到一帧；None = 现在没有；Error = 出错
     * @note 取到的帧可能是只带 EOS 的空帧（没有 buffer），调用方要判断
     */
    GetFrameResult getFrame(MppFramePtr &outputFramePtr);

    /**
     * @brief info change 发生了几次
     * @return 次数；正常的文件是 1 次（第一帧之前）
     * @note 用来验收
     */
    int getInfoChangeCount() const { return infoChangeCount; }

private:
    /**
     * @brief 处理 info change：按新的 buffer 大小准备内存池，告诉解码器"准备好了"
     * @param[in] infoChangeFrame  带 info change 标志的帧，从里面读宽高、stride、buffer 大小
     * @return true = 成功；false = 失败
     * @note 第一次建内存池；以后再发生（分辨率变了）就清空重配
     */
    bool handleInfoChange(MppFrame infoChangeFrame);

    std::string logName;              // 日志里显示的名字，例如 "ch0"
    MppBufferGroupPtr frameGroupPtr;  // 解码器内存池：解出来的画面都放在这里的 buffer 里
    MppCtx decoderCtx = nullptr;      // 解码器实例
    MppApi *decoderApi = nullptr;     // 函数表：decode_put_packet、decode_get_frame、control
    MppDecCfg decoderCfg = nullptr;   // 解码配置
    int infoChangeCount = 0;          // info change 发生了几次
    static constexpr int kFrameBufferCount = 24; // 内存池最多几块 buffer
};
