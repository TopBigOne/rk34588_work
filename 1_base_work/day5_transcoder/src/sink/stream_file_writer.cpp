//
// Created by DEV on 2026/11/14.
//

#include "stream_file_writer.h"
#include <cerrno>
#include <cstring>
#include "common/log.h"
#include "sink/stream_file_writer.h"


static const char *const kTag = "StreamWriter";

bool StreamFileWriter::open(const char *path) {
    file.reset(fopen(path, "wb"));
    if (!file) {
        LOGE(kTag, "打开 %s 失败：%s", path, strerror(errno));
        return false;
    }
    return true;
}

bool StreamFileWriter::writeBytes(const std::vector<uint8_t> &data) {
    if (data.empty()) {
        return true;
    }
    if (fwrite(data.data(), 1, data.size(), file.get()) != data.size()) {
        LOGE(kTag, "fwrite 没写完：%s", strerror(errno));
        return false;
    }
    writenBytes += static_cast<int64_t>(data.size());
    return true;
}
