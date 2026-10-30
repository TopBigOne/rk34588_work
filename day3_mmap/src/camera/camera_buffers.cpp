#include "camera/camera_buffers.h"

#include "common/log.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static const char* kTag = "Camera";

bool open_camera_and_map_buffers(CameraBuffers& camera) {
    LOGD(kTag, "open_camera_and_map_buffers()");
    camera.videoFd = open(kVideoDevicePath, O_RDWR | O_CLOEXEC);
    if (camera.videoFd < 0) {
        LOGE(kTag, "     open %s: %s", kVideoDevicePath, strerror(errno));
        return false;
    }
    v4l2_format format            = {};
    format.type                   = kBufType;
    format.fmt.pix_mp.height      = kHeight;
    format.fmt.pix_mp.width       = kWidth;
    format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    // 这个格式实际用了几个平面（几块独立内存）。
    // 由 pixelformat 决定：NV12 为 1，NV12M 为 2，YUV420M 为 3。
    format.fmt.pix_mp.num_planes = 1;
    if (ioctl(camera.videoFd, VIDIOC_S_FMT, &format) != 0) { // 不等于0 ，就是失败了
        LOGE(kTag, "     VIDIOC_S_FMT: %s（相机 App 或者别的程序占着摄像头？）", strerror(errno));
        return false;
    }
    v4l2_requestbuffers requestBufs = {};
    requestBufs.count               = kBufCount;
    requestBufs.type                = kBufType;
    requestBufs.memory              = V4L2_MEMORY_MMAP; // 缓冲区内存的来源/管理方式
    if (ioctl(camera.videoFd, VIDIOC_REQBUFS, &requestBufs) != 0) {
        LOGE(kTag, "     VIDIOC_REQBUFS: %s", strerror(errno));
        return false;
    }
    // count 是"填 + 读回"两头看：驱动可能给得比要的少（doc/4 第 3 节），
    // 少了的话下面 QUERYBUF 那几块会报 EINVAL，不如在这里就说清楚
    if (requestBufs.count < kBufCount) {
        LOGE(kTag, "     VIDIOC_REQBUFS: 要 %d 块，驱动只给了 %u 块", kBufCount, requestBufs.count);
        return false;
    }

    for (int bufIndex = 0; bufIndex < kBufCount; ++bufIndex) {
        v4l2_plane planes[1] = {};
        v4l2_buffer v4l2Buf  = {};
        v4l2Buf.type         = kBufType; // 缓冲区类型
        v4l2Buf.memory       = V4L2_MEMORY_MMAP; // memory 决定用哪种内存方式，
        v4l2Buf.index        = bufIndex;  // Note : 每次循环，这是变化的地方，所以 planes[0].m.mem_offset 值是不一样
        v4l2Buf.m.planes     = planes;
        v4l2Buf.length       = 1;
        if (ioctl(camera.videoFd, VIDIOC_QUERYBUF, &v4l2Buf) != 0) {
            LOGE(kTag, "     VIDIOC_QUERYBUF %d: %s", bufIndex, strerror(errno));
            return false;
        }
        LOGI(kTag, "       |--------------------------------------------------↓");
        LOGI(kTag, "        -videoFd    ：%d", camera.videoFd);
        LOGI(kTag, "        -length     ：%d", planes[0].length);
        LOGI(kTag, "        -mem_offset ：0x%x", planes[0].m.mem_offset);
        LOGI(kTag, "       |--------------------------------------------------↑");
        void* mmapAddress =
            mmap(nullptr, planes[0].length, PROT_READ | PROT_WRITE, MAP_SHARED, camera.videoFd, planes[0].m.mem_offset);
        if (mmapAddress == MAP_FAILED) {
            LOGE(kTag, "     mmap camera buffer %d: %s", bufIndex, strerror(errno));
            return false;
        }
        camera.memOffsets[bufIndex]    = planes[0].m.mem_offset;
        camera.lengths[bufIndex]       = planes[0].length;
        camera.mmapAddresses[bufIndex] = mmapAddress;
    }
    return true;
}

void close_camera(CameraBuffers& camera) {
    for (int bufIndex = 0; bufIndex < kBufCount; ++bufIndex) {
        if (camera.mmapAddresses[bufIndex] != nullptr) {
            munmap(camera.mmapAddresses[bufIndex], camera.lengths[bufIndex]);
            camera.mmapAddresses[bufIndex] = nullptr;
        }
    }
    if (camera.videoFd >= 0) {
        close(camera.videoFd);
        camera.videoFd = -1;
    }
}
