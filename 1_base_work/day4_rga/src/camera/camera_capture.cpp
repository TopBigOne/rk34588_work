//
// Created by DEV on 2026/11/7.
//

#include "camera_capture.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <queue>
#include <sys/ioctl.h>
#include <unistd.h>

#include "common/log.h"

static const char *kTag = "Camera";
static const v4l2_buf_type v4l2BufType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
static int xioctl(int fd, unsigned long request, void *args) {
    int ret;
    do {
        ret = ioctl(fd, request, args);
    } while (ret == -1 && errno == EINTR);
    return ret;
}

bool CameraCapture::openAndStart(const char *devicePath, int width, int height) {
    // step 1:
    videoFd = open(devicePath, O_RDWR | O_CLOEXEC);
    if (videoFd < 0) {
        LOGE(kTag, "open %s: %s", devicePath, strerror(errno));
        return false;
    }
    // step 2:
    v4l2_format v4L2Format = {};
    v4L2Format.type = v4l2BufType;
    v4L2Format.fmt.pix_mp.width = width;
    v4L2Format.fmt.pix_mp.height = height;
    v4L2Format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    v4L2Format.fmt.pix_mp.field = V4L2_FIELD_NONE;
    v4L2Format.fmt.pix_mp.num_planes = 1;

    if (xioctl(videoFd, VIDIOC_S_FMT, &v4L2Format) == -1) {
        LOGE(kTag, "VIDIOC_S_FMT: %s", strerror(errno));
        return false;
    }

    if (xioctl(videoFd, VIDIOC_G_FMT, &v4L2Format) == -1) {
        LOGE(kTag, "VIDIOC_S_FMT / G_FMT: %s", strerror(errno));
        return false;
    }
    this->width = static_cast<int>(v4L2Format.fmt.pix_mp.width);
    this->height = static_cast<int>(v4L2Format.fmt.pix_mp.height);

    horStride = static_cast<int>(v4L2Format.fmt.pix_mp.plane_fmt[0].bytesperline);
    unsigned int sizeImageBytes = v4L2Format.fmt.pix_mp.plane_fmt[0].sizeimage;
    if (v4L2Format.fmt.pix_mp.pixelformat != V4L2_PIX_FMT_NV12 || v4L2Format.fmt.pix_mp.num_planes != 1 ||
        horStride <= 0) {
        LOGE(kTag, "驱动没有给 1 个 plane 的 NV12");
        return false;
    }
    // todo 为啥verStride 要自己算呢？
    verStride = sizeImageBytes / horStride * 2 / 3;
    LOGI(kTag, "%s: %dx%d NV12，stride %dx%d，sizeimage %u", devicePath, width, height, horStride, verStride,
         sizeImageBytes);

    // step 3: acquire 4 blocks
    v4l2_requestbuffers requestBuffer = {};
    requestBuffer.count = bufCount;
    requestBuffer.type = v4l2BufType;
    requestBuffer.memory = V4L2_MEMORY_MMAP;
    if (xioctl(videoFd, VIDIOC_REQBUFS, &requestBuffer) == -1) {
        LOGE(kTag, "VIDIOC_REQBUFS in error: %s", strerror(errno));
        return false;
    }

    if (requestBuffer.count != bufCount) {
        LOGE(kTag, "VIDIOC_REQBUFS: 要 %d 块，给了 %u 块，%s", bufCount, requestBuffer.count, strerror(errno));
        return false;
    }

    // step 4:
    // 4.1
    for (int bufIndex = 0; bufIndex < bufCount; ++bufIndex) {
        v4l2_plane plane[1] = {};
        v4l2_buffer v4l2Buffer = {};
        v4l2Buffer.type = v4l2BufType;
        v4l2Buffer.memory = V4L2_MEMORY_MMAP;
        v4l2Buffer.index = bufIndex;
        v4l2Buffer.m.planes = plane;
        v4l2Buffer.length = 1;

        if (xioctl(videoFd, VIDIOC_QUERYBUF, &v4l2Buffer) == -1) {
            LOGE(kTag, "VIDIOC_QUERYBUF %d: %s", bufIndex, strerror(errno));
            return false;
        }
        // 4.2 把这块内存导出成dma_buf fd ,不分配新内存，只是给同同一个内存 整一个别的硬件也认得fd
        v4l2_exportbuffer exportBuffer = {};
        exportBuffer.index = bufIndex;
        exportBuffer.type = v4l2BufType;
        // export Buffer之前，为啥plane 被设置成0；
        exportBuffer.plane = 0;
        exportBuffer.flags = O_CLOEXEC;
        if (xioctl(videoFd, VIDIOC_EXPBUF, &exportBuffer) == -1) {
            LOGE(kTag, "VIDIOC_EXPBUF %d: %s", bufIndex, strerror(errno));
            return false;
        }
        // 驱动写回的fd，析构时close？
        cameraBuf[bufIndex].dmaFd = exportBuffer.fd;

        // 4.3
        MppBufferInfo bufInfo = {};
        bufInfo.type = MPP_BUFFER_TYPE_EXT_DMA;
        bufInfo.fd = exportBuffer.fd;
        bufInfo.size = plane[0].length; // 整块buffer的字节数
        bufInfo.index = bufIndex;
        // 把外部内存（一般是 dma_buf fd）包装成一个 MppBuffer
        if (mpp_buffer_import(&cameraBuf[bufIndex].mppBuffer, &bufInfo) != MPP_OK) {
            LOGE(kTag, "mpp_buffer_import %d 失败", bufIndex);
            return false;
        }
    }

    // step 5: 把4块空buffer 全部交给驱动，再开拍
    for (int i = 0; i < bufCount; ++i) {
        if (!queueBuffer(i)) {
            return false;
        }
    }

    v4l2_buf_type bufType = v4l2BufType;
    if (xioctl(videoFd, VIDIOC_STREAMON, &bufType) == -1) {
        LOGE(kTag, "VIDIOC_STREAMON: %s", strerror(errno));
        return false;
    }
    isStreaming = true;
    return true;
}

