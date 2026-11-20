#pragma once

#include <cstdint>

#include "common/detection_types.h"

// 一帧 NV12 在内存里的样子：Y 平面 horStride × 高；紧跟着（或别处）UV 平面，每行 horStride 字节，U V U V 交错，
// 每组 UV 管 2×2 个像素。不依赖 MPP，Mac 单测直接拿 std::vector 当内存。
struct Nv12PlaneView {
    uint8_t *yPlane = nullptr;
    uint8_t *uvPlane = nullptr;
    int width = 0;      // 有效画面宽，像素
    int height = 0;
    int horStride = 0;  // 一行占多少字节（≥ width），Y 和 UV 平面相同
};

struct Nv12Color {
    uint8_t lumaValue = 0;  // Y
    uint8_t chromaUValue = 128;
    uint8_t chromaVValue = 128;
};

// 常用颜色（BT.601 有限范围，推断值，上板看截图确认）
constexpr Nv12Color kNv12Green = {145, 54, 34};
constexpr Nv12Color kNv12Red = {81, 90, 240};

// 画一个空心矩形。坐标先对齐到偶数（左上往小取、右下往大取），因为一组 UV 管 2×2 个像素，
// 奇数坐标会让框边上的颜色只改了一半像素；超出画面的部分裁掉。lineWidthPixels 也按偶数处理。
void drawNv12Rectangle(const Nv12PlaneView &picture, const DetectionBox &box, const Nv12Color &color,
                       int lineWidthPixels);
