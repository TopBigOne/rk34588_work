#pragma once

#include <cstdint> // uint8_t、int64_t
#include <memory>  // std::shared_ptr
#include <vector>

/**
 * @brief 一张转好的 RGBX 画面（每个像素 4 字节：R、G、B、一个不用的字节），准备交给界面显示
 * @note 不依赖 Qt：pipeline 层只认这个结构体，由 player 层把它包成 QImage（不复制像素）。
 *       像素放在 shared_ptr 管着的 vector 里：QImage 显示期间还要用这块内存，
 *       谁最后一个放手（视频线程或者界面），谁负责释放
 */
struct RgbxPicture {
    std::shared_ptr<std::vector<uint8_t>> pixelBufferPtr; // 像素数据，大小 = strideBytes × height
    int width = 0;                                        // 宽，像素
    int height = 0;                                       // 高，像素
    int strideBytes = 0;                                  // 一行占多少字节（= width × 4，没有填充）
    int64_t ptsUs = 0;                                    // 这一帧的显示时间，微秒
};
