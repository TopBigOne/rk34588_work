#include "pipeline/mosaic_compose_stage.h"

#include "common/log.h"
#include "rga/rga_compositor.h"

#include <utility> // std::move

static const char *const kTag = "MosaicCompose";

/**
 * @brief 申请大图、涂黑
 * @param[in] mosaicWidth   大图宽
 * @param[in] mosaicHeight  大图高
 * @param[in] requestedColumnCount  列数
 * @param[in] requestedRowCount     行数
 * @param[in] channelCount          路数
 * @return true = 成功；false = 失败
 * @note 第 1 步 检查参数；第 2 步 申请 buffer；第 3 步 整张涂黑
 */
bool MosaicComposeStage::open(int mosaicWidth, int mosaicHeight, int requestedColumnCount, int requestedRowCount,
                              int channelCount) {
    // 第 1 步 检查参数
    if (mosaicBufferPtr) {
        LOGE(kTag, "open 只能调用一次");
        return false;
    }
    if (channelCount > requestedColumnCount * requestedRowCount) {
        LOGE(kTag, "%d 路放不进 %d×%d 个格子", channelCount, requestedColumnCount, requestedRowCount);
        return false;
    }
    columnCount = requestedColumnCount;
    rowCount = requestedRowCount;
    isChannelEnded.assign(channelCount, false);
    drawnFrameCounts.assign(channelCount, 0);
    repeatedFrameCounts.assign(channelCount, 0);
    droppedFrameCounts.assign(channelCount, 0);

    // 第 2 步 申请 buffer：DRM 类型的组，RGA 能通过 dma-buf fd 写它，编码器也能读它
    MppBufferGroup rawGroup = nullptr;
    MPP_CHECK(mpp_buffer_group_get_internal(&rawGroup, MPP_BUFFER_TYPE_DRM), false);
    mosaicGroupPtr.reset(rawGroup);
    mosaicPicture = makeAlignedNv12Picture(mosaicWidth, mosaicHeight);
    mosaicBufferPtr = allocNv12Buffer(mosaicGroupPtr.get(), mosaicPicture);
    if (!mosaicBufferPtr) {
        return false;
    }

    // 第 3 步 整张涂黑：新申请的 buffer 里是什么没有保证。没有画面的格子（比如只接了 3 路）就是黑的
    if (!fillNv12PictureBlack(mosaicPicture)) {
        return false;
    }
    LOGI(kTag, "大图 %dx%d，stride %dx%d，%d 列 × %d 行，每格 %dx%d", mosaicPicture.width, mosaicPicture.height,
         mosaicPicture.horStride, mosaicPicture.verStride, columnCount, rowCount, getCellRect(0).width,
         getCellRect(0).height);
    return true;
}

/**
 * @brief 拼下一张大图
 * @param[in,out] channelPtrs  所有通道
 * @return Mosaic / AllChannelsEnded / Aborted / Error
 * @note 每一路：第 1 步 pop 一帧；第 2 步 把帧描述成 Nv12Picture；第 3 步 RGA 缩放贴进格子；第 4 步 帧释放，buffer 还给这一路的解码器
 */
ComposeResult MosaicComposeStage::composeNextMosaic(std::vector<std::unique_ptr<DecodeChannel>> &channelPtrs) {
    bool hasDrawnAnyCell = false;
    for (std::unique_ptr<DecodeChannel> &channelPtr : channelPtrs) {
        const int channelIndex = channelPtr->getChannelIndex();
        if (isChannelEnded[channelIndex]) {
            continue; // 这一路解完了，格子停在最后一帧
        }

        // 第 1 步 pop 一帧。这一路的帧还没解出来，就在这里等
        MppFramePtr framePtr;
        const PopResult popResult = channelPtr->getFrameQueue().pop(framePtr);
        if (popResult == PopResult::Aborted) {
            return ComposeResult::Aborted;
        }
        if (popResult == PopResult::EndOfStream) {
            isChannelEnded[channelIndex] = true;
            LOGI(kTag, "ch%d 解完了（贴了 %d 帧），格子停在最后一帧", channelIndex, drawnFrameCounts[channelIndex]);
            continue;
        }

        // 第 2 步 描述成 Nv12Picture：宽高、stride 用这一帧自己的，每一路可能都不一样
        Nv12Picture decodedPicture;
        if (!describeDecodedFrame(framePtr.get(), decodedPicture)) {
            return ComposeResult::Error;
        }

        // 第 3 步 RGA 缩放贴进格子
        if (!resizeNv12IntoRectByRga(decodedPicture, mosaicPicture, getCellRect(channelIndex))) {
            return ComposeResult::Error;
        }
        drawnFrameCounts[channelIndex]++;
        hasDrawnAnyCell = true;
        // 第 4 步 framePtr 在这一圈结束时析构：RGA 已经读完（同步调用），buffer 马上回到这一路解码器的内存池
    }

    if (!hasDrawnAnyCell) {
        return ComposeResult::AllChannelsEnded; // 这一圈每一路都是"已经解完"，没有新画面
    }
    composedMosaicCount++;
    return ComposeResult::Mosaic;
}

