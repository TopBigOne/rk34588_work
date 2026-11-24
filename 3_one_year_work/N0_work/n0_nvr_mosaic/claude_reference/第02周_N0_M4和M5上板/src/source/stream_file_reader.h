#pragma once

#include <cstddef> // size_t
#include <cstdint> // uint8_t、int64_t
#include <vector>

#include "common/mpp_utils.h" // FilePtr

/** 把码流文件按固定大小一块一块读出来，交给解码器 */
class StreamFileReader {
public:
    /**
     * @brief 打开码流文件
     * @param[in] filePath                 文件路径，例如 "/userdata/av/aaa.264"
     * @param[in] requestedChunkSizeBytes  每次读多少字节，默认 64 KB
     * @return true = 打开成功；false = 打开失败（已打印原因）
     * @note 一块不一定是一帧：解码器开了内部分帧（split_parse），自己会从码流里找帧的边界
     */
    bool open(const char *filePath, size_t requestedChunkSizeBytes = 64 * 1024);

    /**
     * @brief 读下一块
     * @param[out] chunk        读到的数据；最后一块可能比 chunkSizeBytes 小，甚至是 0 字节
     * @param[out] isLastChunk  true = 文件读完了，这是最后一块
     * @return true = 读成功（包括最后一块）；false = 读出错（已打印原因）
     * @note 调用前必须 open 成功
     */
    bool readChunk(std::vector<uint8_t> &chunk, bool &isLastChunk);

    /**
     * @brief 一共读了多少字节
     * @return 字节数；文件读完时等于文件大小
     * @note 用来验收：和 ls -l 看到的文件大小比
     */
    int64_t getReadBytes() const { return readBytes; }

private:
    FilePtr filePtr;           // 打开的文件，析构时自动 fclose
    size_t chunkSizeBytes = 0; // 每次读多少字节
    int64_t readBytes = 0;     // 一共读了多少字节
};
