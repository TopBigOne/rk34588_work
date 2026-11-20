#pragma once

#include <cstddef> // size_t
#include <string>

#include "pipeline/decode_stage.h"
#include "pipeline/frame_queue.h"

/**
 * @brief 一路通道：一个解码段 + 一个帧队列。解码线程往队列里放帧，拼图线程从队列里取帧
 * @note 不能复制、不能移动（里面有 MppDecoder 和 mutex），多路时用 std::unique_ptr<DecodeChannel> 放进 vector
 */
class DecodeChannel {
public:
    /**
     * @brief 构造一路通道（还没打开文件）
     * @param[in] requestedChannelIndex             第几路，从 0 开始；也是它在大图里的格子编号
     * @param[in] requestedQueueCapacityFrameCount  帧队列最多排几帧
     * @return 无（构造函数）
     * @note 帧队列在这里就建好了，容量之后不能改
     */
    DecodeChannel(int requestedChannelIndex, size_t requestedQueueCapacityFrameCount);

    /**
     * @brief 打开码流文件，初始化这一路的解码器
     * @param[in] streamPath  码流文件路径
     * @param[in] codingType  码流格式
     * @return true = 成功；false = 失败（已打印原因）
     * @note 在主线程里调用，起线程之前
     */
    bool open(const char *streamPath, MppCodingType codingType);

    /**
     * @brief 解码线程的函数体：一帧一帧解，放进队列；解完了告诉队列"没有了"
     * @return 无（结果用 isDecodeLoopOk 查）
     * @note 在这一路自己的线程里调用。出错时 abort 自己的队列，把等在 pop 里的拼图线程叫醒；
     *       被拼图线程叫停（push 返回 false）时直接退出
     */
    void runDecodeLoop();

    /**
     * @brief 这一路的帧队列
     * @return 队列的引用
     * @note 拼图线程用它 pop；出错时任何线程都可以对它 abort
     */
    FrameQueue &getFrameQueue() { return frameQueue; }

    /**
     * @brief 这一路的解码段，用来读帧数、错误帧数、读入字节数
     * @return 解码段的常量引用
     * @note 解码线程还在跑的时候不要读，等 join 之后再读
     */
    const DecodeStage &getDecodeStage() const { return decodeStage; }

    /**
     * @brief 第几路
     * @return 从 0 开始的编号
     * @note 也是它在大图里的格子编号
     */
    int getChannelIndex() const { return channelIndex; }

    /**
     * @brief 解码线程是不是正常结束的
     * @return true = 解到 EOS 并 closeInput；false = 出错或被叫停
     * @note 等 join 之后再读
     */
    bool isDecodeLoopOk() const { return isDecodeOk; }

private:
    const int channelIndex; // 第几路
    std::string logName;    // 日志名字，例如 "ch0"
    DecodeStage decodeStage; // 读文件 + 解码
    FrameQueue frameQueue;  // 解码好的帧排在这里，等拼图线程来取
    bool isDecodeOk = false; // 解码线程的结果：只有解码线程写，join 之后主线程读
};
