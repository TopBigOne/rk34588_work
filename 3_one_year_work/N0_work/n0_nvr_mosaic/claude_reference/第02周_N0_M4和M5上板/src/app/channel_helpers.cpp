#include "app/channel_helpers.h"

#include "app/app_config.h"
#include "common/log.h"

/**
 * @brief 建好并打开每一路通道
 * @param[out] channelPtrs  建好的通道
 * @return true = 成功；false = 失败
 * @note make_unique<DecodeChannel>(参数...)：new 一个 DecodeChannel，参数原样交给它的构造函数，结果包进 unique_ptr
 */
bool openAllChannels(std::vector<std::unique_ptr<DecodeChannel>> &channelPtrs) {
    for (int channelIndex = 0; channelIndex < kChannelCount; ++channelIndex) {
        channelPtrs.push_back(std::make_unique<DecodeChannel>(channelIndex, kFrameQueueCapacity));
        if (!channelPtrs.back()->open(kChannelInputs[channelIndex].streamPath,
                                      kChannelInputs[channelIndex].codingType)) {
            return false;
        }
    }
    return true;
}

/**
 * @brief 叫停所有通道
 * @param[in,out] channelPtrs  所有通道
 * @return 无
 * @note abort 可以重复调用，已经结束的通道 abort 也没关系
 */
void abortAllChannels(std::vector<std::unique_ptr<DecodeChannel>> &channelPtrs) {
    for (std::unique_ptr<DecodeChannel> &channelPtr : channelPtrs) {
        channelPtr->getFrameQueue().abort();
    }
}

/**
 * @brief 打印每一路的结果
 * @param[in] channelPtrs   所有通道
 * @param[in] composeStage  拼图段
 * @param[in] tag           日志标签
 * @return true = 全部正常；false = 有一路不正常
 * @note "解出的帧全部贴上了"：贴的帧数 == 解出的帧数，说明队列里一帧都没丢
 */
bool printChannelResults(const std::vector<std::unique_ptr<DecodeChannel>> &channelPtrs,
                         const MosaicComposeStage &composeStage, const char *tag) {
    bool isAllOk = true;
    for (const std::unique_ptr<DecodeChannel> &channelPtr : channelPtrs) {
        const int channelIndex = channelPtr->getChannelIndex();
        const DecodeStage &decodeStage = channelPtr->getDecodeStage();
        const int drawnFrameCount = composeStage.getDrawnFrameCount(channelIndex);
        LOGI(tag, "ch%d %s：解码%s，正常帧 %d，错误帧 %d，贴进大图 %d 帧，队列最多排过 %zu 帧", channelIndex,
             kChannelInputs[channelIndex].streamPath, channelPtr->isDecodeLoopOk() ? "正常" : "出错",
             decodeStage.getDecodedFrameCount(), decodeStage.getErrorFrameCount(), drawnFrameCount,
             channelPtr->getFrameQueue().getMaxQueuedFrameCount());
        if (!channelPtr->isDecodeLoopOk() || decodeStage.getErrorFrameCount() > 0 ||
            drawnFrameCount != decodeStage.getDecodedFrameCount()) {
            isAllOk = false;
        }
    }
    return isAllOk;
}
