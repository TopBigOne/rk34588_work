#include "app/m2_compose_mosaic.h"

#include <memory> // std::unique_ptr
#include <thread>
#include <vector>

#include "app/app_config.h"
#include "app/channel_helpers.h"
#include "common/log.h"
#include "common/time_utils.h"
#include "pipeline/decode_channel.h"
#include "pipeline/mosaic_compose_stage.h"
#include "sink/nv12_file_writer.h"

static const char *const kTag = "M2";

/**
 * @brief 这张大图要不要存下来
 * @param[in] mosaicIndex  第几张大图，从 0 开始
 * @return true = 在 kM2SnapshotMosaicIndexes 里，要存
 * @note 范围 for 可以直接遍历 C 数组
 */
static bool isSnapshotMosaic(int mosaicIndex) {
    for (const int snapshotIndex : kM2SnapshotMosaicIndexes) {
        if (snapshotIndex == mosaicIndex) {
            return true;
        }
    }
    return false;
}

/**
 * @brief 拼图循环：一张一张拼大图，遇到要存的就写进文件，直到所有通道都解完
 * @param[in,out] channelPtrs     所有通道
 * @param[in,out] composeStage    拼图段
 * @param[in,out] snapshotWriter  存大图的文件
 * @return true = 拼到所有通道都解完；false = 出错或被叫停
 * @note 在调用它的线程（主线程）里跑。出错时叫停所有通道，让解码线程都退出
 */
static bool runComposeLoop(std::vector<std::unique_ptr<DecodeChannel>> &channelPtrs,
                           MosaicComposeStage &composeStage, Nv12FileWriter &snapshotWriter) {
    while (true) {
        const ComposeResult composeResult = composeStage.composeNextMosaic(channelPtrs);
        if (composeResult == ComposeResult::AllChannelsEnded) {
            return true;
        }
        if (composeResult != ComposeResult::Mosaic) {
            abortAllChannels(channelPtrs); // Aborted：有一路出错了；Error：RGA 出错。都要叫停其他通道
            return false;
        }

        const int mosaicIndex = composeStage.getComposedMosaicCount() - 1;
        if (isSnapshotMosaic(mosaicIndex)) {
            if (!snapshotWriter.writePicture(composeStage.getMosaicPicture())) {
                abortAllChannels(channelPtrs);
                return false;
            }
            LOGI(kTag, "存下第 %d 张大图", mosaicIndex);
        }
    }
}

/**
 * @brief M2：4 路解码 + RGA 拼成 2×2 大图
 * @return 0 = 成功；1 = 失败
 * @note 第 1 步 打开 4 路通道；第 2 步 打开拼图段和抓图文件；第 3 步 每路起一个解码线程；
 *       第 4 步 主线程拼图；第 5 步 等解码线程结束；第 6 步 打印结果
 */
int runM2ComposeMosaic() {
    // 第 1 步 打开 4 路通道
    std::vector<std::unique_ptr<DecodeChannel>> channelPtrs;
    if (!openAllChannels(channelPtrs)) {
        return 1;
    }

    // 第 2 步 打开拼图段和抓图文件
    MosaicComposeStage composeStage;
    if (!composeStage.open(kMosaicWidth, kMosaicHeight, kMosaicColumnCount, kMosaicRowCount, kChannelCount)) {
        return 1;
    }
    Nv12FileWriter snapshotWriter;
    if (!snapshotWriter.open(kM2SnapshotPath)) {
        return 1;
    }

    // 第 3 步 每路起一个解码线程。[&channel]：按引用捕获这一路的 DecodeChannel，线程里调用它的 runDecodeLoop
    const double startMs = getNowMs();
    std::vector<std::thread> decodeThreads;
    for (std::unique_ptr<DecodeChannel> &channelPtr : channelPtrs) {
        DecodeChannel &channel = *channelPtr;
        decodeThreads.emplace_back([&channel] { channel.runDecodeLoop(); });
    }

    // 第 4 步 主线程拼图（不另起线程：主线程本来也要等着，正好用它来拼）
    const bool isComposeOk = runComposeLoop(channelPtrs, composeStage, snapshotWriter);

    // 第 5 步 等解码线程结束。拼图出错时已经 abort 了所有队列，解码线程很快会退出
    for (std::thread &decodeThread : decodeThreads) {
        decodeThread.join();
    }
    const double elapsedMs = getNowMs() - startMs;

    // 第 6 步 打印结果
    const bool isChannelsOk = printChannelResults(channelPtrs, composeStage, kTag);
    const int composedMosaicCount = composeStage.getComposedMosaicCount();
    LOGI(kTag, "拼了 %d 张大图，用时 %.0f ms，%.1f 张/秒", composedMosaicCount, elapsedMs,
         elapsedMs > 0 ? composedMosaicCount * 1000.0 / elapsedMs : 0.0);
    LOGI(kTag, "存了 %d 张大图到 %s", snapshotWriter.getWrittenFrameCount(), kM2SnapshotPath);
    if (!isComposeOk || !isChannelsOk) {
        LOGE(kTag, "失败：拼图%s，通道%s", isComposeOk ? "正常" : "出错", isChannelsOk ? "正常" : "不正常");
        return 1;
    }
    return 0;
}
