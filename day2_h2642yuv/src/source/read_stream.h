/*
 * ReadStream : StreamSource —— 从码流文件里按固定长度（默认 64KB）一块一块读
 *
 * 用法：
 *   ReadStream reader;
 *   if (!reader.open("/userdata/av/aaa.264")) return -1;
 *   std::vector<uint8_t> chunk;
 *   bool isLast = false;
 *   while (!isLast) { reader.read(chunk, isLast); ...送给解码器... }
 *   // 不用手动 fclose：reader 析构时自动关闭
 */
#pragma once

#include "common/mpp_utils.h" // FilePtr
#include "source/stream_source.h"

#include <cstddef> // size_t

class ReadStream : public StreamSource {
public:
    /**
     * 打开码流文件
     * @param path       文件路径
     * @param chunkSize  每次读多少字节
     * @return  成功返回 true；打开失败打印原因，返回 false
     */
    bool open(const char* path, size_t chunkSize = 64 * 1024);

    /**
     * fread 一块。读不满 chunkSize 就是文件读完了（isLast = true）
     */
    bool read(std::vector<uint8_t>& data, bool& isLast) override;

private:
    FilePtr file_;          // RAII：析构时 fclose
    size_t chunkSize_ = 0;
};
