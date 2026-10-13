// 【扫一眼】fread 一块，M3 里自己写过
#include "source/read_stream.h"

#include <cstdio> // fopen / fread / ferror / printf

/**
 * @brief 打开码流文件
 * @param path       文件路径
 * @param chunkSize  每次读多少字节
 * @return 成功返回 true；打开失败打印原因，返回 false
 * @note 【扫一眼】
 */
bool ReadStream::open(const char* path, size_t chunkSize) {
    file_.reset(fopen(path, "rb"));
    if (!file_) {
        printf("open %s failed\n", path);
        return false;
    }
    chunkSize_ = chunkSize;
    return true;
}

/**
 * @brief fread 一块；读不满 chunkSize 就是文件读完了
 * @param data    输出参数：这一块的数据，size() 就是真正读到的字节数
 * @param isLast  输出参数：true = 这是最后一块
 * @return 成功返回 true；读出错返回 false
 * @note 【扫一眼】
 */
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
