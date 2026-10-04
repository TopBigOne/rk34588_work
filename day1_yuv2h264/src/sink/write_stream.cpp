/*
 * WriteStream 的实现，说明见 write_stream.h
 */
#include "sink/write_stream.h"

#include <cstdio> // fopen / fwrite / printf

bool WriteStream::open(const char* path) {
    streamOutputFile_.reset(fopen(path, "wb"));
    if (!streamOutputFile_) {
        printf("open %s failed\n", path);
        return false;
    }
    return true;
}

bool WriteStream::write(const std::vector<uint8_t>& data) {
    return fwrite(data.data(), 1, data.size(), streamOutputFile_.get()) == data.size();
}
