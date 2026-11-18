//
// Created by DEV on 2026/11/7.
//

#pragma once

#include <linux/videodev2.h>
#include <rockchip/rk_mpi.h>

struct CameraBuf {
    int dmaFd = -1;
    MppBuffer mppBuffer = nullptr;
};
class CameraCapture {
public:
    static constexpr int bufCount = 4;
    CameraCapture() = default;
    ~CameraCapture();
    CameraCapture(const CameraCapture &) = delete;
    CameraCapture &operator=(const CameraCapture &) = delete;

    bool openAndStart(const char *devicePath, int width, int height);

    int dequeueFilledBuffer();

    bool queueBuffer(int bufIndex);
    MppBuffer getMppBuffer(const int index) const { return cameraBuf[index].mppBuffer; }

    int getWidth() const { return width; }

    int getHeight() const { return height; }

    int getHorStride() const { return horStride; }

    int getVerStride() const { return verStride; }



private:
    int videoFd = -1;
    bool isStreaming = false;
    CameraBuf cameraBuf[bufCount];
    int width = 0;
    int height = 0;
    int horStride = 0;
    int verStride = 0;
};
