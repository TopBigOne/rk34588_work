#include "pipeline/encode_stage.h"

#include <vector>

#include "common/log.h"

static const char *const kTag = "EncodeStage";

/**
 * @brief 初始化编码器，写码流头
 * @param[in] config      编码参数
 * @param[in] outputPath  输出文件路径
 * @return true = 成功；false = 失败
 * @note 第 1 步 打开输出文件；第 2 步 初始化编码器；第 3 步 取码流头写到文件开头
 */
bool EncodeStage::open(const EncoderConfig &config, const char *outputPath) {
    // 第 1 步 打开输出文件。是 FIFO 的话，这里会等到播放器打开它
    if (!writer.open(outputPath)) {
        return false;
    }
    // 第 2 步 初始化编码器
    if (!encoder.init(config)) {
        return false;
    }
    // 第 3 步 取码流头写到文件开头。取了不写，第一个 I 帧前面就没有头，播放器解不出前面的帧
    std::vector<uint8_t> header;
    if (!encoder.getHeader(header)) {
        return false;
    }
    LOGI(kTag, "码流头 %zu 字节", header.size());
    return writer.writeBytes(header);
}

/**
 * @brief 编码一张画面
 * @param[in] picture  要编码的画面
 * @return true = 成功；false = 失败
 * @note 一送一取，编出来的包马上写文件
 */
bool EncodeStage::encodePicture(const Nv12Picture &picture) {
    if (!encoder.encode(picture.mppBuf, encodedPacket, false)) {
        return false;
    }
    if (!writer.writeBytes(encodedPacket.data)) {
        return false;
    }
    encodedFrameCount++;
    return true;
}

/**
 * @brief 排空编码器
 * @return true = 成功；false = 失败
 * @note 编码器内部可能还压着包没吐出来；送"空帧 + EOS"，一直取到带 EOS 标志的包为止
 */
bool EncodeStage::drainEncoder() {
    for (int tryIndex = 0; tryIndex < kDrainTryCount; ++tryIndex) {
        if (!encoder.encode(nullptr, encodedPacket, true)) {
            return false;
        }
        if (!writer.writeBytes(encodedPacket.data)) {
            return false;
        }
        if (encodedPacket.isEos) {
            return true;
        }
    }
    LOGW(kTag, "送了 %d 次 EOS 还没拿到 EOS 包", kDrainTryCount);
    return true;
}
