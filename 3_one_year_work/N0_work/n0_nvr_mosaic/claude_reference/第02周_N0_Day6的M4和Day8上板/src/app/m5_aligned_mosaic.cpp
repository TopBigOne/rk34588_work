#include "app/m5_aligned_mosaic.h"

#include <cstdint> // int64_t
#include <memory>  // std::unique_ptr
#include <thread>
#include <vector>

#include "app/app_config.h"
#include "app/channel_helpers.h"
#include "common/log.h"
#include "common/time_utils.h"
#include "pipeline/decode_channel.h"
#include "pipeline/encode_stage.h"
#include "pipeline/mosaic_compose_stage.h"
#include "sync/master_clock.h"

/**
 * @brief 按 kM5ChannelInputs 建好 4 路通道：打开文件、初始化解码器、设置打时间戳的帧率和人为停顿
 * @param[out] channelPtrs  建好的通道，下标就是第几路
 * @return true = 全部成功；false = 有一路失败（已打印原因）
 * @note 还没起线程。和 openAllChannels 的区别：输入表不同，多了帧率和停顿
 */
static bool openAlignedChannels(std::vector<std::unique_ptr<DecodeChannel>> &channelPtrs) {
    for (int channelIndex = 0; channelIndex < kChannelCount; ++channelIndex) {
        const AlignedChannelInput &channelInput = kM5ChannelInputs[channelIndex];
        channelPtrs.push_back(std::make_unique<DecodeChannel>(channelIndex, kFrameQueueCapacity));
        DecodeChannel &channel = *channelPtrs.back();
        if (!channel.open(channelInput.streamPath, channelInput.codingType)) {
            return false;
        }
        channel.setPtsFrameRate(channelInput.framesPerSecond);
        channel.setDecodeStall(channelInput.stallAtFrameIndex, channelInput.stallDurationMs);
    }
    return true;
}

/**
 * @brief 按主时钟拼图、编码：第 1 张阻塞着拼（等 4 路都有第一帧），然后开始计时，每 1/30 秒拼一张
 * @param[in,out] channelPtrs   所有通道
 * @param[in,out] composeStage  拼图段
 * @param[in,out] encodeStage   编码段
 * @return true = 拼完、编完、排空；false = 出错或被叫停
 * @note 第 k 张大图在时钟走到 k × 1000000 / kM5MosaicFps 微秒时拼。早了就睡（早了等）；
 *       每一格只取到点的帧，过时的丢掉（晚了丢），规则在 MosaicComposeStage::composeAlignedMosaic
 */
static bool runAlignedComposeEncodeLoop(std::vector<std::unique_ptr<DecodeChannel>> &channelPtrs,
                                        MosaicComposeStage &composeStage, EncodeStage &encodeStage) {
    // 第 1 步 第一张大图：每一路都 pop 一帧（阻塞）。这样 4 路解码器初始化的时间不算进时钟，开头不会被判成“晚了”
    ComposeResult composeResult = composeStage.composeNextMosaic(channelPtrs);
    if (composeResult != ComposeResult::Mosaic) {
        abortAllChannels(channelPtrs);
        return false;
    }
    if (!encodeStage.encodePicture(composeStage.getMosaicPicture())) {
        abortAllChannels(channelPtrs);
        return false;
    }

    // 第 2 步 开始计时：第一张大图对应时间 0（4 路第一帧的时间戳都是 0）
    MasterClock clock;
    clock.start();

    // 第 3 步 每一拍：睡到该拼的时间，按时钟拼一张，编码
    for (int64_t mosaicIndex = 1;; ++mosaicIndex) {
        clock.sleepUntilPositionUs(mosaicIndex * 1000000 / kM5MosaicFps);
        composeResult = composeStage.composeAlignedMosaic(channelPtrs, clock.getPositionUs());
        if (composeResult == ComposeResult::AllChannelsEnded) {
            return encodeStage.drainEncoder();
        }
        if (composeResult != ComposeResult::Mosaic) {
            abortAllChannels(channelPtrs);
            return false;
        }
        if (!encodeStage.encodePicture(composeStage.getMosaicPicture())) {
            abortAllChannels(channelPtrs);
            return false;
        }
    }
}

/**
 * @brief 4 路不同帧率 → 按主时钟对齐拼图 → 编码
 * @param[in] outputPath  输出路径
 * @param[in] tag         日志标签
 * @return 0 = 成功；1 = 失败
 * @note 第 1 步 打开 4 路；第 2 步 打开拼图段；第 3 步 打开编码段；第 4 步 起解码线程；
 *       第 5 步 按时钟拼图编码；第 6 步 join；第 7 步 打印每一路 显示 / 重复 / 丢弃
 */
