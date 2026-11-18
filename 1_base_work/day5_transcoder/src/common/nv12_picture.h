//
// Created by DEV on 2026/11/9.
//
#pragma once
#include <rockchip/rk_mpi.h>
#include "common/mpp_utils.h"

struct Nv12Picture {
    MppBuffer mppBuf = nullptr;
    int width = 0;
    int height = 0;
    int horStride = 0;
    int verStride = 0; // Y 平面在内存占多少行（>= height）; UV 平面从第verStride 行开始
};

Nv12Picture makeAlignedNv12Picture(int width,int height);

MppBufferPtr allocNv12Buffer(MppBufferGroup bufferGroup,Nv12Picture &picture);

bool describeDecodedFrame(MppFrame decodedFrame,Nv12Picture &picture);


