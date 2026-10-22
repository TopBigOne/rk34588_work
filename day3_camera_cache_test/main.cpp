// day3_camera_cache_test：摄像头的 buffer 对 CPU 来说是 cached 还是 uncached？
//
// 做法：用同样的方式读 3,110,400 字节（一帧 1080p NV12），只换"从哪块内存读"，比较速度：
//   ① malloc 的普通内存               → 一定是 cached（"快"的标准）
//   ② dma_heap "system"               → cached 的 dma_buf（和 M6 的 g_copyBuf 同一种）
//   ③ dma_heap "system-uncached"      → 一定是 uncached（"慢"的标准）
//   ④ 摄像头的 buffer（mmap 的 start）→ 不知道，这就是要测的
// ④ 的速度接近 ①②，说明是 cached；接近 ③，说明是 uncached。
//
// 每块内存测两种读法：
//   memcpy        ：整块拷到 malloc 内存（M6 拷贝版本就是这么用的）
//   逐 8 字节读求和：模拟 CPU 自己一个一个处理像素（以后做 OSD、图像分析时的读法）
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <linux/dma-heap.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static const char* kDevicePath       = "/dev/video22";
static const int kWidth              = 1920;
static const int kHeight             = 1080;
static const int kBufCount           = 4;
static const int kSkipFrameCount     = 10;  // 先跳过几帧，让 3A 稳定、让 buffer 里是真的画面
static const int kRepeatCount        = 30;  // 每项测试重复几次，取平均
static const size_t kFrameSizeBytes  = 3110400; // 一帧 1080p NV12 = 1920 × 1080 × 1.5
static const v4l2_buf_type kBufType  = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

// 防止编译器发现"结果没人用"而把读内存的代码优化掉：结果都写进这个 volatile 变量
static volatile uint64_t g_sink = 0;

/**
 * @brief 取当前时间，单位毫秒（steady_clock：只往前走，适合量时间差）
 * @return 从某个固定起点开始的毫秒数，只用来相减
 */
static double now_ms() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

/**
 * @brief 从 dma_heap 申请一块 dma_buf，并 mmap 到本进程，得到 CPU 地址
 * @param[in]  heapPath  堆的设备文件，例如 "/dev/dma_heap/system"（cached）、"/dev/dma_heap/system-uncached"
 * @param[in]  sizeBytes 要多大
 * @param[out] dmaBufFd  传出 dma_buf 的 fd，用完要 close
 * @return 成功返回 CPU 地址；失败打印原因并返回 nullptr
 * @note dma_heap 是内核统一的"硬件可用内存"分配接口，M6 里 MPP 申请 g_copyBuf 实际也是走的它（实测来自 system 堆）
 */
static void* alloc_dma_heap(const char* heapPath, size_t sizeBytes, int* dmaBufFd) {
    int heapFd = open(heapPath, O_RDWR | O_CLOEXEC);
    if (heapFd < 0) {
        printf("open %s: %s\n", heapPath, strerror(errno));
        return nullptr;
    }
    dma_heap_allocation_data allocData = {};
    allocData.len                      = sizeBytes;
    allocData.fd_flags                 = O_RDWR | O_CLOEXEC;
    const int ret                      = ioctl(heapFd, DMA_HEAP_IOCTL_ALLOC, &allocData);
    close(heapFd); // 堆的 fd 只用来申请，申请完就可以关；分到的内存由 allocData.fd 代表
    if (ret != 0) {
        printf("DMA_HEAP_IOCTL_ALLOC %s: %s\n", heapPath, strerror(errno));
        return nullptr;
    }
    void* cpuAddr = mmap(nullptr, sizeBytes, PROT_READ | PROT_WRITE, MAP_SHARED, allocData.fd, 0);
    if (cpuAddr == MAP_FAILED) {
        printf("mmap %s: %s\n", heapPath, strerror(errno));
        close(allocData.fd);
        return nullptr;
    }
    *dmaBufFd = allocData.fd;
    return cpuAddr;
}

/**
 * @brief 测一块内存的读速度：memcpy 和逐 8 字节读求和各测 kRepeatCount 次，打印平均值
 * @param[in] name    这块内存的名字，打印用
 * @param[in] srcAddr 要读的内存的 CPU 地址（至少 kFrameSizeBytes 字节）
 * @param[in] dstAddr memcpy 的目的地（malloc 的普通内存）
 */
