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
//
// main() 只有 5 步，每一步是一个函数（详细讲解见 readme.md 第 7、9 节）：
//   第 1 步 alloc_touched_malloc()       准备 ① 和 memcpy 的目的地
//   第 2 步 alloc_dma_heap()             准备 ②③
//   第 3 步 open_camera_and_map_buffers() + start_and_hold_one_frame()   准备 ④
//   第 4 步 run_all_measurements()       测速
//   第 5 步 close_camera() / free_dma_heap() / free()                    收尾
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

static const char *kDevicePath = "/dev/video22";
static const int kWidth = 1920;
static const int kHeight = 1080;
static const int kBufCount = 4;
static const int kSkipFrameCount = 10; // 先跳过几帧，让 3A 稳定、让 buffer 里是真的画面
static const int kRepeatCount = 30; // 每项测试重复几次，取平均
static const size_t kFrameSizeBytes = 3110400; // 一帧 1080p NV12 = 1920 × 1080 × 1.5
static const v4l2_buf_type kBufType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

// 防止编译器发现"结果没人用"而把读内存的代码优化掉：结果都写进这个 volatile 变量
static volatile uint64_t g_sink = 0;

/** 从 dma_heap 申请到的一块内存：一把"钥匙"（dma_buf fd）+ CPU 能用的地址 */
struct DmaHeapBuffer {
    int dmaBufFd = -1; // 这块内存的钥匙（ioctl 申请时内核新开的 fd）
    void *cpuAddr = nullptr; // mmap 以后本进程 CPU 能用的地址
    size_t sizeBytes = 0;
};

/** 摄像头这一边要记住的东西：设备 fd、4 块 buffer 的地址、手里拿着不还的那一帧 */
struct CameraSession {
    int videoFd = -1;
    void *bufAddrs[kBufCount] = {}; // 4 块 buffer mmap 以后的 CPU 地址
    size_t bufLengths[kBufCount] = {}; // 4 块 buffer 的长度（munmap 要用）
    v4l2_plane heldPlanes[1] = {}; // heldBuf.m.planes 指向这里
    v4l2_buffer heldBuf = {}; // DQBUF 取出来、测试期间不还的那一帧
    bool isStreaming = false;
    bool isHoldingFrame = false;
};

/**
 * @brief 取当前时间，单位毫秒（steady_clock：只往前走，适合量时间差）
 * @return 从某个固定起点开始的毫秒数，只用来相减
 */
static double now_ms() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

/**
 * @brief 打印一个 fd 背后是什么东西（读 /proc/self/fd/<fd> 这个链接）
 * @param[in] label 打印时的前缀，说明这是哪个 fd
 * @param[in] fd    要查的文件描述符
 * @note /proc/self/fd/ 下面每个数字都是一个链接，指向这个 fd 打开的东西：
 *       普通设备文件显示路径（如 /dev/dma_heap/system），dma_buf 显示 /dmabuf:...
 */
static void print_fd_target(const char *label, int fd) {
    char linkPath[64];
    char targetPath[256] = {};
    snprintf(linkPath, sizeof(linkPath), "/proc/self/fd/%d", fd);
    const ssize_t targetLength = readlink(linkPath, targetPath, sizeof(targetPath) - 1);
    printf("    %-26s fd %d -> %s\n", label, fd, targetLength > 0 ? targetPath : "(unknown)");
}

/**
 * @brief malloc 一块内存，并把每个字节都写一遍
 * @param[in] sizeBytes 要多大
 * @param[in] fillByte  写进去的值（随便一个数）
 * @return 成功返回地址；失败返回 nullptr
 * @note 为什么要写一遍：Linux 的 malloc 只是"答应"给你内存，第一次写进去时才真正分配物理页。
 *       先写一遍，测速时就不会把"分配内存"的时间也算进去。
 */
static void *alloc_touched_malloc(size_t sizeBytes, int fillByte) {
    void *addr = malloc(sizeBytes);
    if (addr) {
        memset(addr, fillByte, sizeBytes);
    }
    return addr;
}

/**
 * @brief 从 dma_heap 申请一块 dma_buf，并 mmap 到本进程，得到 CPU 地址
 *
 * 一共 3 步，用到两个不同的 fd（图见 readme.md 第 9 节）：
 *   第 1 步 open(heapPath)                  → heapFd：dma_heap 这个"窗口"的 fd，只用来提申请
 *   第 2 步 ioctl(heapFd, ALLOC, &allocData) → 内核分配内存，新开一个 fd 写回 allocData.fd：这块内存的"钥匙"
 *   第 3 步 mmap(allocData.fd)              → 用钥匙把这块内存映射进本进程，得到 CPU 地址
 * 申请完 heapFd 就可以关：关掉窗口不影响已经拿到的钥匙。
 *
 * @param[in]  heapPath  堆的设备文件，例如 "/dev/dma_heap/system"（cached）、"/dev/dma_heap/system-uncached"
 * @param[in]  sizeBytes 要多大
 * @param[out] heapBuf   传出钥匙（dmaBufFd）和 CPU 地址（cpuAddr），用完调 free_dma_heap()
 * @return 成功返回 true；失败打印原因并返回 false
 * @note M6 里 MPP 申请 g_copyBuf，背后走的也是这条路（实测来自 system 堆）
 */
