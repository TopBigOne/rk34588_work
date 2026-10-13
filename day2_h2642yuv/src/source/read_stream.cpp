#include "source/read_stream.h"

#include <cstdio> // fopen / fread / ferror / printf

bool ReadStream::open(const char* path, size_t chunkSize) {
    file_.reset(fopen(path, "rb"));
    if (!file_) {
        printf("open %s failed\n", path);
        return false;
    }
    chunkSize_ = chunkSize;
    return true;
}

bool ReadStream::read(std::vector<uint8_t>& data, bool& isLast) {
    data.resize(chunkSize_);
    const size_t readSize = fread(data.data(), 1, chunkSize_, file_.get());
    if (ferror(file_.get())) {
        perror("    fread");
        return false;
    }
    data.resize(readSize); // 最后一块可能读不满，size() 就是真正读到的字节数
    isLast = readSize < chunkSize_; // 读不满，就是文件读完了
    return true;
}