static void measure_read_speed(const char* name, const void* srcAddr, void* dstAddr) {
    // 先各做一次热身：把页表、TLB 准备好，避免第一次的额外开销算进去
    memcpy(dstAddr, srcAddr, kFrameSizeBytes);

    // 读法 1：memcpy 整块拷走（C 库的 memcpy 会用 NEON 一次读很多字节，是最快的读法）
    double startMs = now_ms();
    for (int repeatIndex = 0; repeatIndex < kRepeatCount; ++repeatIndex) {
        memcpy(dstAddr, srcAddr, kFrameSizeBytes);
        g_sink = g_sink + static_cast<uint8_t*>(dstAddr)[repeatIndex];
    }
    const double memcpyMs = (now_ms() - startMs) / kRepeatCount;

    // 读法 2：一次读 8 字节、加起来（模拟 CPU 自己处理像素：一边读一边算）
    const uint64_t* words  = static_cast<const uint64_t*>(srcAddr);
    const size_t wordCount = kFrameSizeBytes / sizeof(uint64_t);
    startMs                = now_ms();
    for (int repeatIndex = 0; repeatIndex < kRepeatCount; ++repeatIndex) {
        uint64_t sum = 0;
        for (size_t wordIndex = 0; wordIndex < wordCount; ++wordIndex) {
            sum += words[wordIndex];
        }
        g_sink = g_sink + sum;
    }
    const double readSumMs = (now_ms() - startMs) / kRepeatCount;

    // GB/s = 字节数 ÷ 秒 ÷ 1e9 = 字节数 ÷ (毫秒 × 1e6)
    printf("  %-34s memcpy %6.2f ms (%5.2f GB/s)   read-sum %7.2f ms (%5.2f GB/s)\n", name, memcpyMs,
        kFrameSizeBytes / (memcpyMs * 1e6), readSumMs, kFrameSizeBytes / (readSumMs * 1e6));
}