static bool alloc_dma_heap(const char *heapPath, size_t sizeBytes, DmaHeapBuffer &heapBuf) {
    printf("  %s:\n", heapPath);

    // 第 1 步：打开 dma_heap 这个"窗口"。heapFd 代表的是"分配器"，不是内存
    const int heapFd = open(heapPath, O_RDWR | O_CLOEXEC);
    if (heapFd < 0) {
        printf("    open: %s\n", strerror(errno));
        return false;
    }
    print_fd_target("step 1 open  -> heapFd", heapFd);

    // 第 2 步：对窗口提申请（填表：要多大、新 fd 要什么属性），内核分配好内存以后，
    // 新开一个 fd 代表这块内存，写回 allocData.fd（和 V4L2 的 ioctl 一样：填表 → 交表 → 看回执）
    dma_heap_allocation_data allocData = {};
    allocData.len = sizeBytes;
    allocData.fd_flags = O_RDWR | O_CLOEXEC;
    if (ioctl(heapFd, DMA_HEAP_IOCTL_ALLOC, &allocData) != 0) {
        printf("    DMA_HEAP_IOCTL_ALLOC: %s\n", strerror(errno));
        close(heapFd);
        return false;
    }
    const int dmaBufFd = static_cast<int>(allocData.fd);
    print_fd_target("step 2 ioctl -> allocData.fd", dmaBufFd);

    // 申请完就关窗口：内存由 dmaBufFd 代表，和 heapFd 已经没有关系了
    close(heapFd);

    // 第 3 步：用钥匙 dmaBufFd 把这块内存映射进本进程，得到 CPU 能直接读写的地址。
    // 和 M2 里 mmap(g_videoFd, mem_offset) 是同一个函数：mmap 的意思就是"把这个 fd 代表的东西，放到我的地址空间里"
    void *cpuAddr = mmap(nullptr, sizeBytes, PROT_READ | PROT_WRITE, MAP_SHARED, dmaBufFd, 0);
    if (cpuAddr == MAP_FAILED) {
        printf("    mmap: %s\n", strerror(errno));
        close(dmaBufFd);
        return false;
    }
    printf("    %-26s cpuAddr %p\n", "step 3 mmap  -> cpuAddr", cpuAddr);

    heapBuf.dmaBufFd = dmaBufFd;
    heapBuf.cpuAddr = cpuAddr;
    heapBuf.sizeBytes = sizeBytes;
    return true;
}

/**
 * @brief 释放 alloc_dma_heap() 申请的内存：先 munmap，再 close 钥匙（和申请的顺序相反）
 * @param[in,out] heapBuf 要释放的内存；释放后字段恢复成初始值
 */
static void free_dma_heap(DmaHeapBuffer &heapBuf) {
    if (heapBuf.cpuAddr) {
        munmap(heapBuf.cpuAddr, heapBuf.sizeBytes);
        heapBuf.cpuAddr = nullptr;
    }
    if (heapBuf.dmaBufFd >= 0) {
        close(heapBuf.dmaBufFd);
        heapBuf.dmaBufFd = -1;
    }
}

/**
 * @brief 打开摄像头，申请 4 块 buffer，mmap 到本进程，全部 QBUF 交给驱动（和 day3_cam2h264 的前半段一样）
 * @param[out] camera 传出 videoFd 和 4 块 buffer 的地址、长度
 * @return 成功返回 true；失败打印原因并返回 false（已经打开的东西由 close_camera() 收拾）
 * @note 摄像头 buffer 的 mmap 用的是 videoFd + mem_offset（偏移量选第几块），
 *       dma_heap 的 mmap 用的是 dmaBufFd + 0（一个 fd 就只代表一块）。见 readme.md 第 9.4 节
 */
