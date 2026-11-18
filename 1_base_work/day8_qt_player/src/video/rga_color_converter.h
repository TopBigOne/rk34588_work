#pragma once

#include <rockchip/rk_mpi.h>

#include "video/rgbx_picture.h"

/**
 * @brief 用 RGA 把解码出来的 NV12 画面转成 RGBX，同时缩放到指定大小
 * @param[in]  nv12Frame      MPP 解出来的帧（NV12，buffer 是 DRM 类型，有 dma-buf fd）
 * @param[in]  outputWidth    输出宽，像素；要求是 16 的倍数（RGA 对 RGBX 的行宽有对齐要求，调用方先对齐好）
 * @param[in]  outputHeight   输出高，像素；要求是 2 的倍数
 * @param[out] outputPicture  转好的画面：新申请一块内存放像素，pts 从帧上抄过来
 * @return true = 成功；false = 失败（已打印原因）
 * @note 颜色转换用 BT.709（高清视频的标准）。源用 dma-buf fd 导入，零拷贝；
 *       目标是普通内存（vector），用虚拟地址导入，RGA 通过 IOMMU 直接写进去，CPU 不碰像素
 */
bool convertNv12FrameToRgbxByRga(MppFrame nv12Frame, int outputWidth, int outputHeight, RgbxPicture &outputPicture);