int main() {
    // malloc 的目的地 + ① 的来源。先写一遍：让内核真正分配物理页（第一次写才分配），不让分配时间算进测试
    void* dstAddr    = malloc(kFrameSizeBytes);
    void* mallocAddr = malloc(kFrameSizeBytes);
    if (!dstAddr || !mallocAddr) {
        printf("malloc failed\n");
        return 1;
    }
    memset(dstAddr, 0, kFrameSizeBytes);
    memset(mallocAddr, 0x5a, kFrameSizeBytes);

    // ②③ 两块参照内存
    int cachedHeapFd        = -1;
    int uncachedHeapFd      = -1;
    void* cachedHeapAddr    = alloc_dma_heap("/dev/dma_heap/system", kFrameSizeBytes, &cachedHeapFd);
    void* uncachedHeapAddr  = alloc_dma_heap("/dev/dma_heap/system-uncached", kFrameSizeBytes, &uncachedHeapFd);
    if (!cachedHeapAddr || !uncachedHeapAddr) {
        return 1;
    }
    memset(cachedHeapAddr, 0x5a, kFrameSizeBytes);
    memset(uncachedHeapAddr, 0x5a, kFrameSizeBytes);

    // ④ 摄像头的 buffer：和 day3_cam2h264 一样的流程，取到一帧以后先不还，测完再还
    int videoFd = open(kDevicePath, O_RDWR | O_CLOEXEC);
    if (videoFd < 0) {
        printf("open %s: %s\n", kDevicePath, strerror(errno));
        return 1;
    }
    v4l2_format format            = {};
    format.type                   = kBufType;
    format.fmt.pix_mp.width       = kWidth;
    format.fmt.pix_mp.height      = kHeight;
    format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    format.fmt.pix_mp.num_planes  = 1;
    v4l2_requestbuffers requestBufs = {};
    requestBufs.count               = kBufCount;
    requestBufs.type                = kBufType;
    requestBufs.memory              = V4L2_MEMORY_MMAP;
    if (ioctl(videoFd, VIDIOC_S_FMT, &format) != 0 || ioctl(videoFd, VIDIOC_REQBUFS, &requestBufs) != 0) {
        printf("S_FMT / REQBUFS: %s（相机 App 占着摄像头？）\n", strerror(errno));
        return 1;
    }
    void* cameraAddrs[kBufCount]  = {};
    size_t cameraLengths[kBufCount] = {};
    for (int bufIndex = 0; bufIndex < kBufCount; ++bufIndex) {
        v4l2_plane planes[1] = {};
        v4l2_buffer v4l2Buf  = {};
        v4l2Buf.type         = kBufType;
        v4l2Buf.memory       = V4L2_MEMORY_MMAP;
        v4l2Buf.index        = bufIndex;
        v4l2Buf.m.planes     = planes;
        v4l2Buf.length       = 1;
        ioctl(videoFd, VIDIOC_QUERYBUF, &v4l2Buf);
        cameraLengths[bufIndex] = planes[0].length;
        cameraAddrs[bufIndex]   = mmap(nullptr, planes[0].length, PROT_READ | PROT_WRITE, MAP_SHARED, videoFd,
              planes[0].m.mem_offset);
        if (cameraAddrs[bufIndex] == MAP_FAILED) {
            printf("mmap camera buffer %d: %s\n", bufIndex, strerror(errno));
            return 1;
        }
        ioctl(videoFd, VIDIOC_QBUF, &v4l2Buf);
    }
    int bufType = kBufType;
    if (ioctl(videoFd, VIDIOC_STREAMON, &bufType) != 0) {
        printf("STREAMON: %s\n", strerror(errno));
        return 1;
    }

    // 跳过前几帧（取到就还），最后一帧取出来不还：测试期间摄像头不会往这块里写，读到的是稳定的画面
    v4l2_plane planes[1] = {};
    v4l2_buffer heldBuf  = {};
    for (int frameIndex = 0; frameIndex <= kSkipFrameCount; ++frameIndex) {
        heldBuf          = {};
        heldBuf.type     = kBufType;
        heldBuf.memory   = V4L2_MEMORY_MMAP;
        heldBuf.m.planes = planes;
        heldBuf.length   = 1;
        if (ioctl(videoFd, VIDIOC_DQBUF, &heldBuf) != 0) {
            printf("DQBUF: %s\n", strerror(errno));
            return 1;
        }
        if (frameIndex < kSkipFrameCount) {
            ioctl(videoFd, VIDIOC_QBUF, &heldBuf);
        }
    }
    const void* cameraAddr = cameraAddrs[heldBuf.index];
    printf("camera buffer %u held, bytesused %u\n\n", heldBuf.index, planes[0].bytesused);

    // 测两轮，第二轮倒过来：排除"先测的那项吃亏"（CPU 刚开始干活时频率还低，要过一会儿才升上去）
    printf("read %zu bytes, average of %d runs:\n", kFrameSizeBytes, kRepeatCount);
    printf("round 1:\n");
    measure_read_speed("1 malloc (cached, reference)", mallocAddr, dstAddr);
    measure_read_speed("2 dma_heap system (cached)", cachedHeapAddr, dstAddr);
    measure_read_speed("3 dma_heap system-uncached", uncachedHeapAddr, dstAddr);
    measure_read_speed("4 camera buffer (video22 mmap)", cameraAddr, dstAddr);
    printf("round 2 (reverse order):\n");
    measure_read_speed("4 camera buffer (video22 mmap)", cameraAddr, dstAddr);
    measure_read_speed("3 dma_heap system-uncached", uncachedHeapAddr, dstAddr);
    measure_read_speed("2 dma_heap system (cached)", cachedHeapAddr, dstAddr);
    measure_read_speed("1 malloc (cached, reference)", mallocAddr, dstAddr);

    // 收尾：还 buffer → 停止采集 → 解除映射 → 关 fd → 释放内存（和申请的顺序相反）
    ioctl(videoFd, VIDIOC_QBUF, &heldBuf);
    ioctl(videoFd, VIDIOC_STREAMOFF, &bufType);
    for (int bufIndex = 0; bufIndex < kBufCount; ++bufIndex) {
        munmap(cameraAddrs[bufIndex], cameraLengths[bufIndex]);
    }
    close(videoFd);
    munmap(uncachedHeapAddr, kFrameSizeBytes);
    munmap(cachedHeapAddr, kFrameSizeBytes);
    close(uncachedHeapFd);
    close(cachedHeapFd);
    free(mallocAddr);
    free(dstAddr);
    return 0;
}