bool CameraCapture::queueBuffer(int bufIndex) {
    v4l2_plane plane[1] = {};
    v4l2_buffer v4l2Buffer = {};
    v4l2Buffer.type = v4l2BufType;
    v4l2Buffer.memory = V4L2_MEMORY_MMAP;
    v4l2Buffer.index = bufIndex;
    v4l2Buffer.m.planes = plane;
    v4l2Buffer.length = 1;
    if (xioctl(videoFd, VIDIOC_QBUF, &v4l2Buffer) == -1) {
        LOGE(kTag, "VIDIOC_QBUF %d: %s", bufIndex, strerror(errno));
        return false;
    }
    return true;
}

int CameraCapture::dequeueFilledBuffer() {
    v4l2_plane plane[1] = {};
    v4l2_buffer buf = {};
    buf.type = v4l2BufType;
    buf.m.planes = plane;
    buf.length = 1;
    buf.memory = V4L2_MEMORY_MMAP;
    if (xioctl(videoFd, VIDIOC_DQBUF, &buf) == -1) {
        LOGE(kTag, "VIDIOC_DQBUF in error : %s", strerror(errno));
        return -1;
    }
    if (buf.index >= bufCount) {
        LOGE(kTag, "VIDIOC_DQBUF 返回的序号 %u 越界", buf.index);
        return -1;
    }
    return static_cast<int>(buf.index);
}

CameraCapture::~CameraCapture() {
    if (isStreaming) {
        v4l2_buf_type bufType = v4l2BufType;
        xioctl(videoFd, VIDIOC_STREAMOFF, &bufType);
    }
    for (auto buf: cameraBuf) {
        if (buf.mppBuffer != nullptr) {
            mpp_buffer_put(buf.mppBuffer);
        }
        if (buf.dmaFd >= 0) {
            close(buf.dmaFd);
        }
    }
    if (videoFd >= 0) {
        close(videoFd);
    }
}
