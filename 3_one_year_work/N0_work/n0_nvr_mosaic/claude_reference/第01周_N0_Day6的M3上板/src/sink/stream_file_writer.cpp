#include "sink/stream_file_writer.h"

#include <cerrno>  // errno
#include <cstring> // strerror

#include "common/log.h"

static const char *const kTag = "StreamWriter";

/**
 * @brief 创建输出文件
 * @param[in] filePath  文件路径
 * @return true = 成功；false = 失败
 * @note "wb"：只写、二进制，文件存在就清空
 */
bool StreamFileWriter::open(const char *filePath) {
    filePtr.reset(fopen(filePath, "wb"));
    if (!filePtr) {
        LOGE(kTag, "打开 %s 失败：%s", filePath, strerror(errno));
        return false;
    }
    return true;
}

/**
 * @brief 追加写一段数据
 * @param[in] data  要写的数据
 * @return true = 成功；false = 失败
 * @note fwrite 返回实际写了几个元素（元素大小写的 1，就是字节数），比要写的少就是出错了（比如磁盘满）
 */
bool StreamFileWriter::writeBytes(const std::vector<uint8_t> &data) {
    if (data.empty()) {
        return true;
    }
    if (fwrite(data.data(), 1, data.size(), filePtr.get()) != data.size()) {
        LOGE(kTag, "fwrite 没写完：%s", strerror(errno));
        return false;
    }
    writtenBytes += static_cast<int64_t>(data.size());
    return true;
}