static bool open_camera_and_map_buffers(CameraSession &camera) {
    camera.videoFd = open(kDevicePath, O_RDWR | O_CLOEXEC);
    if (camera.videoFd < 0) {
        printf("open %s: %s\n", kDevicePath, strerror(errno));
        return false;
    }
    print_fd_target("videoFd", camera.videoFd);

    v4l2_format format = {};
    format.type = kBufType;
    format.fmt.pix_mp.width = kWidth;
    format.fmt.pix_mp.height = kHeight;
    format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    format.fmt.pix_mp.num_planes = 1;
    if (ioctl(camera.videoFd, VIDIOC_S_FMT, &format) != 0) {
        printf("VIDIOC_S_FMT: %s（相机 App 或者别的程序占着摄像头？）\n", strerror(errno));
        return false;
    }

    v4l2_requestbuffers requestBufs = {};
    requestBufs.count = kBufCount;
    requestBufs.type = kBufType;
    requestBufs.memory = V4L2_MEMORY_MMAP;
    if (ioctl(camera.videoFd, VIDIOC_REQBUFS, &requestBufs) != 0) {
        printf("VIDIOC_REQBUFS: %s\n", strerror(errno));
        return false;
    }

    for (int bufIndex = 0; bufIndex < kBufCount; ++bufIndex) {
        v4l2_plane planes[1] = {};
        v4l2_buffer v4l2Buf = {};
        v4l2Buf.type = kBufType;
        v4l2Buf.memory = V4L2_MEMORY_MMAP;
        v4l2Buf.index = bufIndex;
        v4l2Buf.m.planes = planes;
        v4l2Buf.length = 1;
        if (ioctl(camera.videoFd, VIDIOC_QUERYBUF, &v4l2Buf) != 0) {
            printf("VIDIOC_QUERYBUF %d: %s\n", bufIndex, strerror(errno));
            return false;
        }
        camera.bufLengths[bufIndex] = planes[0].length;
        camera.bufAddrs[bufIndex] = mmap(nullptr, planes[0].length, PROT_READ | PROT_WRITE, MAP_SHARED, camera.videoFd,
                                         planes[0].m.mem_offset);
        if (camera.bufAddrs[bufIndex] == MAP_FAILED) {
            camera.bufAddrs[bufIndex] = nullptr;
            printf("mmap camera buffer %d: %s\n", bufIndex, strerror(errno));
            return false;
        }
        printf("    camera buffer %d: mmap(videoFd %d, mem_offset 0x%08x) -> cpuAddr %p\n", bufIndex, camera.videoFd,
               planes[0].m.mem_offset, camera.bufAddrs[bufIndex]);
        if (ioctl(camera.videoFd, VIDIOC_QBUF, &v4l2Buf) != 0) {
            printf("VIDIOC_QBUF %d: %s\n", bufIndex, strerror(errno));
            return false;
        }
    }
    return true;
}

/**
 * @brief STREAMON，跳过前 kSkipFrameCount 帧（取到就还），再取一帧拿在手里不还
 * @param[in,out] camera 传入已经 map 好的摄像头；传出手里那一帧（heldBuf）
 * @return 成功返回 true；失败打印原因并返回 false
 * @note 为什么拿着不还：还回去摄像头就会往这块里写新画面，一边读一边写，测速不准。
 *       拿在手里，测试期间这块内存是安静的。close_camera() 里再还。
 */
static bool start_and_hold_one_frame(CameraSession &camera) {
    int bufType = kBufType;
    if (ioctl(camera.videoFd, VIDIOC_STREAMON, &bufType) != 0) {
        printf("VIDIOC_STREAMON: %s\n", strerror(errno));
        return false;
    }
    camera.isStreaming = true;

    for (int frameIndex = 0; frameIndex <= kSkipFrameCount; ++frameIndex) {
        camera.heldBuf = {};
        camera.heldBuf.type = kBufType;
        camera.heldBuf.memory = V4L2_MEMORY_MMAP;
        camera.heldBuf.m.planes = camera.heldPlanes;
        camera.heldBuf.length = 1;
        if (ioctl(camera.videoFd, VIDIOC_DQBUF, &camera.heldBuf) != 0) {
            printf("VIDIOC_DQBUF: %s\n", strerror(errno));
            return false;
        }
        const bool isLastFrame = (frameIndex == kSkipFrameCount);
        if (!isLastFrame) {
            ioctl(camera.videoFd, VIDIOC_QBUF, &camera.heldBuf); // 前 10 帧：取到就还
        }
    }
    camera.isHoldingFrame = true; // 第 11 帧：拿着不还
    printf("    holding camera buffer %u (bytesused %u)\n", camera.heldBuf.index, camera.heldPlanes[0].bytesused);
    return true;
}

/**
 * @brief 收尾：还掉手里那一帧 → STREAMOFF → munmap 4 块 buffer → 关 videoFd（和打开的顺序相反）
 * @param[in,out] camera 要关闭的摄像头；只收拾已经打开 / 映射过的东西，中途失败时也能安全调用
 */
