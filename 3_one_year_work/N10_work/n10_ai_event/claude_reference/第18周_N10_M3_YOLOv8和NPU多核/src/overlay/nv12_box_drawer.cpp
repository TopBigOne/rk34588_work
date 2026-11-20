#include "overlay/nv12_box_drawer.h"

#include <algorithm>
#include <cstring>

// 把 [left, right) × [top, bottom) 这块实心矩形涂成 color；四个坐标都已经是偶数、在画面内
static void fillEvenAlignedArea(const Nv12PlaneView &picture, int left, int top, int right, int bottom,
                                const Nv12Color &color) {
    if (right <= left || bottom <= top) {
        return;
    }
    for (int row = top; row < bottom; row++) {
        memset(picture.yPlane + static_cast<size_t>(row) * picture.horStride + left, color.lumaValue, right - left);
    }
    // UV 平面高度是 Y 的一半；一行里第 k 组 UV 在第 2k、2k+1 个字节，管 x = 2k、2k+1 两个像素
    for (int uvRow = top / 2; uvRow < bottom / 2; uvRow++) {
        uint8_t *uvRowStart = picture.uvPlane + static_cast<size_t>(uvRow) * picture.horStride;
        for (int x = left; x < right; x += 2) {
            uvRowStart[x] = color.chromaUValue;
            uvRowStart[x + 1] = color.chromaVValue;
        }
    }
}

void drawNv12Rectangle(const Nv12PlaneView &picture, const DetectionBox &box, const Nv12Color &color,
                       int lineWidthPixels) {
    const int evenWidth = picture.width & ~1;
    const int evenHeight = picture.height & ~1;
    // 右、下边界换成“不包含”的写法（+1），再对齐：左上 & ~1 往小取，右下 +1 再 & ~1 往大取
    int left = std::clamp(box.left & ~1, 0, evenWidth);
    int top = std::clamp(box.top & ~1, 0, evenHeight);
    int right = std::clamp((box.right + 2) & ~1, 0, evenWidth);
    int bottom = std::clamp((box.bottom + 2) & ~1, 0, evenHeight);
    if (right <= left || bottom <= top) {
        return;
    }
    int lineWidth = std::max(2, (lineWidthPixels + 1) & ~1);
    int topBandBottom = std::min(top + lineWidth, bottom);
    int bottomBandTop = std::max(bottom - lineWidth, top);
    int leftBandRight = std::min(left + lineWidth, right);
    int rightBandLeft = std::max(right - lineWidth, left);
    fillEvenAlignedArea(picture, left, top, right, topBandBottom, color);         // 上边
    fillEvenAlignedArea(picture, left, bottomBandTop, right, bottom, color);      // 下边
    fillEvenAlignedArea(picture, left, top, leftBandRight, bottom, color);        // 左边
    fillEvenAlignedArea(picture, rightBandLeft, top, right, bottom, color);       // 右边
}
