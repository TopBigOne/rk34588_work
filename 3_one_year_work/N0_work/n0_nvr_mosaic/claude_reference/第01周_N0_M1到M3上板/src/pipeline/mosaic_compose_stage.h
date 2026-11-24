#pragma once

#include <memory> // std::unique_ptr
#include <vector>

#include "common/mpp_utils.h"
#include "common/nv12_picture.h"
#include "pipeline/decode_channel.h"

/** composeNextMosaic() 的结果 */
enum class ComposeResult {
    Mosaic,           // 拼好了一张大图
    AllChannelsEnded, // 所有通道都解完了，不会再有新的大图
    Aborted,          // 有一路出错叫停了整个流程
    Error,            // 拼图出错（RGA 失败，已打印原因）
};

/**
 * @brief 拼图段：从每一路的队列里各取一帧，用 RGA 缩放贴进大图里自己的格子
 * @note 大图只有一张（一块 buffer），每次 composeNextMosaic 都在它上面改。
 *       某一路解完了，它的格子不再更新，停在最后一帧
 */
class MosaicComposeStage {
public:
    /**
     * @brief 申请大图的 buffer，涂黑，算好每个格子的位置
     * @param[in] mosaicWidth   大图宽，单位像素，例如 1920
     * @param[in] mosaicHeight  大图高，单位像素，例如 1080
     * @param[in] requestedColumnCount  切成几列
     * @param[in] requestedRowCount     切成几行
     * @param[in] channelCount          一共几路；必须 ≤ 列数 × 行数
     * @return true = 成功；false = 失败（已打印原因）
     * @note 只能调用一次
     */
    bool open(int mosaicWidth, int mosaicHeight, int requestedColumnCount, int requestedRowCount, int channelCount);

    /**
     * @brief 拼下一张大图：每一路还没结束的通道各 pop 一帧，贴进它的格子
     * @param[in,out] channelPtrs  所有通道，下标就是格子编号；个数必须等于 open 时的 channelCount
     * @return Mosaic = 拼好一张；AllChannelsEnded = 都解完了；Aborted = 被叫停；Error = RGA 出错
     * @note 会阻塞：哪一路的帧还没解出来，就在它的 pop 里等。所以大图的速度跟着最慢的那一路走
     */
    ComposeResult composeNextMosaic(std::vector<std::unique_ptr<DecodeChannel>> &channelPtrs);

    /**
     * @brief 当前的大图
     * @return 大图的描述；buffer 归这个对象管
     * @note composeNextMosaic 返回 Mosaic 之后读；下一次 composeNextMosaic 会改它的内容
     */
    const Nv12Picture &getMosaicPicture() const { return mosaicPicture; }

    /**
     * @brief 一共拼了几张大图
     * @return 张数；4 路都解完时，等于帧数最多的那一路的帧数
     * @note 用来验收
     */
    int getComposedMosaicCount() const { return composedMosaicCount; }

    /**
     * @brief 某一路一共贴了几帧
     * @param[in] channelIndex  第几路
     * @return 帧数；正常结束时等于这一路解出来的帧数
     * @note 用来验收：每一帧都贴上了，一帧没丢
     */
    int getDrawnFrameCount(int channelIndex) const { return drawnFrameCounts[channelIndex]; }

private:
    /**
     * @brief 算第 cellIndex 个格子在大图里的位置：按行排，从左到右、从上到下
     * @param[in] cellIndex  格子编号，从 0 开始
     * @return 格子的矩形；x、y、宽、高都是偶数
     * @note 2 列 × 2 行、1920×1080 时：0 号 (0,0)，1 号 (960,0)，2 号 (0,540)，3 号 (960,540)，每格 960×540
     */
    PictureRect getCellRect(int cellIndex) const;

    MppBufferGroupPtr mosaicGroupPtr; // 大图 buffer 从这个组申请。必须声明在 mosaicBufferPtr 前面：
                                      // 成员按声明的反顺序析构，buffer 先还回去，组再释放
    MppBufferPtr mosaicBufferPtr;     // 大图的 buffer，只有一块
    Nv12Picture mosaicPicture;        // 大图的描述：1920×1080，stride 1920×1088
    int columnCount = 0;              // 几列
    int rowCount = 0;                 // 几行
    std::vector<bool> isChannelEnded; // 每一路是不是已经解完；解完的不再 pop
    std::vector<int> drawnFrameCounts; // 每一路贴了几帧
    int composedMosaicCount = 0;      // 拼了几张大图
};
