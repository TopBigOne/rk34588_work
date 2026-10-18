// day3_cam2h264：先用 FindCamera 找出摄像头节点（M2 之前的准备）
// 写 M2 时把这个文件整个替换成"具体编码day3_cam2h264的流程.md"里的 M2 代码，
// 可以把下面的查找逻辑留下来，当作 kDevice 的默认值
#include "camera/find_camera.h"
#include "common/log.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <linux/media.h>
#include <linux/videodev2.h>
#include <queue>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define CHECK_CAMERA_INFO (0)
const char* const kTag = "day3_main"; // 日志标签（LOGD / LOGI / LOGE 的第一个参数）

static const char* kDevice         = "/dev/video22";
static const int kWidth            = 1920;
static const int kHeight           = 1080;
static const int kSkipFrames       = 30;
static const char* kOutputFilePath = "/userdata/av/cam_m2.nv12";

static const int kBufCount          = 4;
static const v4l2_buf_type kBufType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
#include <sys/mman.h>
struct CamBuffer {
    void* start   = MAP_FAILED;
    size_t length = 0;
};

static int g_videoFd = -1;
static CamBuffer g_bufs[kBufCount];
static bool g_streaming = false;
static FILE* g_out      = nullptr;


static int run();

static void clear_up();


static void getCameraInfo();

void getCameraInfo() {
    if (CHECK_CAMERA_INFO) {
        FindCamera find_camera;
        find_camera.printAllCameraInfo();
    }
}

static int xioctl(int fd, unsigned long request, void* arg) {
    int ret;
    do {
        // todo ioctl() 的用法 是什么
        ret = ioctl(fd, request, arg);
    } while (ret == -1 && errno == EINTR);
    return ret;
}