static void close_camera(CameraSession &camera) {
    if (camera.isHoldingFrame) {
        ioctl(camera.videoFd, VIDIOC_QBUF, &camera.heldBuf);
        camera.isHoldingFrame = false;
    }
    if (camera.isStreaming) {
        int bufType = kBufType;
        ioctl(camera.videoFd, VIDIOC_STREAMOFF, &bufType);
        camera.isStreaming = false;
    }
    for (int bufIndex = 0; bufIndex < kBufCount; ++bufIndex) {
        if (camera.bufAddrs[bufIndex]) {
            munmap(camera.bufAddrs[bufIndex], camera.bufLengths[bufIndex]);
            camera.bufAddrs[bufIndex] = nullptr;
        }
    }
    if (camera.videoFd >= 0) {
        close(camera.videoFd);
        camera.videoFd = -1;
    }
}

/**
 * @brief 测一块内存的读速度：memcpy 和逐 8 字节读求和各测 kRepeatCount 次，打印平均值
 * @param[in] name    这块内存的名字，打印用
 * @param[in] srcAddr 要读的内存的 CPU 地址（至少 kFrameSizeBytes 字节）
 * @param[in] dstAddr memcpy 的目的地（malloc 的普通内存）
 */
static void measure_read_speed(const char *name, const void *srcAddr, void *dstAddr) {
    // 先做一次热身：把页表、TLB 准备好，避免第一次的额外开销算进去
    memcpy(dstAddr, srcAddr, kFrameSizeBytes);

    // 读法 1：memcpy 整块拷走（C 库的 memcpy 会用 NEON 一次读很多字节，是最快的读法）
    double startMs = now_ms();
    for (int repeatIndex = 0; repeatIndex < kRepeatCount; ++repeatIndex) {
        memcpy(dstAddr, srcAddr, kFrameSizeBytes);
        g_sink = g_sink + static_cast<uint8_t *>(dstAddr)[repeatIndex];
    }
    const double memcpyMs = (now_ms() - startMs) / kRepeatCount;

    // 读法 2：一次读 8 字节、加起来（模拟 CPU 自己处理像素：一边读一边算）
    const uint64_t *words = static_cast<const uint64_t *>(srcAddr);
    const size_t wordCount = kFrameSizeBytes / sizeof(uint64_t);
    startMs = now_ms();
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

/**
 * @brief 4 块内存各测一遍，测两轮，第二轮倒过来
 * @param[in] mallocAddr       ① malloc 的内存
 * @param[in] cachedHeapAddr   ② dma_heap system
 * @param[in] uncachedHeapAddr ③ dma_heap system-uncached
 * @param[in] cameraAddr       ④ 摄像头手里那一帧的 buffer
 * @param[in] dstAddr          memcpy 的目的地
 * @note 为什么测两轮、倒序：第一个上场的总吃亏（CPU 刚开始干活时频率还低，要过一会儿才升上去）。
 *       2026-10-23 实测：第一轮 malloc 慢了 2～3 倍，第二轮就正常了
 */
static void run_all_measurements(const void *mallocAddr, const void *cachedHeapAddr, const void *uncachedHeapAddr,
                                 const void *cameraAddr, void *dstAddr) {
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
}

int main() {
    // 第 1 步：准备 ①，以及 memcpy 的目的地（4 块内存都往这里拷，公平）
    printf("step 1: malloc\n");
    void *dstAddr = alloc_touched_malloc(kFrameSizeBytes, 0);
    void *mallocAddr = alloc_touched_malloc(kFrameSizeBytes, 0x5a);
    if (!dstAddr || !mallocAddr) {
        printf("malloc failed\n");
        return 1;
    }

    // 第 2 步：准备 ②③ 两块参照内存
    printf("step 2: dma_heap\n");
    DmaHeapBuffer cachedHeapBuf;
    DmaHeapBuffer uncachedHeapBuf;
    if (!alloc_dma_heap("/dev/dma_heap/system", kFrameSizeBytes, cachedHeapBuf) ||
        !alloc_dma_heap("/dev/dma_heap/system-uncached", kFrameSizeBytes, uncachedHeapBuf)) {
        return 1;
    }
    memset(cachedHeapBuf.cpuAddr, 0x5a, kFrameSizeBytes);
    memset(uncachedHeapBuf.cpuAddr, 0x5a, kFrameSizeBytes);

    // 第 3 步：准备 ④ 摄像头的 buffer：打开、map，取一帧拿在手里
    printf("step 3: camera\n");
    CameraSession camera;
    if (!open_camera_and_map_buffers(camera) || !start_and_hold_one_frame(camera)) {
        close_camera(camera);
        return 1;
    }
    const void *cameraAddr = camera.bufAddrs[camera.heldBuf.index];

    // 第 4 步：测速
    printf("\nstep 4: ");
    run_all_measurements(mallocAddr, cachedHeapBuf.cpuAddr, uncachedHeapBuf.cpuAddr, cameraAddr, dstAddr);

    // 第 5 步：收尾（和申请的顺序相反）
    close_camera(camera);
    free_dma_heap(uncachedHeapBuf);
    free_dma_heap(cachedHeapBuf);
    free(mallocAddr);
    free(dstAddr);
    return 0;
}
