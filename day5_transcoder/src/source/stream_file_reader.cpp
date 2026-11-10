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
    // todo 这里调用resize 解决了什么问题？
    chunk.resize(readSizeBytes);
    isLastChunk = readSizeBytes<chunkSizeBytes;
    readBytes+=static_cast<int64_t>(readSizeBytes);
    return true;
}
