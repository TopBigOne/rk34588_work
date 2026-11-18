#pragma once

#include <cstddef> // size_t
#include <cstdint> // uint8_t、int64_t
#include <rockchip/rk_mpi.h>

#include "common/mpp_utils.h"

/** feedPacket() / feedEndOfStream() 的结果 */
enum class FeedResult {
    Sent,  // 送进去了
    Full,  // 解码器的输入队列满了，这个包没送进去：先取几帧出来，再送同一个包
    Error, // 出错
};

/** getFrame() 的结果 */
enum class GetFrameResult {
    Frame, // 取到一帧（可能带画面，也可能是只带 EOS 标志的空帧）
    None,  // 现在没有帧可取
    Error, // 出错
};

/**
 * @brief MPP 视频硬解：一次送一整帧的码流（带 pts），取出 NV12 画面；内部处理 info change；支持 seek 后 reset
 * @note 和读裸 .h264 文件不一样：mp4 拆出来的每个包本来就是完整的一帧，所以**不打开** split_parse（内部分帧），
 *       一个包送一次。pts 跟着包送进去，MPP 原样带到解出来的帧上（B 帧重排后也对得上）
 */
class MppVideoDecoder {
public:
    MppVideoDecoder() = default;

    /**
     * @brief 销毁解码器（mpp_destroy），释放配置
     * @return 无
     * @note 成员 frameGroupPtr 在析构函数体执行完以后才析构，所以 buffer 组比 MppCtx 晚释放
     */
    ~MppVideoDecoder();

    MppVideoDecoder(const MppVideoDecoder &) = delete;            // 不能复制：两个对象拿着同一个 MppCtx，会 destroy 两次
    MppVideoDecoder &operator=(const MppVideoDecoder &) = delete; // 同上

    /**
     * @brief 创建解码器
     * @param[in] codingType  码流格式：MPP_VIDEO_CodingAVC（H.264）或 MPP_VIDEO_CodingHEVC（H.265）
     * @return true = 成功；false = 失败（已打印原因）
     * @note 只能调用一次。split_parse 设成 0：调用方保证一次送一整帧
     */
    bool init(MppCodingType codingType);

    /**
     * @brief 送一整帧的码流（Annex B 格式）
     * @param[in] data       码流数据，以起始码 00 00 00 01 开头
     * @param[in] sizeBytes  字节数
     * @param[in] ptsUs      这一帧的显示时间，微秒；MPP 不管单位，原样带到输出帧上
     * @return Sent = 送进去了；Full = 输入队列满了，先 getFrame 再送同一个包；Error = 出错
     * @note decode_put_packet 会把数据复制到解码器内部，返回后 data 可以马上释放
     */
    FeedResult feedPacket(const uint8_t *data, size_t sizeBytes, int64_t ptsUs);

    /**
     * @brief 告诉解码器"码流结束了"（送一个只带 EOS 标志的空包）
     * @return Sent / Full / Error，含义同 feedPacket
     * @note 送完以后接着 getFrame，把剩下的帧都取出来，最后会取到一个带 EOS 标志的帧
     */
    FeedResult feedEndOfStream();

    /**
     * @brief 取一个输出帧；遇到 info change 在内部处理掉，接着取
     * @param[out] outputFramePtr  取到的帧，只有返回 Frame 时有效；释放它时 buffer 回到解码器内存池
     * @return Frame = 取到一帧；None = 现在没有；Error = 出错
     * @note 不阻塞（MPP 默认的输出超时就是"没有就马上返回"）。
     *       取到的帧可能是只带 EOS 的空帧（没有 buffer），也可能带错误标志，调用方要判断
     */
    GetFrameResult getFrame(MppFramePtr &outputFramePtr);

    /**
     * @brief 清空解码器：丢掉还没解的码流和还没取走的帧，回到"等下一个关键帧"的状态（seek 时调用）
     * @return true = 成功；false = 失败
     * @note 调用前，手里拿着的帧（MppFramePtr）可以先不放，reset 不会动它们的 buffer；
     *       内存池和宽高设置保留，不用重新 info change
     */
    bool reset();

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

    MppBufferGroupPtr frameGroupPtr; // 解码器内存池：解出来的画面都放在这里的 buffer 里（DRM 类型，有 dma-buf fd）
    MppCtx decoderCtx = nullptr;     // 解码器实例
    MppApi *decoderApi = nullptr;    // 函数表：decode_put_packet、decode_get_frame、reset、control
    MppDecCfg decoderCfg = nullptr;  // 解码配置
    int infoChangeCount = 0;         // info change 发生了几次
    // 内存池最多几块 buffer。解码器自己要留参考帧（H.264 最多 16 帧），我们手里同时最多拿 1～2 帧，24 块够用
    static constexpr int kFrameBufferCount = 24;
};
