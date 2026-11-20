#include "pipeline/decode_channel.h"

#include <unistd.h> // usleep
#include <utility>  // std::move

#include "common/log.h"

/**
 * @brief 构造一路通道
 * @param[in] requestedChannelIndex             第几路
 * @param[in] requestedQueueCapacityFrameCount  队列容量
 * @return 无
 * @note 冒号后面是"成员初始化列表"：const 成员（channelIndex）和没有默认构造的成员（frameQueue）只能在这里初始化
 */
DecodeChannel::DecodeChannel(int requestedChannelIndex, size_t requestedQueueCapacityFrameCount) :
    channelIndex(requestedChannelIndex), logName("ch" + std::to_string(requestedChannelIndex)),
    frameQueue(requestedQueueCapacityFrameCount) {}

/**
 * @brief 打开码流文件，初始化解码器
 * @param[in] streamPath  码流文件路径
 * @param[in] codingType  码流格式
 * @return true = 成功；false = 失败
 * @note 日志名字 "ch0"、"ch1"…… 传给解码段和解码器，4 路的日志混在一起时能分清
 */
bool DecodeChannel::open(const char *streamPath, MppCodingType codingType) {
    return decodeStage.open(streamPath, codingType, logName.c_str());
}

/**
 * @brief 解码线程的函数体
 * @return 无
 * @note 第 1 步 解一帧；第 2 步 解完了就 closeInput，出错就 abort；第 3 步 放进队列，被叫停就退出
 */
void DecodeChannel::runDecodeLoop() {
    while (true) {
        // 第 1 步 解一帧
        MppFramePtr framePtr;
        const ReadFrameResult readFrameResult = decodeStage.readNextFrame(framePtr);

        // 第 2 步 解完了：告诉队列"不会再放了"，拼图线程取完剩下的帧就会拿到 EndOfStream
        if (readFrameResult == ReadFrameResult::EndOfStream) {
            frameQueue.closeInput();
            isDecodeOk = true;
            return;
        }
        if (readFrameResult == ReadFrameResult::Error) {
            frameQueue.abort(); // 叫醒可能等在 pop 里的拼图线程，让它知道出错了
            isDecodeOk = false;
            return;
        }

        // 第 2.5 步（M5 测试用）到了指定的帧就睡一会儿，模拟这一路卡顿。getDecodedFrameCount 已经把这一帧算进去了，所以要减 1
        if (decodeStallAtFrameIndex >= 0 && decodeStage.getDecodedFrameCount() - 1 == decodeStallAtFrameIndex) {
            LOGI(logName.c_str(), "人为停顿 %d ms（第 %d 帧之前）", decodeStallDurationMs, decodeStallAtFrameIndex);
            usleep(static_cast<useconds_t>(decodeStallDurationMs) * 1000); // usleep 的单位是微秒
        }

        // 第 3 步 放进队列。满了就在 push 里等（背压）；返回 false 说明拼图线程叫停了整个流程
        if (!frameQueue.push(std::move(framePtr))) {
            isDecodeOk = false;
            return;
        }
    }
}