// 调用一个 V4L2 ioctl，失败就打印是哪一句、错误原因，然后 return -1（run() 返回后由 cleanup() 释放资源）
#define V4L2_CHECK(request, arg)                                                        \
    do {                                                                                \
        if (xioctl(g_videoFd, request, arg) == -1) {                                    \
            printf("    [FAIL] %s: %s (line %d)", #request, strerror(errno), __LINE__); \
            return -1;                                                                  \
        }                                                                               \
    } while (0)


bool queue_buffer(int i);
int dequeue_buffer(uint32_t* bytesUsed);

/**
 * VIDIOC_QBUF:把 第index 块缓冲区 交给驱动，往这里写下一帧
 * @param index
 * @return
 */
bool queue_buffer(int index) {
    v4l2_plane planes[1] = {};
    v4l2_buffer buf      = {};
    buf.type             = kBufType;
    buf.memory           = V4L2_MEMORY_MMAP;
    buf.index            = index;
    buf.m.planes         = planes;
    buf.length           = 1;
    if (xioctl(g_videoFd, VIDIOC_QBUF, &buf) == -1) {
        LOGE(kTag, "     [FAIL] VIDIOC_QBUF %d: %s\n", index, strerror(errno));
        return false;
    }
    return true;
}

/**
 *
 * @param index
 * @return
 */
int dequeue_buffer(uint32_t* bytesUsed) {
    v4l2_plane planes[1] = {};
    v4l2_buffer buf      = {};
    buf.type             = kBufType;
    buf.memory           = V4L2_MEMORY_MMAP;
    buf.m.planes         = planes;
    buf.length           = 1;
    if (xioctl(g_videoFd, VIDIOC_DQBUF, &buf) == -1) {
        LOGE(kTag, "    [FAIL] VIDIOC_DQBUF: %s\n", strerror(errno));
        return -1;
    }

    if (buf.index >= kBufCount) {
        LOGE(kTag, "    [FAIL] VIDIOC_DQBUF 返回的序号 %u 越界\n", buf.index);
        return -1;
    }
    if (bytesUsed) {
        *bytesUsed = planes[0].bytesused;
    }
    return static_cast<int>(buf.index);
}
int run() {
    LOGD(kTag, "%s", __func__);
    // step 1======== open camera
    // O_CLOEXEC: Set close_on_exec.
    g_videoFd = open(kDevice, O_RDWR | O_CLOEXEC);
    if (g_videoFd < 0) {
        LOGE(kTag, "    open camera in failed.");
        LOGE(kTag, "    [FAIL] open %s: %s\n", kDevice, strerror(errno));
        return -1;
    }
    LOGI(kTag, "    open camera in success.");
    v4l2_capability cap = {};
    // 内部调用：xioctl
    V4L2_CHECK(VIDIOC_QUERYCAP, &cap);

    LOGI(kTag, "    device %s: driver %s, card %s", kDevice, cap.driver, cap.card);
    __u32 caseOne = cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE;
    __u32 caseTwo = cap.capabilities & V4L2_CAP_STREAMING;
    if (!caseOne || !caseTwo) {
        LOGE(kTag, "    %s 不支持 Multiplanar 采集或者 streaming", kDevice);
        return -1;
    }
    // step 2======== config format
    v4l2_format fmt            = {};
    fmt.type                   = kBufType;
    fmt.fmt.pix_mp.width       = kWidth;
    fmt.fmt.pix_mp.height      = kHeight;
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    fmt.fmt.pix_mp.field       = V4L2_FIELD_NONE;
    fmt.fmt.pix_mp.num_planes  = 1;
    // set format by manual
    V4L2_CHECK(VIDIOC_S_FMT, &fmt);
    // get again because the drive maybe change it
    V4L2_CHECK(VIDIOC_G_FMT, &fmt);
    LOGI(kTag, "    format: %ux%u NV12, planes %u, bytesperline %u, sizeimage %u", fmt.fmt.pix_mp.width,
        fmt.fmt.pix_mp.height, fmt.fmt.pix_mp.num_planes, fmt.fmt.pix_mp.plane_fmt[0].bytesperline,
        fmt.fmt.pix_mp.plane_fmt[0].sizeimage);
    if (fmt.fmt.pix_mp.pixelformat != V4L2_PIX_FMT_NV12 || fmt.fmt.pix_mp.num_planes != 1) {
        LOGE(kTag, "[FAIL] 驱动没有给 1 个 plane 的 NV12");
        return -1;
    }
    // step 3======== require 4 block buffer
    v4l2_requestbuffers req = {};
    req.count               = kBufCount;
    req.type                = kBufType;
    req.memory              = V4L2_MEMORY_MMAP;
    V4L2_CHECK(VIDIOC_REQBUFS, &req);
    if (req.count != kBufCount) {
        LOGE(kTag, "     [FAIL] 要 %d 块缓冲区，驱动给了 %u 块\n", kBufCount, req.count);
        return -1;
    }
    LOGI(kTag, "    request %d block buffer in success", req.count);

    // 4: config pre buffer
    for (int i = 0; i < kBufCount; ++i) {
        v4l2_plane planes[1] = {};

        v4l2_buffer buf = {};
        buf.type        = kBufType;
        buf.memory      = V4L2_MEMORY_MMAP;
        buf.index       = i;
        buf.m.planes    = planes;
        buf.length      = 1;
        V4L2_CHECK(VIDIOC_QUERYBUF, &buf);
        g_bufs[i].length = planes[0].length;
        g_bufs[i].start =
            mmap(nullptr, planes->length, PROT_READ | PROT_WRITE, MAP_SHARED, g_videoFd, planes[0].m.mem_offset);
        if (g_bufs[i].start == MAP_FAILED) {
            LOGE(kTag, "     [FAIL] mmap buffer %d: %s", i, strerror(errno));
            return -1;
        }
        LOGI(kTag, "      buffer %d: length %zu", i, g_bufs[i].length);
    }

    // 5: start sample with these four buffer
    for (int i = 0; i < kBufCount; ++i) {
        if (!queue_buffer(i)) {
            return -1;
        }
    }
    v4l2_buf_type type = kBufType;
    V4L2_CHECK(VIDIOC_STREAMON, &type);
    g_streaming = true;
    // 6: drop some frame while only start
    for (int i = 0; i < kSkipFrames; ++i) {
        int index = dequeue_buffer(nullptr);
        if (index < 0 || !queue_buffer(index)) {
            return -1;
        }
    }

    // 7: get one frame , then store it ,at last return back;
    // 这一帧实际用了缓冲区里多少字节
    uint32_t bytesUsed = 0;
    int index          = dequeue_buffer(&bytesUsed);
    if (index < 0) {
        return -1;
    }
    g_out = fopen(kOutputFilePath, "wb");
    if (!g_out) {
        LOGE(kTag, "      [FAIL] fopen %s: %s\n", kOutputFilePath, strerror(errno));
        queue_buffer(index);
        return -1;
    }
    fwrite(g_bufs[index].start, 1, bytesUsed, g_out);
    LOGI(kTag, "      frame: buffer %d, %u bytes -> %s\n", index, bytesUsed, kOutputFilePath);
    if (!queue_buffer(index)) {
        return -1;
    }

    return 0;
}

static void clear_up() {
    if (g_streaming) {
        v4l2_buf_type type = kBufType;
        xioctl(g_videoFd, VIDIOC_STREAMOFF, &type);
        g_streaming = false;
    }

    for (auto& g_buf : g_bufs) {
        if (g_buf.start != MAP_FAILED) {
            munmap(g_buf.start, g_buf.length);
            g_buf.start = MAP_FAILED;
        }
    }
    if (g_videoFd >= 0) {
        close(g_videoFd);
        g_videoFd = -1;
    }
    if (g_out) {
        fclose(g_out);
        g_out = nullptr;
    }
}


int main() {
    int ret = 0;
    getCameraInfo();
    ret = run();


    clear_up();


    return ret;
}
