//
// Created by DEV on 2026/11/9.
//

#include "stream_file_reader.h"
#include "cerrno"
#include "common/log.h"
#include "cstring"
static const char *const kTag = "StreamReader";

bool StreamFileReader::open(const char *filePath, size_t reqChunkSizeBytes) {
    filePtr.reset(fopen(filePath, "rb"));
    if (!filePtr) {
        LOGE(kTag, "打开 %s 失败：%s", filePath, strerror(errno));
        return false;
    }
    this->chunkSizeBytes = reqChunkSizeBytes;
    return true;
}

bool StreamFileReader::readChunk(std::vector<uint8_t> &chunk, bool &isLastChunk) {
    chunk.resize(chunkSizeBytes);
    // fread(目标, 每个元素的字节数, 元素个数, 文件)
    const size_t readSizeBytes = fread(chunk.data(), 1, chunkSizeBytes, filePtr.get());
    if (readSizeBytes<chunkSizeBytes && ferror(filePtr.get())) {
        LOGE(kTag, "fread 出错：%s", strerror(errno));
        return false;
    }
    // fread 绕过了 vector，直接往它的内存里写，所以 size 不会变，因此需要调用：resize()
    // Note :所以 chunk.resize(readSizeBytes) 的作用相当于 Java 的 ByteBuffer.limit(n)：告诉后面用这块数据的人，有效的只有前 readSizeBytes 个字节。
    chunk.resize(readSizeBytes);
    isLastChunk = readSizeBytes<chunkSizeBytes;
    readBytes+=static_cast<int64_t>(readSizeBytes);
    return true;
}
