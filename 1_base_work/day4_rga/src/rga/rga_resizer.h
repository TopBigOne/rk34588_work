//
// Created by DEV on 2026/11/4.
//
#pragma once

#include "common/nv12_picture.h" // Nv12Picture 搬到了这里（重构时从本文件移出，M1～M3 都要用）

/**
 * 用RGA把srcPicture 缩放dstPicture 的尺寸（NV12->NV12）
 * @param srcPicture
 * @param dstPicture
 * @return
 */
bool resize_nv12_by_rga(const Nv12Picture &srcPicture, const Nv12Picture &dstPicture);
