// day3_cam2h264：先用 FindCamera 找出摄像头节点（M2 之前的准备）
// 写 M2 时把这个文件整个替换成"具体编码day3_cam2h264的流程.md"里的 M2 代码，
// 可以把下面的查找逻辑留下来，当作 kDevice 的默认值
#include "camera/find_camera.h"
#include "common/log.h"
#include "encoder/mpp_encoder.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

#define CHECK_CAMERA_INFO (0)
const char* const kTag             = "day3_main"; // 日志标签（LOGD / LOGI / LOGE 的第一个参数）
static const char* kDevice         = "/dev/video22";
static const int kWidth            = 1920;
static const int kHeight           = 1080;
static const int kFrames           = 500;
static const int kSkipFrames       = 30;
static const char* kOutputFilePath = "/userdata/av/cam_m3.h264";
static const int kBufCount         = 4;
// 多平面视频采集
static const v4l2_buf_type kBufType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

struct CamBuffer {
    void* start      = MAP_FAILED;
    size_t length    = 0;
    int dmaFd        = -1;
    MppBuffer mppBuf = nullptr;
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
        ret = ioctl(fd, request, arg);
    } while (ret == -1 && errno == EINTR);
    return ret;
}

// 调用一个 V4L2 ioctl，失败就打印是哪一句、错误原因，然后 return -1（run() 返回后由 cleanup() 释放资源）
#define V4L2_CHECK(request, arg)                                                      \
    do {                                                                              \
        if (xioctl(g_videoFd, request, arg) == -1) {                                  \
            LOGE(kTag, "     %s: %s (line %d)", #request, strerror(errno), __LINE__); \
            return -1;                                                                \
        }                                                                             \
    } while (0)


static bool queue_buffer(int i);

static int dequeue_buffer(uint32_t* bytesUsed);

/**
 * @brief VIDIOC_QBUF：把第 index 块缓冲区还给驱动（入队），让摄像头往里写下一帧
 *
 * 缓冲区轮转里的"还回去"那一步（guide/04 第 7 节）：缓冲区从"程序手里"回到驱动的"等着被写"队列。
 * 板子的节点是多平面类型，所以要挂上 planes 数组、buf.length 填 plane 个数 1（不挂会返回 EINVAL）。
 *
 * @param[in] index  缓冲区序号，0 ～ kBufCount-1，一般就是 dequeue_buffer() 返回的那个
 * @return 成功返回 true；ioctl 失败打印原因并返回 false
 *
 * @note ⭐ 零拷贝时一定要在 encoder.encode() 返回之后才调用：QBUF 以后摄像头随时可能往这块写下一帧，
 *       编码器还在读的话画面会撕裂。
 * @note DQBUF 拿出来的缓冲区，不管后面成功还是失败都要还回来；少还一块就少一块，
 *       4 块都不还，下一次 DQBUF 就永远等不到（2026-10-18 实测：编码后不 QBUF，10 帧时卡死）。
 */
bool queue_buffer(int index) {
    v4l2_plane planes[1] = {};
    v4l2_buffer v4l2Buf  = {};
    v4l2Buf.type         = kBufType;
    v4l2Buf.memory       = V4L2_MEMORY_MMAP;
    v4l2Buf.index        = index;
    v4l2Buf.m.planes     = planes;
    v4l2Buf.length       = 1;
    // ioctl(谁, 干什么, 表格)：
    //   g_videoFd   = 谁：发给 /dev/video22 背后的摄像头驱动（rkisp mainpath）
    //   VIDIOC_QBUF = 干什么：命令号"把 buffer 还回去"，videodev2.h 第 5120 行 _IOWR('V', 15, struct v4l2_buffer)
    //   &buf        = 表格：还哪一块（index）、什么类型
    // 两者的隐形关系：buffer 队列挂在"做了 REQBUFS 的那个 fd"上，index 也只对这个 fd 有意义。
    //   2026-10-21 实测：同一个 /dev/video22 再 open 一个 fd 去 QBUF / REQBUFS → EBUSY（队列已经有主人）；
    //   没 REQBUFS 就 QBUF、index 越界、同一块重复 QBUF → EINVAL
    if (xioctl(g_videoFd, VIDIOC_QBUF, &v4l2Buf) == -1) {
        LOGE(kTag, "     VIDIOC_QBUF %d: %s", index, strerror(errno));
        return false;
    }
    return true;
}

/**
 * @brief VIDIOC_DQBUF：从驱动取出一块已经写好画面的缓冲区（出队），返回它的序号
 *
 * 缓冲区轮转里的"取出来"那一步：缓冲区从驱动的"已经写好"队列到了"程序手里"，驱动不会再往里写，
 * 直到调用 queue_buffer() 还回去。驱动那边还没有写好的帧时，会一直阻塞等（30fps 时最多等约 33ms）。
 *
 * @param[out] bytesUsed  传出这一帧实际用了多少字节（1080p NV12 是 3,110,400，比缓冲区长度 3,133,440 小）；
 *                        不关心时传 nullptr（比如跳过开头的帧时）
 * @return 成功返回缓冲区序号（0 ～ kBufCount-1）；ioctl 失败或序号越界返回 -1
 *
 * @note 帧数据不在返回值里，也不在 bytesUsed 里，而是在驱动已经写好的 g_bufs[序号] 那块内存里：
 *       CPU 用 g_bufs[序号].start 读，编码器用 g_bufs[序号].mppBuf 读（零拷贝）。
 * @note 一个函数只能 return 一个值，所以"第几块"用 return、"用了多少字节"用指针带回来（输出参数，
 *       见 doc/C++小坑和常见写法.md 最后一节）。
 * @note 越界检查是 >=（camera_source.c 第 388 行写成了 >，差 1）。
 */
int dequeue_buffer(uint32_t* bytesUsed) {
    v4l2_plane planes[1] = {};
    v4l2_buffer buf      = {};
    buf.type             = kBufType;
    buf.memory           = V4L2_MEMORY_MMAP;
    buf.m.planes         = planes;
    buf.length           = 1;
    // ioctl(谁, 干什么, 表格)：
    //   g_videoFd    = 谁：从 /dev/video22 背后的摄像头驱动取
    //   VIDIOC_DQBUF = 干什么：命令号"取一块写好的 buffer"，videodev2.h 第 5130 行 _IOWR('V', 17, struct v4l2_buffer)
    //   &buf         = 表格：进去时只填 type / memory / planes；驱动把 index、bytesused、sequence 写回来
    // 两者的隐形关系：和 QBUF 一样，只能对做了 REQBUFS 的同一个 fd 调用（别的 fd → EBUSY）。
    //   fd 是阻塞打开的（没加 O_NONBLOCK），所以没有写好的帧时会在这里一直等
    if (xioctl(g_videoFd, VIDIOC_DQBUF, &buf) == -1) {
        LOGE(kTag, "    VIDIOC_DQBUF: %s", strerror(errno));
        return -1;
    }

    if (buf.index >= kBufCount) {
        LOGE(kTag, "    VIDIOC_DQBUF 返回的序号 %u 越界", buf.index);
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
        LOGE(kTag, "    open %s failed: %s", kDevice, strerror(errno));
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
    fmt.fmt.pix_mp.num_planes  = 1; // 这个格式实际用了几个平面（几块独立内存）。
    // set format by manual
    V4L2_CHECK(VIDIOC_S_FMT, &fmt);
    // get again because the drive maybe change it
    V4L2_CHECK(VIDIOC_G_FMT, &fmt);
    const int width          = static_cast<int>(fmt.fmt.pix_mp.width);
    const int height         = static_cast<int>(fmt.fmt.pix_mp.height);
    const int bytesPerLine   = static_cast<int>(fmt.fmt.pix_mp.plane_fmt[0].bytesperline);
    const uint32_t sizeImage = fmt.fmt.pix_mp.plane_fmt[0].sizeimage;
    LOGI(kTag, "    format: %dx%d NV12, planes %u, bytesperline %d, sizeimage %u", width, height,
        fmt.fmt.pix_mp.num_planes, bytesPerLine, sizeImage);

    if (fmt.fmt.pix_mp.pixelformat != V4L2_PIX_FMT_NV12 || fmt.fmt.pix_mp.num_planes != 1 || bytesPerLine <= 0) {
        LOGE(kTag, "    驱动没有给 1 个 plane 的 NV12");
        return -1;
    }
    // step 3======== require 4 block buffer
    v4l2_requestbuffers req = {};
    req.count               = kBufCount;
    req.type                = kBufType;
    req.memory              = V4L2_MEMORY_MMAP;
    V4L2_CHECK(VIDIOC_REQBUFS, &req);
    if (req.count != kBufCount) {
        LOGE(kTag, "     要 %d 块缓冲区，驱动给了 %u 块", kBufCount, req.count);
        return -1;
    }
    LOGI(kTag, "    request %d block buffer in success", req.count);

    // 4: config pre buffer
    for (int i = 0; i < kBufCount; ++i) {
        // 4.1
        v4l2_plane planes[1] = {};
        v4l2_buffer buf      = {};
        buf.type             = kBufType;
        buf.memory           = V4L2_MEMORY_MMAP;
        buf.index            = i;
        buf.m.planes         = planes;
        buf.length           = 1;
        V4L2_CHECK(VIDIOC_QUERYBUF, &buf);

        // 4.2
        g_bufs[i].length = planes[0].length;
        g_bufs[i].start =
            mmap(nullptr, planes->length, PROT_READ | PROT_WRITE, MAP_SHARED, g_videoFd, planes[0].m.mem_offset);
        if (g_bufs[i].start == MAP_FAILED) {
            LOGE(kTag, "     mmap buffer %d: %s", i, strerror(errno));
            return -1;
        }

        // 4.3: VIDIOC_EXPBUF —— 把第 i 块缓冲区"导出"成一个 dma_buf fd，好交给编码器用（零拷贝的关键一步）
        //
        // 为什么要这一步：
        //   REQBUFS 时内存已经由摄像头驱动分配好了（4.2 的 mmap 也是这块内存），但 mmap 拿到的 start
        //   只是"本程序 CPU 能用的地址"，编码器是另一块硬件，看不懂这个地址。
        //   EXPBUF 让驱动给这块内存发一把"钥匙"——dma_buf fd。拿着这个 fd，别的硬件（MPP 编码器、RGA、
        //   显示）就能找到同一块物理内存，直接读，不用 CPU 复制。
        //
        // 它不做的事：不分配新内存、不复制数据。导出前后，摄像头写的、mmap 看到的、编码器读的，都是同一块内存。
        //
        // 要填的字段（填表 → 交给驱动 → 驱动把结果写回 exp.fd）：
        //   type  = kBufType  缓冲区类型，和 REQBUFS / QUERYBUF 填的一样（多平面采集）
        //   index = i         导出第几块（0 ～ kBufCount-1）
        //   plane = 0         导出这块缓冲区的第几个 plane。NV12 在这里只有 1 个 plane，所以是 0
        //   flags = O_CLOEXEC 新 fd 的属性：本程序以后如果 exec 别的程序，这个 fd 自动关掉，不会泄露给子程序
        // 驱动返回：
        //   exp.fd            新的 dma_buf fd（实测 4 块分别是 12、13、14、15）
        //
        // 注意：这个 fd 是新打开的文件，用完要 close（clear_up() 里 close(g_buf.dmaFd)），否则 fd 泄漏
        v4l2_exportbuffer exp = {};
        exp.type              = kBufType;
        exp.index             = i;
        exp.plane             = 0;
        exp.flags             = O_CLOEXEC;
        V4L2_CHECK(VIDIOC_EXPBUF, &exp);
        // 中文：驱动返回的 dma-buf 文件描述符（输出参数）
        g_bufs[i].dmaFd = exp.fd; // 存起来：4.4 交给 mpp_buffer_import，clear_up() 里 close

        // 4.4
        MppBufferInfo bufInfo = {};
        bufInfo.type          = MPP_BUFFER_TYPE_EXT_DMA;
        bufInfo.fd            = exp.fd;
        bufInfo.size          = planes[0].length;
        bufInfo.index         = i;
        // 把外部内存（一般是 dma_buf fd）包装成一个 MppBuffer
        if (mpp_buffer_import(&g_bufs[i].mppBuf, &bufInfo) != MPP_OK) {
            LOGE(kTag, "     mpp_buffer_import buffer %d", i);
            return -1;
        }
        LOGI(kTag, "      buffer %d: length %zu, dma_buf fd %d", i, g_bufs[i].length, g_bufs[i].dmaFd);
    }


    // 5: 开拍之前，先把 4 块空 buffer 全部交给驱动
    //
    // 为什么要这一步：REQBUFS 只是把 4 块 buffer 造出来，造好时它们在【程序手里】，不在驱动手里。
    // 摄像头只会往"已经交给驱动的空 buffer"里写画面，驱动手里一块都没有的话，开拍后没地方写。
    //
    // 打个比方（食堂打饭）：REQBUFS = 买了 4 个盘子，堆在你桌上；QBUF = 把一个空盘子递到打饭窗口；
    // STREAMON = 阿姨开始打饭，只往窗口里的盘子盛。所以开饭前要把 4 个盘子都递过去。
    //
    // 为什么是循环：QBUF 一次只能交一块（表格里只填一个 index），4 块就调用 4 次，i 就是第几块（0～3）。
    //
    // 这个循环只在开始时做一次。之后主循环里每一帧都是：DQBUF 取一块 → encode → QBUF 还这一块，4 块一直转圈。
    // 交得越少越容易丢帧：2026-10-21 实测只有 1 块 buffer 时，程序什么都不做也只收到 10fps（摄像头是 30fps）。
    for (int i = 0; i < kBufCount; ++i) {
        if (!queue_buffer(i)) {
            return -1;
        }
    }

    // STREAMON：让摄像头开始拍（"开饭"）。从这时起，ISP 每 33ms 往驱动手里的一块空 buffer 写一帧，
    // 写好的放进"已写好"队列，等 DQBUF 来取。对应的关闭是 clear_up() 里的 STREAMOFF。
    // 必须先 QBUF 再 STREAMON：顺序反了，开拍那一刻驱动手里没有空 buffer。
    //
    // 表格只有一个数：type = 启动哪一种队列（多平面采集）。命令号是 _IOW('V', 18, int)（videodev2.h 第 5140 行），
    // W = 只往驱动里写，驱动不往回填；但 ioctl 的第 3 个参数必须是地址，所以要先放进变量 type，再传 &type。
    // 开拍时驱动会一路通知到摄像头芯片：dmesg 里能看到 "imx415 3-001a: s_stream: 1"，STREAMOFF 时是 "s_stream: 0"。
    // 常见失败：video31（没接摄像头的 ISP1）→ EPERM；相机 App 占着 video22 → EBUSY（在更早的 S_FMT / REQBUFS 就会报）。
    v4l2_buf_type type = kBufType;
    V4L2_CHECK(VIDIOC_STREAMON, &type);
    g_streaming = true;

    // 6:
    EncoderConfig cfg;
    cfg.rotation  = 3; // 摄像头侧装，逆时针转 270° 才正
    cfg.width     = width;
    cfg.height    = height;
    cfg.horStride = bytesPerLine;                                       // 1920
    cfg.verStride = static_cast<int>(sizeImage / bytesPerLine * 2 / 3); // 3110400 / 1920 * 2 / 3 = 1080
    MppEncoder encoder;
    if (!encoder.init(cfg)) {
        return -1;
    }

    LOGI(kTag, "      encoder ready: %dx%d stride %dx%d", width, height, encoder.hor_stride(), encoder.ver_stride());

    g_out = fopen(kOutputFilePath, "wb");
    if (!g_out) {
        LOGE(kTag, "      fopen %s: %s", kOutputFilePath, strerror(errno));
        return -1;
    }
    std::vector<uint8_t> header;
    if (!encoder.get_header(header)) {
        LOGE(kTag, "      get_header in fail");
        return -1;
    }
    if (fwrite(header.data(), 1, header.size(), g_out) != header.size()) { // 写不进去（比如磁盘满了）就别往下编了
        LOGE(kTag, "    fwrite header failed: %s", strerror(errno));
        return -1;
    }
    LOGI(kTag, "      header: %zu bytes", header.size());

    // 7:  drop frame
    for (int i = 0; i < kSkipFrames; ++i) {
        int index = dequeue_buffer(nullptr);
        if (index < 0 || !queue_buffer(index)) {
            return -1;
        }
    }

    // 8: encode packet
    EncodedPacket packet;
    std::size_t totalBytes = header.size();
    for (int i = 0; i < kFrames; ++i) {
        uint32_t bytesUsed = 0;
        int index          = dequeue_buffer(&bytesUsed);
        // Note : ioctl 失败或序号越界返回 -1
        if (index < 0) {
            return -1;
        }
        // ⭐: zero-copy: 编码器直接读取摄像头刚写好的这块内存
        if (!encoder.encode(g_bufs[index].mppBuf, false, packet)) {
            queue_buffer(index);
            return -1;
        }
        // ⭐ encode() 返回时硬件已经读完这一帧，现在才能还给摄像头
        if (!queue_buffer(index)) {
            return -1;
        }

        if (!packet.data.empty()) {
            if (fwrite(packet.data.data(), 1, packet.data.size(), g_out) != packet.data.size()) {
                LOGE(kTag, "    fwrite packet failed: %s", strerror(errno));
                return -1;
            }
            totalBytes += packet.data.size();
        }
        if (i % 30 == 0) {
            LOGI(kTag, "      frame %3d: buffer %d, in %u bytes, out %zu bytes%s", i, index, bytesUsed,
                packet.data.size(), packet.isKeyFrame ? " (I)" : "");
        }
    }
    // 9: set eos
    do {
        if (!encoder.encode(nullptr, true, packet)) {
            return -1;
        }
        if (!packet.data.empty()) {
            if (fwrite(packet.data.data(), 1, packet.data.size(), g_out) != packet.data.size()) {
                LOGE(kTag, "    fwrite packet failed: %s", strerror(errno));
                return -1;
            }
            totalBytes += packet.data.size();
        }

    } while (!packet.eos);

    LOGI(kTag, "✅✅✅✅✅Done: %d frames, %zu bytes -> %s", kFrames, totalBytes, kOutputFilePath);
    return 0;
}

static void clear_up() {
    if (g_streaming) {
        v4l2_buf_type type = kBufType;
        xioctl(g_videoFd, VIDIOC_STREAMOFF, &type);
        g_streaming = false;
    }

    for (auto& g_buf : g_bufs) {
        if (g_buf.mppBuf) {
            mpp_buffer_put(g_buf.mppBuf);
            g_buf.mppBuf = nullptr;
        }
        if (g_buf.start != MAP_FAILED) {
            munmap(g_buf.start, g_buf.length);
            g_buf.start = MAP_FAILED;
        }
        if (g_buf.dmaFd >= 0) {
            close(g_buf.dmaFd);
            g_buf.dmaFd = -1;
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
