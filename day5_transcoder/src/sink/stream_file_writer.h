//
// Created by DEV on 2026/11/14.
//
#pragma once

#include <cstdint>
#include <vector>
#include "common/mpp_utils.h"
class StreamFileWriter {

public:
    bool open(const char*path);

    bool writeBytes(const std::vector<uint8_t> &data);

    int64_t getWritenBytes()const {
        return writenBytes;
    }


private:
    FilePtr file;
    int64_t writenBytes = 0;

};