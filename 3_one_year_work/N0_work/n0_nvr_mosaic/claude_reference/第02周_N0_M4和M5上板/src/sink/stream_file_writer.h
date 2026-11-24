#pragma once

#include <cstdint> // uint8_t、int64_t
#include <vector>

#include "common/mpp_utils.h" // FilePtr

/** 把编码出来的码流一段一段追加写进文件 */
class StreamFileWriter {
public:
    /**
     * @brief 创建（或清空）输出文件
     * @param[in] filePath  文件路径，例如 "/userdata/av/mosaic.h265"
     * @return true = 成功；false = 失败（已打印原因）
     * @note filePath 是命名管道（FIFO）时，fopen 会一直等，直到另一头有人打开它来读
     */
    bool open(const char *filePath);

    /**
     * @brief 追加写一段数据
     * @param[in] data  要写的数据；空的就什么都不做
     * @return true = 写完了；false = 没写完（已打印原因）
     * @note 调用前必须 open 成功
     */
    bool writeBytes(const std::vector<uint8_t> &data);

    /**
     * @brief 一共写了多少字节
     * @return 字节数
     * @note 用来验收：和 ls -l 看到的文件大小比
     */
    int64_t getWrittenBytes() const { return writtenBytes; }

private:
    FilePtr filePtr;          // 输出文件，析构时自动 fclose
    int64_t writtenBytes = 0; // 一共写了多少字节
};
