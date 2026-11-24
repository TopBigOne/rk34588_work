#include "app/m3_encode_mosaic.h"

#include <memory> // std::unique_ptr
#include <thread>
#include <vector>

#include "app/app_config.h"
#include "app/channel_helpers.h"
#include "common/log.h"
#include "common/time_utils.h"
#include "pipeline/decode_channel.h"
#include "pipeline/encode_stage.h"
#include "pipeline/mosaic_compose_stage.h"

/**
 * @brief 拼图编码循环：拼一张大图、编一张，直到所有通道都解完，再排空编码器
 * @param[in,out] channelPtrs   所有通道
 * @param[in,out] composeStage  拼图段
 * @param[in,out] encodeStage   编码段
 * @return true = 全部拼完、编完、排空；false = 出错或被叫停
 * @note 在主线程里跑。大图只有一块 buffer：编码返回时编码器已经读完，下一圈才能在上面拼下一张
 */
static bool runComposeEncodeLoop(std::vector<std::unique_ptr<DecodeChannel>> &channelPtrs,
                                 MosaicComposeStage &composeStage, EncodeStage &encodeStage) {
    while (true) {
        const ComposeResult composeResult = composeStage.composeNextMosaic(channelPtrs);
        if (composeResult == ComposeResult::AllChannelsEnded) {
            return encodeStage.drainEncoder(); // 所有通道都解完了：EOS 传到编码器
        }
        if (composeResult != ComposeResult::Mosaic) {
            abortAllChannels(channelPtrs);
            return false;
        }
        if (!encodeStage.encodePicture(composeStage.getMosaicPicture())) {
            abortAllChannels(channelPtrs); // 编码出错：叫停所有解码线程
            return false;
        }
    }
}

/**
 * @brief 4 路解码 → 拼图 → 编码
 * @param[in] outputPath  输出路径
 * @param[in] tag         日志标签
 * @return 0 = 成功；1 = 失败
 * @note 第 1 步 打开 4 路通道；第 2 步 打开拼图段；第 3 步 按大图的尺寸和 stride 打开编码段；
 *       第 4 步 每路起一个解码线程；第 5 步 主线程拼图、编码；第 6 步 等解码线程结束；第 7 步 打印结果
 */
int runMosaicEncodePipeline(const char *outputPath, const char *tag) {
    // 第 1 步 打开 4 路通道
    std::vector<std::unique_ptr<DecodeChannel>> channelPtrs;
    if (!openAllChannels(channelPtrs)) {
        return 1;
    }

    // 第 2 步 打开拼图段
    MosaicComposeStage composeStage;
    if (!composeStage.open(kMosaicWidth, kMosaicHeight, kMosaicColumnCount, kMosaicRowCount, kChannelCount)) {
        return 1;
    }

    // 第 3 步 打开编码段。编码器的 stride 必须和大图 buffer 的实际排布一样（1920 × 1088），直接从大图上取
    const Nv12Picture &mosaicPicture = composeStage.getMosaicPicture();
    EncoderConfig encoderConfig;
    encoderConfig.width = mosaicPicture.width;
    encoderConfig.height = mosaicPicture.height;
    encoderConfig.horStride = mosaicPicture.horStride;
    encoderConfig.verStride = mosaicPicture.verStride;
    encoderConfig.codingType = MPP_VIDEO_CodingHEVC;
    encoderConfig.rcMode = MPP_ENC_RC_MODE_CBR;
    encoderConfig.bps = kMosaicBps;
    encoderConfig.fps = kMosaicFps;
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

    // 第 5 步 主线程拼图、编码
    const bool isComposeEncodeOk = runComposeEncodeLoop(channelPtrs, composeStage, encodeStage);

    // 第 6 步 等解码线程结束
    for (std::thread &decodeThread : decodeThreads) {
        decodeThread.join();
    }
    const double elapsedMs = getNowMs() - startMs;

    // 第 7 步 打印结果
    const bool isChannelsOk = printChannelResults(channelPtrs, composeStage, tag);
    const int encodedFrameCount = encodeStage.getEncodedFrameCount();
    LOGI(tag, "拼了 %d 张大图，编码 %d 帧，用时 %.0f ms，%.1f fps", composeStage.getComposedMosaicCount(),
         encodedFrameCount, elapsedMs, elapsedMs > 0 ? encodedFrameCount * 1000.0 / elapsedMs : 0.0);
    LOGI(tag, "输出 %lld 字节（%s）", static_cast<long long>(encodeStage.getOutputBytes()), outputPath);
    if (!isComposeEncodeOk || !isChannelsOk) {
        LOGE(tag, "失败：拼图编码%s，通道%s", isComposeEncodeOk ? "正常" : "出错", isChannelsOk ? "正常" : "不正常");
        return 1;
    }
    return 0;
}

/**
 * @brief M3：大图编码成文件
 * @return 0 = 成功；1 = 失败
 * @note 输出路径在 app_config.h 的 kM3OutputPath
 */
int runM3EncodeMosaic() {
    return runMosaicEncodePipeline(kM3OutputPath, "M3");
}
