//
// Created by DEV on 2026/11/9.
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "common/mpp_utils.h"

class StreamFileReader {

public:
    bool open(const char *filePath, size_t chunkSizeBytes = 64 * 1024);


    /**
     * 读下一块
     * @param chunk
     * @param isLastChunk
     * @return
     */
    bool readChunk(std::vector<uint8_t> &chunk, bool &isLastChunk);
    int64_t getReadBytes() const { return readBytes; }

private:
    FilePtr filePtr;
    size_t chunkSizeBytes = 0;
    int64_t readBytes = 0;
};
