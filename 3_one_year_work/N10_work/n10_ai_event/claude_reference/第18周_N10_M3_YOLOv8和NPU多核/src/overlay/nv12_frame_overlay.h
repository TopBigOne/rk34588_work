#pragma once

#include <vector>

#include <rockchip/rk_mpi.h>

#include "common/detection_types.h"
#include "overlay/nv12_box_drawer.h"

// 在 MPP 解码帧上画检测框（CPU 写解码器的 buffer，前后做 cache 同步）。
// 只画 classIndex == onlyClassIndex 的框；onlyClassIndex < 0 = 全画。返回画了几个。
int drawDetectionBoxesOnFrame(MppFrame decodedFrame, const std::vector<DetectedObject> &objects, int onlyClassIndex,
                              const Nv12Color &color, int lineWidthPixels);

// 把帧存成紧凑 NV12（去掉 stride 多出来的部分），Mac 上：
// ffmpeg -f rawvideo -pix_fmt nv12 -s 宽x高 -i 文件 out.png
bool saveFrameAsCompactNv12(MppFrame decodedFrame, const char *filePath);
