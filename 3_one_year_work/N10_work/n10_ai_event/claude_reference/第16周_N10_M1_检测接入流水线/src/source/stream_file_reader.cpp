// 来源：3_one_year_work/N0_work/n0_nvr_mosaic/claude_reference/第02周_N0_Day6的M4和Day8上板/src/source/stream_file_reader.cpp
#include "source/stream_file_reader.h"

#include <cerrno>  // errno
#include <cstring> // strerror

#include "common/log.h"

static const char *const kTag = "StreamReader";

/**
 * @brief 打开码流文件
 * @param[in] filePath                 文件路径
 * @param[in] requestedChunkSizeBytes  每次读多少字节
 * @return true = 成功；false = 失败
 * @note 参数叫 requestedChunkSizeBytes 而不是 chunkSizeBytes：和成员同名的话，
 *       chunkSizeBytes = chunkSizeBytes; 是参数自己赋给自己，成员一直是 0
 */
bool StreamFileReader::open(const char *filePath, size_t requestedChunkSizeBytes) {
    filePtr.reset(fopen(filePath, "rb"));
    if (!filePtr) {
        LOGE(kTag, "打开 %s 失败：%s", filePath, strerror(errno));
        return false;
    }
    chunkSizeBytes = requestedChunkSizeBytes;
    return true;
}

/**
 * @brief 读下一块
 * @param[out] chunk        读到的数据
 * @param[out] isLastChunk  是不是最后一块
 * @return true = 成功；false = 出错
 * @note fread 返回实际读到的字节数。比要的少有两种可能：读到文件末尾（正常），或者出错（ferror 为真）
 */
bool StreamFileReader::readChunk(std::vector<uint8_t> &chunk, bool &isLastChunk) {
    chunk.resize(chunkSizeBytes); // 先把 vector 撑到 chunkSizeBytes，fread 才有地方写
    const size_t readSizeBytes = fread(chunk.data(), 1, chunkSizeBytes, filePtr.get());
    if (readSizeBytes < chunkSizeBytes && ferror(filePtr.get())) {
        LOGE(kTag, "fread 出错：%s", strerror(errno));
        return false;
    }
    chunk.resize(readSizeBytes); // 缩回实际读到的大小，后面用 chunk.size() 就是有效字节数
    isLastChunk = readSizeBytes < chunkSizeBytes;
    readBytes += static_cast<int64_t>(readSizeBytes);
    return true;
}
