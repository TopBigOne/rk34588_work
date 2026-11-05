//
// Created by DEV on 2026/11/4.
//
#pragma once

#include <rockchip/rk_mpi.h>

struct Nv12Picture {
    MppBuffer mppBuf = nullptr;
    int width = 0;
    int height = 0;
    int horStride = 0;
    int verStride = 0;
};

/**
 * 用RGA把srcPicture 缩放dstPicture 的尺寸（NV12->NV12）
 * @param srcPicture
 * @param dstPicture
 * @return
 */
bool resize_nv12_by_rga(const Nv12Picture &srcPicture, const Nv12Picture &dstPicture);