int runAlignedMosaicPipeline(const char *outputPath, const char *tag) {
    // 第 1 步 打开 4 路
    std::vector<std::unique_ptr<DecodeChannel>> channelPtrs;
    if (!openAlignedChannels(channelPtrs)) {
        return 1;
    }

    // 第 2 步 打开拼图段
    MosaicComposeStage composeStage;
    if (!composeStage.open(kMosaicWidth, kMosaicHeight, kMosaicColumnCount, kMosaicRowCount, kChannelCount)) {
        return 1;
    }

    // 第 3 步 打开编码段：和 M3 一样，stride 从大图上取；帧率 30 和拼图节拍一致
    const Nv12Picture &mosaicPicture = composeStage.getMosaicPicture();
    EncoderConfig encoderConfig;
    encoderConfig.width = mosaicPicture.width;
    encoderConfig.height = mosaicPicture.height;
    encoderConfig.horStride = mosaicPicture.horStride;
    encoderConfig.verStride = mosaicPicture.verStride;
    encoderConfig.codingType = MPP_VIDEO_CodingHEVC;
    encoderConfig.rcMode = MPP_ENC_RC_MODE_CBR;
    encoderConfig.bps = kMosaicBps;
    encoderConfig.fps = kM5MosaicFps;
    encoderConfig.gop = kMosaicGop;
    EncodeStage encodeStage;
    if (!encodeStage.open(encoderConfig, outputPath)) {
        return 1;
    }

    // 第 4 步 每路起一个解码线程
    const double startMs = getNowMs();
    std::vector<std::thread> decodeThreads;
    for (std::unique_ptr<DecodeChannel> &channelPtr : channelPtrs) {
        DecodeChannel &channel = *channelPtr;
        decodeThreads.emplace_back([&channel] { channel.runDecodeLoop(); });
    }

    // 第 5 步 按时钟拼图编码
    const bool isComposeEncodeOk = runAlignedComposeEncodeLoop(channelPtrs, composeStage, encodeStage);

    // 第 6 步 等解码线程结束
    for (std::thread &decodeThread : decodeThreads) {
        decodeThread.join();
    }
    const double elapsedMs = getNowMs() - startMs;

    // 第 7 步 打印每一路的结果
    bool isChannelsOk = true;
    for (const std::unique_ptr<DecodeChannel> &channelPtr : channelPtrs) {
        const int channelIndex = channelPtr->getChannelIndex();
        const DecodeStage &decodeStage = channelPtr->getDecodeStage();
        LOGI(tag, "ch%d %d fps：解出 %d 帧（错误 %d），显示 %d，重复 %d，丢弃 %d", channelIndex,
             kM5ChannelInputs[channelIndex].framesPerSecond, decodeStage.getDecodedFrameCount(),
             decodeStage.getErrorFrameCount(), composeStage.getDrawnFrameCount(channelIndex),
             composeStage.getRepeatedFrameCount(channelIndex), composeStage.getDroppedFrameCount(channelIndex));
        if (!channelPtr->isDecodeLoopOk() || decodeStage.getErrorFrameCount() > 0) {
            isChannelsOk = false;
        }
    }
    LOGI(tag, "拼了 %d 张大图，编码 %d 帧，用时 %.0f ms（实时速度，预期约 60000 ms）", composeStage.getComposedMosaicCount(),
         encodeStage.getEncodedFrameCount(), elapsedMs);
    LOGI(tag, "输出 %lld 字节（%s）", static_cast<long long>(encodeStage.getOutputBytes()), outputPath);
    if (!isComposeEncodeOk || !isChannelsOk) {
        LOGE(tag, "失败：拼图编码%s，通道%s", isComposeEncodeOk ? "正常" : "出错", isChannelsOk ? "正常" : "不正常");
        return 1;
    }
    return 0;
}

/**
 * @brief M5：输出到文件
 * @return 0 = 成功；1 = 失败
 * @note 无
 */
int runM5AlignedMosaic() {
    return runAlignedMosaicPipeline(kM5OutputPath, "M5");
}

/**
 * @brief M5 上屏：输出到 FIFO
 * @return 0 = 成功；1 = 失败
 * @note 必须先有 gst 打开 FIFO 的读端，用 run_m4_on_board_with_mac.sh m5screen 运行
 */
int runM5AlignedMosaicOnScreen() {
    return runAlignedMosaicPipeline(kM4FifoPath, "M5screen");
}
