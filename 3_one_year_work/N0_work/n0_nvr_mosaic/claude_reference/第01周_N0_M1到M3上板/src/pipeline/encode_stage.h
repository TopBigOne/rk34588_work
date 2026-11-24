#pragma once

#include <cstdint> // int64_t

#include "common/nv12_picture.h"
#include "encoder/mpp_encoder.h"
#include "sink/stream_file_writer.h"

/**
 * @brief 编码段：把一张 NV12 画面编码，码流写进文件
 * @note 不管画面从哪来（这里是拼好的大图），只要是 NV12、宽高和 open 时一样就行
 */
class EncodeStage {
public:
    /**
     * @brief 初始化编码器，打开输出文件，把码流头写到文件开头
     * @param[in] config      编码参数；horStride、verStride 要和以后送进来的画面一致
     * @param[in] outputPath  输出文件路径
     * @return true = 成功；false = 失败（已打印原因）
     * @note 只能调用一次
     */
    bool open(const EncoderConfig &config, const char *outputPath);

    /**
     * @brief 编码一张画面，码流写进文件
     * @param[in] picture  要编码的画面
     * @return true = 成功；false = 失败（已打印原因）
     * @note 返回时编码器已经读完 picture 的 buffer，调用方可以马上改它（拼下一张大图）
     */
    bool encodePicture(const Nv12Picture &picture);

    /**
     * @brief 排空编码器：送 EOS，把编码器里剩的包都取出来写进文件
     * @return true = 拿到了 EOS 包，或者送了 kDrainTryCount 次还没拿到（只打警告）；false = 编码或写文件出错
     * @note 所有画面都送完以后调用一次
     */
    bool drainEncoder();

    /**
     * @brief 编了几帧
     * @return 帧数；等于送进来的大图张数
     * @note 用来验收
     */
    int getEncodedFrameCount() const { return encodedFrameCount; }

    /**
     * @brief 输出文件一共写了多少字节（包括码流头）
     * @return 字节数
     * @note 用来验收
     */
    int64_t getOutputBytes() const { return writer.getWrittenBytes(); }

private:
    static constexpr int kDrainTryCount = 30; // 排空时最多送几次 EOS
    StreamFileWriter writer;     // 写码流文件
    MppEncoder encoder;          // MPP 编码器
    EncodedPacket encodedPacket; // 每次编码的包，反复用，省得每帧新建 vector
    int encodedFrameCount = 0;   // 编了几帧
};
