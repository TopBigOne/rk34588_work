#pragma once

#include <cstdint>
#include <vector>

#include "npu/letterbox.h"

struct ImageModelInput {
    int sourceWidth = 0;
    int sourceHeight = 0;
    LetterboxInfo letterboxInfo;
    std::vector<uint8_t> modelInputRgb;  // modelWidth × modelHeight × 3
};

// 读 jpg/png（stb_image，输出 RGB 顺序）→ CPU letterbox 成模型输入。m0 和 m1cmp 用
bool loadImageAsModelInput(const char *imagePath, int modelWidth, int modelHeight, ImageModelInput &imageInput);