/**
 * @brief 按主时钟拼一张大图
 * @param[in,out] channelPtrs  所有通道
 * @param[in]     nowUs        主时钟现在的位置，单位微秒
 * @return Mosaic / AllChannelsEnded / Aborted / Error
 * @note 每一路：第 1 步 把到点的帧全部取出，只留最新的；第 2 步 有新帧就贴进格子，没有就算重复
 */
ComposeResult MosaicComposeStage::composeAlignedMosaic(std::vector<std::unique_ptr<DecodeChannel>> &channelPtrs,
                                                       int64_t nowUs) {
    bool hasLiveChannel = false; // 这一拍还有没结束的通道
    for (std::unique_ptr<DecodeChannel> &channelPtr : channelPtrs) {
        const int channelIndex = channelPtr->getChannelIndex();
        if (isChannelEnded[channelIndex]) {
            continue; // 解完了：格子停在最后一帧，不再计数
        }

        // 第 1 步 到点的帧全部取出。取到第二帧时，第一帧已经“过时”了：它没来得及显示，算丢弃
        MppFramePtr latestDueFramePtr; // 这一拍要贴的帧：到点的帧里最新的那一帧
        while (true) {
            MppFramePtr dueFramePtr;
            const TryPopResult tryPopResult = channelPtr->getFrameQueue().tryPopIfDue(nowUs, dueFramePtr);
            if (tryPopResult == TryPopResult::Aborted) {
                return ComposeResult::Aborted;
            }
            if (tryPopResult == TryPopResult::EndOfStream) {
                isChannelEnded[channelIndex] = true;
                LOGI(kTag, "ch%d 解完了（显示 %d、重复 %d、丢弃 %d），格子停在最后一帧", channelIndex,
                     drawnFrameCounts[channelIndex], repeatedFrameCounts[channelIndex],
                     droppedFrameCounts[channelIndex]);
                break;
            }
            if (tryPopResult != TryPopResult::Frame) {
                break; // NotDue / Empty：后面的帧还没到点，或者还没解出来
            }
            if (latestDueFramePtr) {
                droppedFrameCounts[channelIndex]++; // 被更新的帧顶掉；赋值时旧帧析构，buffer 还给解码器
            }
            latestDueFramePtr = std::move(dueFramePtr);
        }

        // 第 2 步 有新帧就贴；没有新帧、这一路也没结束，格子保持上一帧
        if (latestDueFramePtr) {
            Nv12Picture decodedPicture;
            if (!describeDecodedFrame(latestDueFramePtr.get(), decodedPicture)) {
                return ComposeResult::Error;
            }
            if (!resizeNv12IntoRectByRga(decodedPicture, mosaicPicture, getCellRect(channelIndex))) {
                return ComposeResult::Error;
            }
            drawnFrameCounts[channelIndex]++;
        } else if (!isChannelEnded[channelIndex]) {
            repeatedFrameCounts[channelIndex]++;
        }
        if (!isChannelEnded[channelIndex] || latestDueFramePtr) {
            hasLiveChannel = true;
        }
    }

    if (!hasLiveChannel) {
        return ComposeResult::AllChannelsEnded;
    }
    composedMosaicCount++;
    return ComposeResult::Mosaic;
}

/**
 * @brief 算格子位置
 * @param[in] cellIndex  格子编号
 * @return 格子的矩形
 * @note & ~1：把最低位清零，变成偶数。RGA 要求 NV12 的矩形 x、y、宽、高都是偶数
 */
PictureRect MosaicComposeStage::getCellRect(int cellIndex) const {
    const int cellWidth = (mosaicPicture.width / columnCount) & ~1;
    const int cellHeight = (mosaicPicture.height / rowCount) & ~1;
    const int columnIndex = cellIndex % columnCount; // 第几列
    const int rowIndex = cellIndex / columnCount;    // 第几行
    return {columnIndex * cellWidth, rowIndex * cellHeight, cellWidth, cellHeight};
}
