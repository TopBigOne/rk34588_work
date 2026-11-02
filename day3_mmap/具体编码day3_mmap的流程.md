# 具体编码 day3_mmap 的流程

> 这份文档是 [readme.md](readme.md) 第 2、3 节的**展开版**：每个实验拆成小步骤，每一步都给出可以照着写的代码。
>
> **代码状态（2026-10-27）**：
> - 下面的代码拼起来就是一个完整的 `main.cpp`。已经用 RK3588 工具链交叉编译通过（`-Wall -Wextra` 没有警告），`objdump -T` 检查也没有 `GLIBC_2.38`。
> - **还没上板**（板子没连）。所以"期望看到的输出"都是**我的猜测**，用 ⚠️ 标出来，上板以后要改成实测。
> - `day3_mmap/main.cpp` 还是 Hello World，没有动，留给你自己写。

---

## 0. 整体结构：一个程序，四个实验

### 0.1 怎么运行

一个程序，用命令行参数选做哪个实验：

```bash
cd /Users/dev/Documents/AV/rk_work/day3_mmap
./run_on_board_with_mac.sh m1     # 脚本会把 m1 原样传给板子上的程序
./run_on_board_with_mac.sh m2
./run_on_board_with_mac.sh m3
./run_on_board_with_mac.sh m4
```

### 0.2 main.cpp 从上到下的布局

> **2026-10-31 已拆成多个文件**：实际代码不再是一个 main.cpp，而是 `src/` 下按模块分开，main.cpp 只负责按参数选实验（目录见 readme.md 第 6 节）。下面的布局图和参考代码还是按"一个文件"写的，**函数内容一样，只是放的位置不同**：公共小工具 → `src/common/`、`src/camera/`、`src/dma_heap/`；每个实验 → `src/experiments/`。

```
main.cpp
├── 第 1 部分：头文件、常量、结构体
├── 第 2 部分：公共小工具（几个实验都要用）
│   ├── now_ms()                        计时                     ← M3
│   ├── parse_hex()                     十六进制文字 → 数字       ← M1
│   ├── print_whole_maps()              整个 maps 原样打印         ← M1
│   ├── print_maps_line_containing()    找到包含某地址的那一行     ← M1 M2 M4
│   ├── alloc_dma_heap() / free_dma_heap()                       ← M1 M3
│   └── open_camera_and_map_buffers() / close_camera()           ← M1 M2
├── 第 3 部分：M1  create_test_file()、run_m1_maps()
├── 第 4 部分：M2  run_m2_two_pointers()
├── 第 5 部分：M3  sync_dma_buf()、measure_sync_timing()、run_m3_sync()
├── 第 6 部分：M4  create_and_map_dumb()、fill_pixels_by_pitch()、check_export_to_dma_buf()、run_m4_dumb_buffer()
└── 第 7 部分：main()  按 argv[1] 选实验
```

### 0.3 建议的编写顺序

不要一口气写完。每写完一步就上板跑一次：

| 顺序 | 写什么 | 写完能跑什么 |
|:---:|---|---|
| 1 | 第 1 部分 + `parse_hex`、`print_whole_maps`、`print_maps_line_containing` + M1 的第 1 步 | M1 只有 malloc 和文件 |
| 2 | `open_camera_and_map_buffers`、`close_camera` + M1 第 2 步 | M1 加上摄像头 |
| 3 | `alloc_dma_heap`、`free_dma_heap` + M1 第 3 步 | M1 完整 |
| 4 | M2 | ⭐ 两个指针 |
| 5 | `now_ms` + M3 | sync 计时 |
| 6 | M4 | 显存 |

---

## 1. 第 1 部分：头文件、常量、结构体

```cpp
// day3_mmap：在 RK3588 上动手学 mmap
//
// 用法（板子上）：day3_mmap m1 | m2 | m3 | m4
//   m1  一个指针，到 /proc/self/maps 里查它是谁（malloc、文件、摄像头 buffer、dma_heap）
//   m2  同一块摄像头 buffer，两个指针：mmap(videoFd, mem_offset) 和 mmap(EXPBUF 的 fd, 0)
//   m3  自己调 DMA_BUF_IOCTL_SYNC，比较 cached 和 uncached 两种 dma_heap
//   m4  DRM dumb buffer：申请显存、mmap、写像素，看 pitch
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <drm/drm.h>            // M4：DRM_IOCTL_MODE_CREATE_DUMB 等（它里面会 include drm_mode.h）
#include <fcntl.h>
#include <linux/dma-buf.h>      // M3：DMA_BUF_IOCTL_SYNC
#include <linux/dma-heap.h>     // M1 M3：DMA_HEAP_IOCTL_ALLOC
#include <linux/videodev2.h>    // M1 M2：V4L2
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static const char *kVideoDevicePath = "/dev/video22";
static const char *kTestFilePath = "/tmp/day3_mmap_test.bin";
static const char *kDrmDevicePath = "/dev/dri/card0";
static const int kWidth = 1920;
static const int kHeight = 1080;
static const int kBufCount = 4;
static const size_t kMallocSizeBytes = 4 * 1024 * 1024;
static const size_t kTestFileSizeBytes = 4096;
static const size_t kHeapBufSizeBytes = 3 * 1024 * 1024;
static const int kSyncRepeatCount = 10;
static const v4l2_buf_type kBufType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

/** 从 dma_heap 申请到的一块内存：钥匙（dma_buf fd）+ mmap 得到的地址 */
struct DmaHeapBuffer {
    int dmaBufFd = -1;
    void *mmapAddress = nullptr;
    size_t sizeBytes = 0;
};

/** 摄像头这一边：设备 fd，和 4 块 buffer 的 mem_offset、长度、mmap 得到的地址 */
struct CameraBuffers {
    int videoFd = -1;
    uint32_t memOffsets[kBufCount] = {};
    size_t lengths[kBufCount] = {};
    void *mmapAddresses[kBufCount] = {};
};
```

> IDE 里（Mac 上的 clang）可能会提示 `'drm/drm.h' file not found`、`Unknown type name 'v4l2_buf_type'`，原因是 Mac 自己的 clang 看不到板子的 sysroot。只要 CLion 用 **RK3588-Debug** profile 编译能通过，就不用管这些提示。

---

## 2. 第 2 部分：公共小工具

### 2.1 now_ms：计时（M3 用）

```cpp
/**
 * @brief 取当前时间，单位毫秒（steady_clock：只往前走，适合量时间差）
 * @return 从某个固定起点开始的毫秒数，只用来相减
 */
static double now_ms() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}
```

### 2.2 parse_hex：十六进制文字 → 数字（M1 用）

maps 每一行开头是 `7f8a000000-7f8a2fd000`，要把它变成数字才能和指针比大小。

**为什么不用 `sscanf` / `strtoul`**：这两个函数会链接到 `GLIBC_2.38`，板子上跑不起来（readme 第 5 节）。所以自己写一个小循环：每读一个字符，`value = value × 16 + 这一位`。

```cpp
/**
 * @brief 把一段十六进制文字（不带 0x）转成数字，遇到第一个不是十六进制的字符就停
 * @param[in]  text    从这里开始读，例如 "7f8a000000-7f8a2fd000 ..."
 * @param[out] endText 传出停下来的位置（指向那个不是十六进制的字符）
 * @return 转出来的数字
 * @note 为什么不用 sscanf / strtoul：工具链的 glibc 比板子新，这两个函数会链接到
 *       GLIBC_2.38 才有的 __isoc23_* 版本，板子上跑不起来（readme 第 5 节）
 */
static uintptr_t parse_hex(const char *text, const char **endText) {
    uintptr_t value = 0;
    while (true) {
        const char ch = *text;
        int digit = -1;
        if (ch >= '0' && ch <= '9') {
            digit = ch - '0';
        } else if (ch >= 'a' && ch <= 'f') {
            digit = ch - 'a' + 10;
        } else if (ch >= 'A' && ch <= 'F') {
            digit = ch - 'A' + 10;
        }
        if (digit < 0) {
            break;
        }
        value = value * 16 + digit;
        ++text;
    }
    *endText = text;
    return value;
}
```

举例：`parse_hex("7f8a000000-7f8a2fd000", &cursor)` 返回 `0x7f8a000000`，cursor 停在 `-` 上。跳过 `-` 再调一次，就拿到结束地址。

### 2.3 print_whole_maps：整个 maps 原样打印（M1 第 1 步）

```cpp
/**
 * @brief 把整个 /proc/self/maps 原样打印出来（M1 第 1 步：先用眼睛找）
 * @note /proc/self/maps 是内核现场生成的文本文件，每次读都是"此刻"的样子
 */
static void print_whole_maps() {
    FILE *mapsFile = fopen("/proc/self/maps", "r");
    if (!mapsFile) {
        printf("fopen /proc/self/maps: %s\n", strerror(errno));
        return;
    }
    char line[512];
    while (fgets(line, sizeof(line), mapsFile)) {
        printf("%s", line);
    }
    fclose(mapsFile);
}
```

### 2.4 print_maps_line_containing：找到包含某地址的那一行（M1 M2 M4）

思路：一行一行读，读出"起始"和"结束"，满足 `起始 <= 地址 < 结束` 就是这一行。

```cpp
/**
 * @brief 在 /proc/self/maps 里找到包含 address 的那一行，打印出来
 * @param[in] label   打印时的前缀，说明这是谁的地址
 * @param[in] address 要查的地址
 * @note 每一行开头是 "起始-结束"（十六进制），起始 <= address < 结束 就是这一行
 */
static void print_maps_line_containing(const char *label, const void *address) {
    FILE *mapsFile = fopen("/proc/self/maps", "r");
    if (!mapsFile) {
        printf("fopen /proc/self/maps: %s\n", strerror(errno));
        return;
    }
    const uintptr_t targetAddress = reinterpret_cast<uintptr_t>(address);
    char line[512];
    bool isFound = false;
    while (fgets(line, sizeof(line), mapsFile)) {
        const char *cursor = line;
        const uintptr_t startAddress = parse_hex(cursor, &cursor);
        ++cursor; // 跳过中间的 '-'
        const uintptr_t endAddress = parse_hex(cursor, &cursor);
        if (startAddress <= targetAddress && targetAddress < endAddress) {
            printf("  %-14s %p\n    -> %s", label, address, line);
            isFound = true;
            break;
        }
    }
    if (!isFound) {
        printf("  %-14s %p\n    -> （maps 里没找到）\n", label, address);
    }
    fclose(mapsFile);
}
```

### 2.5 alloc_dma_heap / free_dma_heap（M1 M3）

和 day3_camera_cache_test 的同名函数一样，只是去掉了打印。三步：`open(heap)` → `ioctl(ALLOC)` 得到 `allocData.fd` → `mmap(allocData.fd, 0)`。

```cpp
/**
 * @brief 从 dma_heap 申请一块 dma_buf，并 mmap 到本进程
 * @param[in]  heapPath  例如 "/dev/dma_heap/system"（cached）、"/dev/dma_heap/system-uncached"
 * @param[in]  sizeBytes 要多大
 * @param[out] heapBuf   传出 dmaBufFd 和 mmapAddress，用完调 free_dma_heap()
 * @return 成功返回 true；失败打印原因并返回 false
 * @note 和 day3_camera_cache_test 的 alloc_dma_heap() 一样：open → ioctl(ALLOC) → mmap(allocData.fd)
 */
static bool alloc_dma_heap(const char *heapPath, size_t sizeBytes, DmaHeapBuffer &heapBuf) {
    const int heapFd = open(heapPath, O_RDWR | O_CLOEXEC);
    if (heapFd < 0) {
        printf("open %s: %s\n", heapPath, strerror(errno));
        return false;
    }
    dma_heap_allocation_data allocData = {};
    allocData.len = sizeBytes;
    allocData.fd_flags = O_RDWR | O_CLOEXEC;
    const int ret = ioctl(heapFd, DMA_HEAP_IOCTL_ALLOC, &allocData);
    close(heapFd); // 申请完就关窗口，内存由 allocData.fd 代表
    if (ret != 0) {
        printf("DMA_HEAP_IOCTL_ALLOC %s: %s\n", heapPath, strerror(errno));
        return false;
    }
    void *mmapAddress = mmap(nullptr, sizeBytes, PROT_READ | PROT_WRITE, MAP_SHARED, allocData.fd, 0);
    if (mmapAddress == MAP_FAILED) {
        printf("mmap dma_buf: %s\n", strerror(errno));
        close(allocData.fd);
        return false;
    }
    heapBuf.dmaBufFd = allocData.fd;
    heapBuf.mmapAddress = mmapAddress;
    heapBuf.sizeBytes = sizeBytes;
    return true;
}

/**
 * @brief 释放 alloc_dma_heap() 申请的内存：先 munmap，再 close（和申请的顺序相反）
 * @param[in,out] heapBuf 要释放的内存；释放后字段恢复成初始值
 */
static void free_dma_heap(DmaHeapBuffer &heapBuf) {
    if (heapBuf.mmapAddress) {
        munmap(heapBuf.mmapAddress, heapBuf.sizeBytes);
        heapBuf.mmapAddress = nullptr;
    }
    if (heapBuf.dmaBufFd >= 0) {
        close(heapBuf.dmaBufFd);
        heapBuf.dmaBufFd = -1;
    }
}
```

### 2.6 open_camera_and_map_buffers / close_camera（M1 M2）

和 day3_cam2h264 的前半段一样：open → S_FMT → REQBUFS(4) → 每块 QUERYBUF + mmap。

**和 day3_cam2h264 的区别**：**不 QBUF、不 STREAMON**。M1、M2 只关心"内存映射到哪里"，不需要摄像头真的拍，所以简单很多。

```cpp
/**
 * @brief 打开摄像头，S_FMT，REQBUFS(4)，4 块都 QUERYBUF + mmap（不 QBUF、不 STREAMON，不需要真的拍）
 * @param[out] camera 传出 videoFd 和 4 块 buffer 的 mem_offset、长度、CPU 地址
 * @return 成功返回 true；失败打印原因并返回 false（已经打开的东西由 close_camera() 收拾）
 */
static bool open_camera_and_map_buffers(CameraBuffers &camera) {
    camera.videoFd = open(kVideoDevicePath, O_RDWR | O_CLOEXEC);
    if (camera.videoFd < 0) {
        printf("open %s: %s\n", kVideoDevicePath, strerror(errno));
        return false;
    }

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
        void *mmapAddress = mmap(nullptr, planes[0].length, PROT_READ | PROT_WRITE, MAP_SHARED, camera.videoFd,
                             planes[0].m.mem_offset);
        if (mmapAddress == MAP_FAILED) {
            printf("mmap camera buffer %d: %s\n", bufIndex, strerror(errno));
            return false;
        }
        camera.memOffsets[bufIndex] = planes[0].m.mem_offset;
        camera.lengths[bufIndex] = planes[0].length;
        camera.mmapAddresses[bufIndex] = mmapAddress;
    }
    return true;
}

/**
 * @brief 关闭摄像头：先 munmap 4 块 buffer，再 close videoFd
 * @param[in,out] camera 要关的摄像头；关完字段恢复成初始值
 */
static void close_camera(CameraBuffers &camera) {
    for (int bufIndex = 0; bufIndex < kBufCount; ++bufIndex) {
        if (camera.mmapAddresses[bufIndex]) {
            munmap(camera.mmapAddresses[bufIndex], camera.lengths[bufIndex]);
            camera.mmapAddresses[bufIndex] = nullptr;
        }
    }
    if (camera.videoFd >= 0) {
        close(camera.videoFd);
        camera.videoFd = -1;
    }
}
```

---

## 3. M1：一个指针，到 /proc/self/maps 里查它是谁

### 3.1 任务拆解

| 步 | 做什么 | 用到的函数 |
|:---:|---|---|
| 1a | malloc 4 MB，memset 一遍 | `malloc` |
| 1b | 建一个 4 KB 的测试文件，同一个文件 mmap **两次**：一次 `MAP_SHARED`，一次 `MAP_PRIVATE` | `create_test_file`、`mmap` |
| 1c | 打印三个地址，然后整个 maps 原样打印，**用眼睛找** | `print_whole_maps` |
| 2 | 打开摄像头，4 块 buffer 都 mmap | `open_camera_and_map_buffers` |
| 3 | 从 dma_heap `system` 申请一块 3 MB | `alloc_dma_heap` |
| 4 | 对每个地址，**自动**找出 maps 里的那一行 | `print_maps_line_containing` |
| 5 | 收尾：和申请的顺序相反 | `free_dma_heap`、`close_camera`、`munmap`、`free` |

为什么 1b 要把同一个文件映射两次：两行的路径一样，只有权限列一个是 `s`、一个是 `p`。这样对比最清楚，能看出 `MAP_SHARED` / `MAP_PRIVATE` 在登记簿里怎么写。

### 3.2 代码

```cpp
/**
 * @brief 创建一个 kTestFileSizeBytes 大的测试文件，写满字母 'A'
 * @return 成功返回 true；失败打印原因并返回 false
 * @note mmap 一个文件之前，文件得先有这么长；空文件 mmap 以后一访问就会 SIGBUS
 */
static bool create_test_file() {
    const int fileFd = open(kTestFilePath, O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fileFd < 0) {
        printf("open %s: %s\n", kTestFilePath, strerror(errno));
        return false;
    }
    char content[kTestFileSizeBytes];
    memset(content, 'A', sizeof(content));
    const bool isWritten = write(fileFd, content, sizeof(content)) == static_cast<ssize_t>(sizeof(content));
    close(fileFd);
    return isWritten;
}

/**
 * @brief M1：准备 4 种映射，打印地址，再到 /proc/self/maps 里找到它们
 * @return 成功返回 0；失败返回 1
 * @note 第 1 步 malloc + 文件 → 第 2 步 摄像头 → 第 3 步 dma_heap，每一步都打印一次
 */
static int run_m1_maps() {
    printf("===== M1：到 /proc/self/maps 里查指针是谁 =====\n");

    // 第 1 步：malloc 一大块（glibc 对超过 128 KB 的 malloc 内部用 mmap）
    void *mallocAddr = malloc(kMallocSizeBytes);
    memset(mallocAddr, 0, kMallocSizeBytes);

    // 第 1 步：同一个文件 mmap 两次，一次 MAP_SHARED、一次 MAP_PRIVATE，看权限列的 s 和 p
    if (!create_test_file()) {
        free(mallocAddr);
        return 1;
    }
    const int fileFd = open(kTestFilePath, O_RDWR | O_CLOEXEC);
    void *sharedFileAddr = mmap(nullptr, kTestFileSizeBytes, PROT_READ | PROT_WRITE, MAP_SHARED, fileFd, 0);
    void *privateFileAddr = mmap(nullptr, kTestFileSizeBytes, PROT_READ | PROT_WRITE, MAP_PRIVATE, fileFd, 0);
    close(fileFd); // mmap 完 fd 就可以关，映射还在
    if (sharedFileAddr == MAP_FAILED || privateFileAddr == MAP_FAILED) {
        printf("mmap %s: %s\n", kTestFilePath, strerror(errno));
        free(mallocAddr);
        return 1;
    }

    printf("\n--- 第 1 步：整个 maps 原样打印，用眼睛找下面两个地址 ---\n");
    printf("mallocAddr      = %p\n", mallocAddr);
    printf("sharedFileAddr  = %p\n", sharedFileAddr);
    printf("privateFileAddr = %p\n\n", privateFileAddr);
    print_whole_maps();

    // 第 2 步：摄像头 4 块 buffer
    CameraBuffers camera;
    const bool isCameraReady = open_camera_and_map_buffers(camera);

    // 第 3 步：dma_heap 一块
    DmaHeapBuffer heapBuf;
    const bool isHeapReady = alloc_dma_heap("/dev/dma_heap/system", kHeapBufSizeBytes, heapBuf);

    printf("\n--- 第 2、3 步：自动找出每个地址所在的那一行 ---\n");
    print_maps_line_containing("malloc", mallocAddr);
    print_maps_line_containing("file SHARED", sharedFileAddr);
    print_maps_line_containing("file PRIVATE", privateFileAddr);
    if (isCameraReady) {
        for (int bufIndex = 0; bufIndex < kBufCount; ++bufIndex) {
            char label[32];
            snprintf(label, sizeof(label), "camera buf %d", bufIndex);
            print_maps_line_containing(label, camera.mmapAddresses[bufIndex]);
            printf("    （QUERYBUF 的 mem_offset = 0x%08x，长度 = %zu）\n", camera.memOffsets[bufIndex],
                   camera.lengths[bufIndex]);
        }
    }
    if (isHeapReady) {
        print_maps_line_containing("dma_heap", heapBuf.mmapAddress);
    }

    // 收尾：和申请的顺序相反
    free_dma_heap(heapBuf);
    close_camera(camera);
    munmap(privateFileAddr, kTestFileSizeBytes);
    munmap(sharedFileAddr, kTestFileSizeBytes);
    free(mallocAddr);
    return 0;
}
```

**一个细节**：摄像头打不开（比如相机 App 占着）时，M1 不会退出，只是跳过摄像头那几行。这样第 1 步在任何情况下都能跑。

### 3.3 期望看到的输出 ⚠️ 猜测，上板后改成实测

```
--- 第 2、3 步：自动找出每个地址所在的那一行 ---
  malloc         0x7f9b400010
    -> 7f9b400000-7f9b801000 rw-p 00000000 00:00 0
  file SHARED    0x7f9c7ff000
    -> 7f9c7ff000-7f9c800000 rw-s 00000000 b3:08 1234   /tmp/day3_mmap_test.bin
  file PRIVATE   0x7f9c7fe000
    -> 7f9c7fe000-7f9c7ff000 rw-p 00000000 b3:08 1234   /tmp/day3_mmap_test.bin
  camera buf 0   0x7f9a000000
    -> 7f9a000000-7f9a2fb000 rw-s 00000000 00:06 567    /dev/video22
    （QUERYBUF 的 mem_offset = 0x00000000，长度 = 3110400）
  camera buf 1   ...
    -> ...                   rw-s 002fb000 ...          /dev/video22
  dma_heap       0x7f99d00000
    -> 7f99d00000-7f9a000000 rw-s 00000000 00:09 89     /dmabuf:      ← 具体写什么不确定
```

**实测（2026-10-29，第 1 步：malloc + 文件）**，和猜测一致：

```
mallocAddr      = 0x7f8d162010
sharedFileAddr  = 0x7f8d902000
privateFileAddr = 0x7f8d901000

7f8d162000-7f8d563000 rw-p 00000000 00:00 0                        ← malloc：没有路径、p；长度 0x401000 = 4 MB + 4 KB
7f8d901000-7f8d902000 rw-p 00000000 00:11 63   /tmp/day3_mmap_test.bin  ← MAP_PRIVATE：p
7f8d902000-7f8d903000 rw-s 00000000 00:11 63   /tmp/day3_mmap_test.bin  ← MAP_SHARED：s，其他列完全一样
```

- malloc 返回的指针比映射起点多 `0x10`（16 字节）：glibc 的记账头。要 4 MB 实际映射了 4 MB + 4 KB，多出来的那一页也是给这个头和对齐用的
- 两次文件映射：路径、设备号 `00:11`、inode `63` 都一样（同一个文件），只有 `s` / `p` 不同。`00:11` 说明板子的 /tmp 是内存文件系统（tmpfs），不是 eMMC 上的分区（eMMC 上的库是 `b3:06`）
- 后 mmap 的（PRIVATE）地址反而更小：内核从高往低找空位

**实测（2026-10-30，第 2 步：摄像头）**：

```
camera buf 0   0x7f905a5000 -> 7f905a5000-7f908a2000 rw-s 00000000 00:05 182   /dev/video22   mem_offset 0x00000000
camera buf 1   0x7f902a8000 -> 7f902a8000-7f905a5000 rw-s 00000000 00:05 182   /dev/video22   mem_offset 0x002fd000
camera buf 2   0x7f8ffab000 -> 7f8ffab000-7f902a8000 rw-s 00000000 00:05 182   /dev/video22   mem_offset 0x005fa000
camera buf 3   0x7f8fcae000 -> 7f8fcae000-7f8ffab000 rw-s 00000000 00:05 182   /dev/video22   mem_offset 0x008f7000
```

路径是 `/dev/video22`、4 行、每行 0x2fd000、`s`，都和猜的一样。**猜错了一个：偏移列 4 行全是 0，不等于 mem_offset**（原因见 doc/1-理解M1的运行结果.md 第 6.1 节）。第 3 步（dma_heap）的写法和实测见 3.5 节（2026-10-31 上板通过）。

猜测的理由：
- malloc 的地址末尾是 `010`，不是整页（`000`）：glibc 在这块的开头留了 16 字节记账用，所以 malloc 返回的指针比映射的起点多 16
- 摄像头第 1 块的"偏移"列应该等于它的 `mem_offset`。maps 的偏移列就是 mmap 的最后一个参数（⚠️ 猜错了：实测 4 块的偏移列都是 0，见上面的实测）

### 3.4 要回答的问题

| # | 问题 | 提示 |
|---|---|---|
| 1 | malloc 那一行，路径为什么是空的？ | 匿名映射，不对应任何文件 |
| 2 | 摄像头 4 块 buffer 是 4 行，还是 1 行？ | 调了 4 次 mmap |
| 3 | 摄像头那几行的"偏移"列，和打印的 `mem_offset` 一样吗？ | mmap 的最后一个参数 |
| 4 | dma_heap 那一行，路径是 `/dev/dma_heap/system` 吗？ | mmap 的是 `allocData.fd`，不是 `heapFd` |
| 5 | file SHARED 和 file PRIVATE 两行，只有哪一列不一样？ | `s` / `p` |

### 3.5 第 3 步具体怎么写：dma_heap（2026-10-31 补，按拆分后的 src/ 写）

**目的（一句话）**：从 dma heap 申请一块内存，mmap 以后到 maps 里找它那一行，和 M2 里 pointerB（摄像头 EXPBUF 出来的 dma_buf）那一行比一比。

#### 3.5.1 已经准备好的东西（不用写）

| 东西 | 在哪 | 做什么 |
|---|---|---|
| `alloc_dma_heap(heapPath, sizeBytes, heapBuf)` | `src/dma_heap/dma_heap_buffer.cpp` | open(heap) → `DMA_HEAP_IOCTL_ALLOC` 拿到 dma_buf fd → close(heapFd) → mmap(dma_buf fd, 0)。成功返回 true |
| `free_dma_heap(heapBuf)` | 同上 | munmap → close(dmaBufFd)。没申请成功也能调（字段是初始值就跳过） |
| `DmaHeapBuffer` | `src/dma_heap/dma_heap_buffer.h` | 三个字段：`dmaBufFd`、`mmapAddress`、`sizeBytes` |
| `kHeapBufSizeBytes` | 同上 | 3 MB |
| `print_maps_line_containing(label, address)` | `src/common/maps_printer.h` | 第 2 步已经用过 |

这两个函数 2026-10-31 拆文件时就写好了，一直没被调用过，所以**还没在板子上跑过**。

#### 3.5.2 板子上有哪些 heap（2026-10-31 实测 `adb shell ls -l /dev/dma_heap/`）

```
crw-rw---- 1 root video 251, 4 cma
crw-rw---- 1 root video 251, 5 cma-uncached
crw-rw---- 1 root video 251, 0 system
crw-rw---- 1 root video 251, 1 system-dma32
crw-rw---- 1 root video 251, 2 system-uncached
crw-rw---- 1 root video 251, 3 system-uncached-dma32
```

- 第一个字母 `c`：字符设备。`251, 0` 是主设备号、次设备号，6 个 heap 主设备号都是 251，是同一个 dma-heap 驱动
- `system`：普通物理页，不要求物理连续；CPU 访问走缓存（cached）
- `cma`：从 CMA 区域分配，物理连续
- `-uncached`：CPU 访问不走缓存，M3 要拿它和 cached 比 sync 的耗时
- `-dma32`：只从 4 GB 以下的物理地址分配，给只能访问 32 位地址的硬件用

第 3 步用 **`system`**。

> 上面对每种 heap 的解释，是 Linux / Rockchip 的通用说法，这块板子上还没逐个验证。

#### 3.5.3 要写的代码（都在 `src/experiments/m1_maps.cpp`）

一共改 3 处：

**① 文件开头的 include 里加一行**

```cpp
#include "dma_heap/dma_heap_buffer.h"
```

**② 常量区加一个常量**（和 `kTestFilePath` 放一起，不在函数里直接写字符串）

```cpp
// 从哪个 dma heap 申请：system = 普通物理页、CPU 访问走缓存（cached）。本板有 6 个 heap，见流程.md 3.5.2
static const char* kDmaHeapPath = "/dev/dma_heap/system";
```

**③ `run_m1_maps()` 里，第 2 步那个 `if` 结束以后、`close_camera(camera);` 之前，插入第 3 步**

```cpp
    // 第 3 步：从 dma heap 申请一块 dma_buf，看它在 maps 里是哪一行
    //   alloc_dma_heap 里做了三件事：open(heap) → DMA_HEAP_IOCTL_ALLOC 拿到 dma_buf fd → mmap(dma_buf fd, 0)
    //   和 M2 的 pointerB 比：那块 dma_buf 是摄像头驱动用 EXPBUF 导出的，这块是 heap 直接分配的
    DmaHeapBuffer heapBuf;
    if (alloc_dma_heap(kDmaHeapPath, kHeapBufSizeBytes, heapBuf)) {
        LOGI(kTag, "--- 第 3 步：dma_heap 一块 ---");
        LOGI(kTag, "    dmaBufFd = %d, sizeBytes = %zu", heapBuf.dmaBufFd, heapBuf.sizeBytes);
        print_maps_line_containing("dma_heap", heapBuf.mmapAddress);
    }
    // 成功、失败都要调：失败时 heapBuf 的字段还是初始值（-1、nullptr），free_dma_heap 会直接跳过
    // 释放顺序和申请相反：dma_heap 是最后申请的，所以最先释放，然后才是 close_camera
    free_dma_heap(heapBuf);
```

改完后，`run_m1_maps()` 结尾的顺序是：

```
第 2 步 if { ... }
第 3 步（新加的）
free_dma_heap(heapBuf);
close_camera(camera);
munmap(fileAddrPrivate, ...);
munmap(fileAddrShared, ...);
free(mallocAddr);
return 0;
```

#### 3.5.4 运行

```bash
./run_on_board_with_mac.sh m1
```

CLion 里运行时，Program arguments 填 `m1`。不带参数会跑 M2，不是 M1。

#### 3.5.5 期望看到的输出 ⚠️ 猜测，上板后改成实测

```
--- 第 3 步：dma_heap 一块 ---
    dmaBufFd = 5, sizeBytes = 3145728
dma_heap       0x7f8xxxx000 -> 7f8xxxx000-7f8yyyy000 rw-s 00000000 00:08 <inode>   /dmabuf:<pid>-day3_mmap
```

猜测的理由：
- **dmaBufFd 是 5**：摄像头开着时 videoFd 占了 3；alloc_dma_heap 里 open(heap) 拿到 4，ALLOC 拿到 5，然后才 close(4)。摄像头没打开的话就是 4
- **长度 0x300000**：3 MB = 3145728 正好是 768 页，不用向上取整
- **`s`、偏移 0**：MAP_SHARED、偏移填的 0
- **`00:08` 和 `/dmabuf:`**：M2 实测 pointerB 那一行就是这样（doc/2 第 3 节）。heap 分配的和 EXPBUF 导出的都是 dma_buf，推断显示方式一样。名字是不是 `<pid>-day3_mmap` 不确定

**实测（2026-10-31，第 3 步：dma_heap system）**，猜测全部对上：

```
--- 第 3 步：dma_heap 一块 ---
    dmaBufFd = 5, sizeBytes = 3145728
dma_heap       0x7f8f3ee000 -> 7f8f3ee000-7f8f6ee000 rw-s 00000000 00:08 22   /dmabuf:1628-day3_mmap
```

- `dmaBufFd = 5`：和猜的一样（videoFd 3、heapFd 4、ALLOC 给 5，然后才关掉 4）
- 长度 `0x7f8f6ee000 - 0x7f8f3ee000 = 0x300000` = 3 MB
- `rw-s`、偏移 0、设备号 `00:08`、路径 `/dmabuf:<pid>-<程序名>`，和 M2 里 pointerB（摄像头 EXPBUF 出来的）那一行格式完全一样。**所以只看 maps，分不出这个 dma_buf 是 heap 分配的，还是摄像头导出的**
- 地址紧挨着摄像头 buf 3 的下面（buf 3 从 `7f8f6ee000` 开始，dma_heap 正好在 `7f8f6ee000` 结束）。摄像头 buf 0 也紧挨着 malloc 那块的下面。说明内核是从高地址往低地址，一块挨一块地找空位

#### 3.5.6 要回答的问题

| #   | 问题                                                                                  | 提示                                                          |
|-----|---------------------------------------------------------------------------------------|---------------------------------------------------------------|
| 1   | 路径是 `/dev/dma_heap/system`，还是 `/dmabuf:...`？                                   | mmap 的是 `allocationData.fd`（dma_buf），不是 `heapFd`       |
| 2   | 和 M2 的 pointerB 那一行比，哪几列一样、哪几列不一样？                                | 设备号、路径；inode 每个 dma_buf 都不同                       |
| 3   | `dmaBufFd` 实际是几？和猜的 5 一样吗？为什么？                                        | open 总是给最小的空号；close(heapFd) 在 ALLOC 之后            |
| 4   | heapFd 在 mmap 之前就关了，为什么 mmap 还能成功？                                     | 内存归 dma_buf 管，不归 heapFd                                |
| 5   | （进阶）把 `kDmaHeapPath` 换成 `system-uncached` 或 `cma` 再跑，maps 那一行有区别吗？ | 推断看不出区别：有没有走缓存不会显示在 maps 里，M3 用计时来看 |

#### 3.5.7 写完自查

- [ ] 新常量叫 `kDmaHeapPath`（`k` 开头 + 说清是路径），没有在函数里直接写 `"/dev/dma_heap/system"`
- [ ] 有知识点的行写了注释
- [ ] `free_dma_heap` 放在 `close_camera` 之前，而且不管成功失败都会调到
- [ ] 上板 `m1` 跑完，`echo $?` 是 0
- [ ] 把实测输出贴回 3.5.5，猜错的地方标出来

---

## 4. M2：同一块内存，两个指针 ⭐

### 4.1 任务拆解

| 步 | 做什么 | 结果 |
|:---:|---|---|
| 1 | 打开摄像头，4 块都 mmap | 第 0 块的 mmapAddress 就是**指针 A** |
| 2 | 对第 0 块 `VIDIOC_EXPBUF`，**flags 带 O_RDWR** | 得到 `dmaBufFd` |
| 3 | `mmap(dmaBufFd, 偏移 0)` | **指针 B** |
| 4 | 打印 A、B，各自在 maps 里找一行 | 两个地址不一样 |
| 5 | 从 A 的开头写 `"hello from A"`，从 B 的开头读 | 应该读到同样的字 |
| 6 | 从 B 的**末尾**写 `"hello from B (at the end)"`，从 A 的末尾读 | 应该读到同样的字 → 整块都是同一块 |
| 7 | 收尾：munmap B → close dmaBufFd → close_camera | |

### 4.2 一个容易踩的坑：EXPBUF 的 flags

day3_cam2h264 里 EXPBUF 只写了 `flags = O_CLOEXEC`。这样拿到的是一个**只读**的 dma_buf fd。交给编码器没问题，因为编码器只读这块内存。

但 M2 要用这个 fd 做**可写**的 mmap（`PROT_WRITE` + `MAP_SHARED`），而对只读 fd 做可写的共享映射，内核会拒绝（`EACCES`）。所以这里要写 `O_RDWR | O_CLOEXEC`。

> 就像拿着一把"只能看"的钥匙，却想进去搬东西，门卫不让。

### 4.3 代码

```cpp
/**
 * @brief M2：第 0 块摄像头 buffer 用两种方式 mmap，从 A 写、从 B 读，再反过来
 * @return 两个方向都读到一样的字返回 0；否则返回 1
 * @note 指针 A：mmap(videoFd, mem_offset)；指针 B：mmap(EXPBUF 得到的 dmaBufFd, 0)
 */
static int run_m2_two_pointers() {
    printf("===== M2：同一块内存，两个指针 =====\n");

    // 第 1 步：打开摄像头，4 块都 mmap（指针 A 就是第 0 块的 mmapAddress）
    CameraBuffers camera;
    if (!open_camera_and_map_buffers(camera)) {
        close_camera(camera);
        return 1;
    }
    char *pointerA = static_cast<char *>(camera.mmapAddresses[0]);
    const size_t bufLengthBytes = camera.lengths[0];

    // 第 2 步：EXPBUF 第 0 块，拿到 dma_buf fd。
    // 注意 flags 要带 O_RDWR：day3_cam2h264 只给编码器用，只写了 O_CLOEXEC（只读 fd）；
    // 这里要 mmap 成可写，只读的 fd 做 PROT_WRITE + MAP_SHARED 的 mmap 会失败（EACCES）
    v4l2_exportbuffer exportBuf = {};
    exportBuf.type = kBufType;
    exportBuf.index = 0;
    exportBuf.plane = 0;
    exportBuf.flags = O_RDWR | O_CLOEXEC;
    if (ioctl(camera.videoFd, VIDIOC_EXPBUF, &exportBuf) != 0) {
        printf("VIDIOC_EXPBUF: %s\n", strerror(errno));
        close_camera(camera);
        return 1;
    }
    const int dmaBufFd = exportBuf.fd;

    // 第 3 步：用 dma_buf fd 再 mmap 一次，偏移是 0（一个 dma_buf fd 只代表这一块）
    void *mapped = mmap(nullptr, bufLengthBytes, PROT_READ | PROT_WRITE, MAP_SHARED, dmaBufFd, 0);
    if (mapped == MAP_FAILED) {
        printf("mmap dmaBufFd: %s\n", strerror(errno));
        close(dmaBufFd);
        close_camera(camera);
        return 1;
    }
    char *pointerB = static_cast<char *>(mapped);

    // 第 4 步：两个地址不一样，各自在 maps 里是哪一行
    printf("pointerA = %p  （mmap(videoFd %d, mem_offset 0x%08x)）\n", pointerA, camera.videoFd,
           camera.memOffsets[0]);
    printf("pointerB = %p  （mmap(dmaBufFd %d, 0)）\n", pointerB, dmaBufFd);
    print_maps_line_containing("pointerA", pointerA);
    print_maps_line_containing("pointerB", pointerB);

    // 第 5 步：从 A 写，从 B 读
    const char *messageFromA = "hello from A";
    memset(pointerA, 0, 64);
    strcpy(pointerA, messageFromA);
    printf("\n写入 A：\"%s\"\n从 B 读：\"%.63s\"\n", messageFromA, pointerB);
    const bool isAToBSame = strcmp(pointerB, messageFromA) == 0;

    // 第 6 步：反过来，从 B 写，从 A 读。顺便写在 buffer 最后 64 字节，证明整块都是同一块
    const char *messageFromB = "hello from B (at the end)";
    char *tailOfB = pointerB + bufLengthBytes - 64;
    char *tailOfA = pointerA + bufLengthBytes - 64;
    memset(tailOfB, 0, 64);
    strcpy(tailOfB, messageFromB);
    printf("写入 B 末尾：\"%s\"\n从 A 末尾读：\"%.63s\"\n", messageFromB, tailOfA);
    const bool isBToASame = strcmp(tailOfA, messageFromB) == 0;

    printf("\n结论：%s\n", isAToBSame && isBToASame ? "两个指针读写的是同一块内存" : "不一样！要查原因");

    // 收尾：先 munmap B、关 dmaBufFd，再关摄像头
    munmap(pointerB, bufLengthBytes);
    close(dmaBufFd);
    close_camera(camera);
    return isAToBSame && isBToASame ? 0 : 1;
}
```

### 4.4 期望看到的输出 ⚠️ 猜测

> **2026-10-31 已上板**：两个方向都读到，B 那一行是 `/dmabuf:2349-day3_mmap`（猜测的 `/dmabuf:` 后面还跟着"进程号-程序名"），设备号 `00:08`、inode `24`。下面"可能出现的意外"都没出现。实测和分析见 [doc/2-理解M2的运行结果.md](doc/2-理解M2的运行结果.md)。

```
pointerA = 0x7f9a000000  （mmap(videoFd 3, mem_offset 0x00000000)）
pointerB = 0x7f99a00000  （mmap(dmaBufFd 4, 0)）
  pointerA       0x7f9a000000
    -> 7f9a000000-7f9a2fb000 rw-s 00000000 00:06 567  /dev/video22
  pointerB       0x7f99a00000
    -> 7f99a00000-7f99cfb000 rw-s 00000000 00:09 90   /dmabuf:

写入 A："hello from A"
从 B 读："hello from A"
写入 B 末尾："hello from B (at the end)"
从 A 末尾读："hello from B (at the end)"

结论：两个指针读写的是同一块内存
```

**可能出现的意外**（上板后看）：
- `mmap dmaBufFd` 失败：说明摄像头驱动导出的 dma_buf 不支持 CPU mmap。那就只能用 bufinfo 间接证明。我估计不会失败：videobuf2 导出的 dma_buf 一般都支持 mmap
- 两个地址正好挨着：这没关系，地址是内核随便挑的空位

### 4.5 要回答的问题

1. 两个地址不一样，为什么读写的是同一块？（页表：两个虚拟地址的页表项指向同一批物理页。见 open_ioctl_mmap是什么.md 第 1 节）
2. maps 里 A 那行路径是 `/dev/video22`，B 那行是什么？说明了什么？
3. 为什么 A 的 mmap 要传 `mem_offset`，B 的 mmap 传 0？（videoFd 管 4 块，要用偏移选第几块；dmaBufFd 只代表一块）

---

## 5. M3：自己调 DMA_BUF_IOCTL_SYNC

### 5.1 任务拆解

|  步   | 做什么                                                                                                 |
|:-----:|--------------------------------------------------------------------------------------------------------|
|   1   | 写一个函数 `sync_dma_buf(fd, flags)`：填 `dma_buf_sync`，调 `ioctl(fd, DMA_BUF_IOCTL_SYNC, &syncArgs)` |
|   2   | 从 `/dev/dma_heap/system`（cached）申请 3 MB，先 memset 一遍、不计时（第一次往往偏慢）                 |
|   3   | 重复 10 轮：`sync(START \| WRITE)` → `memset` 写满 → `sync(END \| WRITE)`，三段分别计时                |
|   4   | 打印 10 轮平均                                                                                         |
|   5   | 对 `/dev/dma_heap/system-uncached` 做同样的事                                                          |

### 5.2 flags 怎么填

`linux/dma-buf.h` 里的定义（sysroot 第 30～34 行）：

| 宏                   | 值 | 意思           |
|----------------------|----|----------------|
| `DMA_BUF_SYNC_READ`  | 1  | CPU 要读       |
| `DMA_BUF_SYNC_WRITE` | 2  | CPU 要写       |
| `DMA_BUF_SYNC_START` | 0  | 开始访问之前调 |
| `DMA_BUF_SYNC_END`   | 4  | 访问完之后调   |

所以"开始写"是 `START | WRITE`，"写完了"是 `END | WRITE`。START 的值是 0，"或"上去不改变数字，写上只是为了让人读得懂。

### 5.3 代码（2026-10-31 按拆分后的 src/ 重写，全部代码）

一共动 3 个文件：

| 文件                              | 做什么                                                                                      |
|-----------------------------------|---------------------------------------------------------------------------------------------|
| `src/experiments/m3_sync.h`   | 整个替换成 5.3.1 的内容（CLion 生成的 `#ifndef` 模板换成 `#pragma once`，和其他头文件一致） |
| `src/experiments/m3_sync.cpp` | 整个替换成 5.3.2 的内容                                                                     |
| `src/main.cpp`                    | 加一行 include、加一个 `m3` 分支（5.3.3）                                                   |

`CMakeLists.txt` 已经加过这两个文件，不用再改。`alloc_dma_heap` / `free_dma_heap` 用 `src/dma_heap/` 里现成的，M1 第 3 步已经验证过。

函数怎么分工：

```
run_m3_sync()                       ← main 调它
├── measure_sync_timing(system)            申请 → 先写一遍 → 10 轮计时 → 释放，传出平均值
│     ├── alloc_dma_heap / free_dma_heap    （src/dma_heap/，现成的）
│     ├── sync_dma_buf(fd, START | WRITE)   就是一次 ioctl(DMA_BUF_IOCTL_SYNC)
│     ├── memset 写满 3 MB
│     ├── sync_dma_buf(fd, END | WRITE)
│     └── get_now_ms()                      每段前后各取一次时间
├── measure_sync_timing(system-uncached)   同上
└── print_sync_timing() × 2                两行放在一起打印，方便对比
```

#### 5.3.1 `src/experiments/m3_sync.h`

```cpp
/*
 * m3_sync.h：M3 实验——自己调 DMA_BUF_IOCTL_SYNC，比较 cached 和 uncached 两种 dma heap
 *
 *   对每种 heap：申请 3 MB → 先写一遍（不计时）→ 重复 10 轮：
 *     sync(START | WRITE) → memset 写满 3 MB → sync(END | WRITE)，三段分别计时，最后取平均
 *
 * 写法和期望输出见 具体编码day3_mmap的流程.md 第 5 节
 */
#pragma once

/**
 * @brief M3：cached（system）和 uncached（system-uncached）两种 dma heap，各测一次 sync 和 memset 的耗时
 * @return 两种 heap 都测完返回 0；任何一种申请失败或 sync 失败返回 1
 * @note 只测"CPU 写"这个方向（flags 带 DMA_BUF_SYNC_WRITE），不涉及硬件，所以只能看出 sync 本身花多少时间
 */
int run_m3_sync();
```

#### 5.3.2 `src/experiments/m3_sync.cpp`

```cpp
#include "experiments/m3_sync.h"

#include "common/log.h"
#include "dma_heap/dma_heap_buffer.h"
#include <cerrno> // errno
#include <chrono> // std::chrono::steady_clock：计时
#include <cstdint> // uint64_t
#include <cstring> // memset、strerror
#include <linux/dma-buf.h> // dma_buf_sync、DMA_BUF_IOCTL_SYNC、DMA_BUF_SYNC_START / END / READ / WRITE
#include <sys/ioctl.h> // ioctl

static const char* kTag = "M3";
// 每种 heap 重复测几轮取平均：只测一轮的话，CPU 升频、别的进程打扰都会让数字跳来跳去
static const int kSyncRepeatCount = 10;
// 要比较的两种 heap（板子上都有，流程.md 3.5.2 实测）：
//   system         ：CPU 读写走缓存（cached）。CPU 写的数据可能先停在缓存里，还没到内存，
//                    交给硬件之前要 sync，把缓存里的数据写回内存
//   system-uncached：CPU 读写不走缓存，每次都直接到内存。不用 sync，代价是 CPU 读写慢
static const char* kCachedHeapPath   = "/dev/dma_heap/system";
static const char* kUncachedHeapPath = "/dev/dma_heap/system-uncached";

// 一种 heap 测出来的平均耗时，单位毫秒
struct SyncTiming {
    double syncStartMs = 0; // sync(START | WRITE) 花的时间
    double memsetMs    = 0; // memset 写满整块花的时间
    double syncEndMs   = 0; // sync(END | WRITE) 花的时间
};

/**
 * @brief 取当前时间，单位毫秒
 * @return 从某个固定起点开始的毫秒数，只能拿两次的结果相减，单独一个值没有意义
 * @note 用 steady_clock：它只会往前走。system_clock 是墙上时间，可能被 NTP 或手动改时间调回去，量时间差会出错
 */
static double get_now_ms() {
    using namespace std::chrono;
    // duration<double, std::milli>：把时间换算成"带小数的毫秒"，这样 0.123 ms 也能表示
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

/**
 * @brief 对一个 dma_buf fd 调一次 DMA_BUF_IOCTL_SYNC
 * @param[in] dmaBufFd dma_buf 的 fd（ALLOC 或 EXPBUF 拿到的）
 * @param[in] flags    DMA_BUF_SYNC_START 或 DMA_BUF_SYNC_END，再"或"上 DMA_BUF_SYNC_READ / DMA_BUF_SYNC_WRITE
 * @return 成功返回 true；失败打印原因并返回 false
 * @note MPP 的 mpp_buffer_sync_begin / end 最后也是调这个 ioctl（mpp/osal/mpp_dmabuf.cpp 第 40、56 行）
 */
static bool sync_dma_buf(int dmaBufFd, uint64_t flags) {
    // dma_buf_sync 只有一个字段 flags（linux/dma-buf.h）
    dma_buf_sync syncArgs = {};
    syncArgs.flags        = flags;
    // DMA_BUF_IOCTL_SYNC = _IOW('b', 0, struct dma_buf_sync)：只有 W，没有 R，
    //   内核只读这张表、不往回写，所以调完以后 syncArgs 里没有新东西，只看返回值
    if (ioctl(dmaBufFd, DMA_BUF_IOCTL_SYNC, &syncArgs) != 0) {
        LOGE(kTag, "DMA_BUF_IOCTL_SYNC flags 0x%llx: %s", static_cast<unsigned long long>(flags), strerror(errno));
        return false;
    }
    return true;
}

/**
 * @brief 从一种 dma heap 申请一块内存，重复 kSyncRepeatCount 轮：sync START → memset 写满 → sync END，
 *        三段分别计时取平均
 * @param[in]  heapPath      heap 的设备文件，例如 kCachedHeapPath
 * @param[out] averageTiming 成功时传出三段的平均耗时
 * @return 成功返回 true；申请失败或 sync 失败返回 false
 * @note 正式计时前先写一遍、不计时：第一次写往往偏慢（CPU 还没升频、页表和缓存都是冷的，推断），不算进平均
 * @note 申请的内存在函数结束前释放，调用方不用管
 */
static bool measure_sync_timing(const char* heapPath, SyncTiming& averageTiming) {
    DmaHeapBuffer heapBuf;
    if (!alloc_dma_heap(heapPath, kHeapBufSizeBytes, heapBuf)) {
        return false;
    }
    // 先写一遍，不计时（理由见 @note）
    memset(heapBuf.mmapAddress, 0, heapBuf.sizeBytes);

    SyncTiming totalTiming;
    bool isAllSyncOk = true;
    for (int roundIndex = 0; roundIndex < kSyncRepeatCount; ++roundIndex) {
        // 第 1 步 开始：告诉内核"CPU 要开始写这块了"。
        //   START 的值是 0，"或"上去数字不变，写上只是让人读懂；WRITE 说明这次 CPU 要写。
        //   cached 的 heap 在这里让 CPU 缓存里这块的旧内容作废，
        //   免得 CPU 读到硬件写之前的旧数据（推断，本板内核源码没核对）
        const double beforeSyncStartMs = get_now_ms();
        // &=：只要有一次 sync 失败，isAllSyncOk 就变成 false，之后再成功也变不回 true
        isAllSyncOk &= sync_dma_buf(heapBuf.dmaBufFd, DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE);
        const double afterSyncStartMs = get_now_ms();

        // 第 2 步 写：CPU 写满整块。每轮写的值不一样（roundIndex 只取低 8 位），保证每轮真的改了内容。
        //   cached：先写进 CPU 缓存，很快；uncached：每个字节都直接写到内存，慢
        memset(heapBuf.mmapAddress, roundIndex, heapBuf.sizeBytes);
        const double afterMemsetMs = get_now_ms();

        // 第 3 步 结束：告诉内核"CPU 写完了"。
        //   cached 的 heap 在这里把缓存里的新数据写回内存，之后硬件从内存读才能读到（推断，本板内核源码没核对）。
        //   uncached 的 heap 没有缓存要处理，推断几乎不花时间
        //   （MPP 对 uncached buffer 也直接跳过 sync：mpp/base/mpp_buffer.cpp 第 293 行）
        isAllSyncOk &= sync_dma_buf(heapBuf.dmaBufFd, DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE);
        const double afterSyncEndMs = get_now_ms();

        totalTiming.syncStartMs += afterSyncStartMs - beforeSyncStartMs;
        totalTiming.memsetMs += afterMemsetMs - afterSyncStartMs;
        totalTiming.syncEndMs += afterSyncEndMs - afterMemsetMs;
    }
    free_dma_heap(heapBuf);
    if (!isAllSyncOk) {
        return false;
    }
    averageTiming.syncStartMs = totalTiming.syncStartMs / kSyncRepeatCount;
    averageTiming.memsetMs    = totalTiming.memsetMs / kSyncRepeatCount;
    averageTiming.syncEndMs   = totalTiming.syncEndMs / kSyncRepeatCount;
    return true;
}

/**
 * @brief 把一种 heap 的平均耗时打印成一行，后面附上合计和 memset 的写入速度
 * @param[in] heapPath heap 的设备文件，打印在行首
 * @param[in] timing   measure_sync_timing() 传出的平均耗时
 * @return 无
 */
static void print_sync_timing(const char* heapPath, const SyncTiming& timing) {
    const double totalMs = timing.syncStartMs + timing.memsetMs + timing.syncEndMs;
    // 写入速度 = 写了多少 MB ÷ 花了多少秒。1 MB = 1024 × 1024 字节，1 秒 = 1000 毫秒
    const double memsetMegabytesPerSecond =
        (static_cast<double>(kHeapBufSizeBytes) / (1024 * 1024)) / (timing.memsetMs / 1000);
    LOGI(kTag, "%-30s sync_start %.3f ms | memset %.3f ms | sync_end %.3f ms | 合计 %.3f ms | 写入 %.0f MB/s", heapPath,
        timing.syncStartMs, timing.memsetMs, timing.syncEndMs, totalMs, memsetMegabytesPerSecond);
}

int run_m3_sync() {
    LOGD(kTag, "run_m3_sync()");
    LOGD(kTag, "===== M3：自己调 DMA_BUF_IOCTL_SYNC（每种 heap %d 轮取平均，每块 %zu 字节）=====", kSyncRepeatCount,
        kHeapBufSizeBytes);

    // 第 1 步：cached heap
    SyncTiming cachedTiming;
    if (!measure_sync_timing(kCachedHeapPath, cachedTiming)) {
        return 1;
    }
    // 第 2 步：uncached heap，做同样的事
    SyncTiming uncachedTiming;
    if (!measure_sync_timing(kUncachedHeapPath, uncachedTiming)) {
        return 1;
    }
    // 第 3 步：两行放在一起打印，方便上下对比
    print_sync_timing(kCachedHeapPath, cachedTiming);
    print_sync_timing(kUncachedHeapPath, uncachedTiming);
    return 0;
}
```

#### 5.3.3 `src/main.cpp` 要加的两处

```cpp
// ① include 区，跟在 m2_two_pointers.h 后面
#include "experiments/m3_sync.h"

// ② main() 里，跟在 m2 那个 if 后面
    if (strcmp(experimentName, "m3") == 0) {
        return run_m3_sync();
    }
```

文件开头的注释里也加一行：`./day3_mmap m3   M3：自己调 DMA_BUF_IOCTL_SYNC，比较 cached 和 uncached → src/experiments/m3_sync.cpp`

#### 5.3.4 运行

```bash
./run_on_board_with_mac.sh m3
```

CLion 里 Program arguments 填 `m3`。

> 2026-10-31 上板通过（用户按 5.3 写成 `m3_sync.cpp/.h`，提交 1272944）。
> 第一次跑时 heap 路径少了开头的 `/`（`"dev/dma_heap/system"`）也成功了，是因为 `adb shell` 的当前目录正好是 `/`；换个目录运行就会 ENOENT，已经改成绝对路径。

### 5.4 实测输出（2026-10-31）和当初的猜测对照

```
I M3: /dev/dma_heap/system           sync_start 0.001 ms | memset 0.349 ms | sync_end 0.001 ms | 合计 0.351 ms | 写入 8601 MB/s
I M3: /dev/dma_heap/system-uncached  sync_start 0.001 ms | memset 0.651 ms | sync_end 0.001 ms | 合计 0.653 ms | 写入 4607 MB/s
```

| | 当初猜测 ⚠️ | 实测 | 对上了吗 |
|---|---|---|---|
| cached 的 sync_start / sync_end | 约 0.2 ms | **0.001 ms** | ❌ 几乎是 0 |
| uncached 的 sync | 约 0.00x ms | 0.001 ms | ✅ |
| cached 的 memset | 0.3xx ms，约 8000 MB/s | 0.349 ms，8601 MB/s | ✅ |
| uncached 的 memset | 3.xxx ms，约 1000 MB/s | **0.651 ms，4607 MB/s** | ❌ 只慢了约 1.9 倍 |

同一天第一次跑的结果是 cached 13740 MB/s、uncached 5045 MB/s（约 2.7 倍）。两次绝对数字差得多，推断是被调度到了不同的核（RK3588 有 4 个 A76 大核、4 个 A55 小核），或者 CPU 频率不同。**只比同一次运行里两行的比值**。

两处没对上的原因：

- **cached 的 sync 几乎是 0**（推断：依据上游 5.10 `drivers/dma-buf/heaps/system_heap.c`，本板 Rockchip 内核源码没核对）：system heap 处理 sync 时，只对"已经 attach 到这块 buffer 的设备"刷 cache。这个实验只是 CPU 自己 mmap 来写，没有任何硬件 attach 过，所以没有要刷的，ioctl 进去就回来了，0.001 ms 基本就是一次系统调用的开销。
- **uncached 的写没有想象中慢**（推断）：uncached heap 映射到用户空间时用的是 write-combine（写合并），连续的写会先在写缓冲里攒成大块再发到内存，所以 memset 这种顺序写还算快。真正慢的是**读**：没有 cache 也没有预取。day3_camera_cache_test 实测 uncached **读** 3 MB 要 3.07 ms，和这里写只要 0.651 ms 对得上。

> 回头看，这个实验设计得不好：buffer 没交给过硬件，看不到"cached 的 sync 很花时间"；只测写不测读，看不到 uncached 最慢的那一面。下次要先上板 probe，确认现象明显再写成实验。

### 5.5 学到了什么（3 句话，也写在 `m3_sync.cpp` 开头的注释里）

1. **dma_buf 的内存分两种**：cached（system）CPU 读写快，但交给硬件前后要 sync；uncached（system-uncached）不用 sync，但 CPU 读写慢。
2. **sync 本身花不花时间，要看这块内存有没有交给过硬件**。只给 CPU 自己用时，cached 的 sync 也几乎是 0（推断，理由见 5.4）。
3. **以后 MPP 申请 buffer 会遇到这个选择**：摄像头 → 编码器、CPU 几乎不碰，用 uncached，省掉 sync；CPU 要改图像（画框、叠字），用 cached，前后记得 sync。MPP 自己对 uncached buffer 也直接跳过 sync（`mpp/base/mpp_buffer.cpp` 第 293 行）。

还有一个没做实验、但值得记住的问题：如果 cached 的 buffer 写完不调 `DMA_BUF_SYNC_END` 就交给编码器，CPU 写的数据可能还在 cache 里，编码器从内存读到的是旧数据（花屏、绿块）。

---

## 6. M4：DRM dumb buffer

**跑完 M4，你能回答：一块图像内存里，每一行占多少字节？是不是就等于 宽 × 4？**

答案是不一定。驱动给的"每行字节数"叫 **pitch**（也叫 stride，摄像头那边叫 `bytesperline`），它可能比 宽 × 4 大，多出来的是每行末尾的填充。后面 MPP 的 `hor_stride`、Day 4 RGA 的 `wstride` 都是这个概念，算错了画面会斜、会花。

顺带还能回答：**这块显存能不能交给别的硬件？** 能：用 PRIME 把它导出成 dma_buf fd，和 M2 摄像头 EXPBUF 是同一件事。

### 6.1 和 mmap、DMA 有什么关系

| | 关系 |
|---|---|
| **mmap** | 这是本程序第 3 种"驱动给的内存"：摄像头（M2）、dma_heap（M1 第 3 步、M3）、DRM 显存（M4）。三种都是"驱动先分配 → 给一个 offset 或 fd → mmap 拿到地址"，只是 ioctl 名字不一样 |
| **DMA** | dumb buffer 是给**显示控制器**（RK3588 叫 VOP）用的：显示时 VOP 自己用 DMA 从这块内存读像素送到屏幕，不经过 CPU。PRIME 导出的 dma_buf fd 还能交给编码器、RGA，和摄像头的 dma_buf 一样用 |

套路和摄像头完全一样：

| | 摄像头（M2） | DRM（M4） |
|---|---|---|
| 打开设备 | `open("/dev/video22")` → `videoFd` | `open("/dev/dri/card0")` → `drmFd` |
| 申请 | `VIDIOC_REQBUFS` | `DRM_IOCTL_MODE_CREATE_DUMB` → handle、pitch、size |
| 问 offset | `VIDIOC_QUERYBUF` → `mem_offset` | `DRM_IOCTL_MODE_MAP_DUMB` → `offset` |
| 映射 | `mmap(videoFd, mem_offset)` | `mmap(drmFd, offset)` |
| 每行字节数 | `bytesperline` | `pitch` |
| 导出成 dma_buf | `VIDIOC_EXPBUF` | `DRM_IOCTL_PRIME_HANDLE_TO_FD` |
| 释放 | `close(videoFd)` | `DRM_IOCTL_MODE_DESTROY_DUMB` → `close(drmFd)` |

### 6.2 任务拆解

对 1920×1080 和 1000×1000 各做一轮（1920 × 4 = 7680 是 64 的倍数，1000 × 4 = 4000 不是）：

|  步  | 做什么 | 看什么 |
|:----:|---|---|
|  1   | CREATE_DUMB → MAP_DUMB → mmap | pitch 和 宽 × 4 一样吗？offset 是什么样的数？ |
|  2   | 按 pitch 写满像素，每个像素写 `行号 × 10000 + 列号` | |
|  3   | 找第 10 行的第一个像素：按 pitch 找一次，按 宽 × 4 找一次 | 读到的是不是第 10 行？ |
|  4   | PRIME 导出成 dma_buf fd，再 mmap 一次 | maps 里两行的路径；A 写 B 读、B 写 A 读对不对得上 |
|  5   | munmap → DESTROY_DUMB；最后 close(drmFd) | |

### 6.3 代码（全部代码，2026-10-31 上板验证过）

一共动 4 个文件：

| 文件 | 做什么 |
|---|---|
| `src/experiments/m4_dumb_buffer.h` | 新建，内容见 6.3.1 |
| `src/experiments/m4_dumb_buffer.cpp` | 新建，内容见 6.3.2 |
| `src/main.cpp` | 加一行 include、加一个 `m4` 分支（6.3.3） |
| `CMakeLists.txt` | `add_executable` 里加上这两个文件（6.3.3） |

函数怎么分工：

```
run_m4_dumb_buffer()                         ← main 调它；open / close drmFd
└── check_dumb_buffer_of_size(1920×1080)、(1000×1000)    一种宽高完整一轮
      ├── create_and_map_dumb()               CREATE_DUMB → MAP_DUMB → mmap，结果放进 DumbBuffer
      ├── fill_pixels_by_pitch()              按 pitch 写满：行号 × 10000 + 列号
      ├── print_row_start_by_two_strides()    第 10 行：按 pitch 找 vs 按 宽 × 4 找
      ├── check_export_to_dma_buf()           PRIME 导出 → mmap(dmaBufFd, 0) → 两个指针互相写读
      │     └── print_maps_line_containing()  （src/common/，现成的）
      └── unmap_and_destroy_dumb()            munmap → DESTROY_DUMB
```

#### 6.3.1 `src/experiments/m4_dumb_buffer.h`

```cpp
/*
 * m4_dumb_buffer.h：M4 实验——从显示驱动（DRM）申请 dumb buffer，看"每行字节数 pitch"是不是等于 宽 × 4
 *
 *   对两种宽度（1920、1000）各做一次：
 *     CREATE_DUMB（申请）→ MAP_DUMB（问 offset）→ mmap → 按 pitch 写满像素
 *     → 第 10 行分别按 pitch 和按 宽 × 4 去找，看读到的是不是第 10 行
 *     → PRIME 导出成 dma_buf fd 再 mmap 一次 → munmap → DESTROY_DUMB（释放）
 *
 * 写法和实测输出见 具体编码day3_mmap的流程.md 第 6 节
 */
#pragma once

/**
 * @brief M4：打开 /dev/dri/card0，申请 1920x1080 和 1000x1000 两块 dumb buffer，比较 pitch 和 宽 × 4
 * @return 0 = 两块都申请、映射、读写、导出成功；1 = 打开设备或任何一块失败
 * @note 只申请显存、CPU 读写，不往屏幕上显示，所以不用接显示器
 */
int run_m4_dumb_buffer();
```

#### 6.3.2 `src/experiments/m4_dumb_buffer.cpp`

`unmap_and_destroy_dumb` 写在 `create_and_map_dumb` 前面：后者失败时要调它，C++ 里函数要先声明才能用。

```cpp
#include "experiments/m4_dumb_buffer.h"

#include "common/log.h"
#include "common/maps_printer.h"
#include <cerrno> // errno
#include <cstdint> // uint32_t、uint64_t
#include <cstring> // strerror
#include <drm/drm.h> // DRM_IOCTL_PRIME_HANDLE_TO_FD、drm_prime_handle、DRM_CLOEXEC、DRM_RDWR
#include <drm/drm_mode.h> // drm_mode_create_dumb、drm_mode_map_dumb、drm_mode_destroy_dumb
#include <fcntl.h> // open、O_RDWR、O_CLOEXEC
#include <sys/ioctl.h> // ioctl
#include <sys/mman.h> // mmap、munmap、PROT_READ、PROT_WRITE、MAP_SHARED、MAP_FAILED
#include <unistd.h> // close

static const char* kTag = "M4";
// DRM（Direct Rendering Manager）：Linux 管显示和 GPU 的那套驱动框架。
//   /dev/dri/card0 是它的设备节点，本板是 Rockchip 的显示驱动（导出的 dma_buf 在 fdinfo 里 exp_name 是 rockchipdrm）
static const char* kDrmDevicePath = "/dev/dri/card0";
// 每个像素写的值 = 行号 × kRowValueFactor + 列号。读回一个值就能一眼看出它是第几行第几列，
//   例如 90928 = 第 9 行第 928 列。宽最大 1920 < 10000，所以列号不会"进位"到行号里
static const uint32_t kRowValueFactor = 10000;
// 拿第几行来比较"按 pitch 找"和"按 宽 × 4 找"：行号越大，错位越多，10 行已经足够看出来
static const uint32_t kCheckRowIndex = 10;

// 一块 dumb buffer 的信息：CREATE_DUMB、MAP_DUMB、mmap 三步写回的东西放在一起
struct DumbBuffer {
    // CREATE_DUMB 写回的编号（GEM handle）。它不是 fd，只在申请它的那个 drmFd 里有意义。0 表示没申请到
    uint32_t handle = 0;
    // CREATE_DUMB 写回的每行字节数（pitch，也叫 stride）。驱动可能给得比 宽 × 4 大，多出来的是每行末尾的填充
    uint32_t pitchBytes = 0;
    // CREATE_DUMB 写回的整块字节数 = pitchBytes × 高
    uint64_t sizeBytes = 0;
    // MAP_DUMB 写回的 offset：给 mmap 第 6 个参数用的编号，不是地址
    uint64_t mmapOffset = 0;
    // mmap(drmFd, mmapOffset) 的返回值：本进程里能直接读写这块显存的地址。nullptr 表示没映射
    void* mmapAddress = nullptr;
};

/**
 * @brief 释放 create_and_map_dumb() 申请的 dumb buffer：先 munmap，再 DESTROY_DUMB
 * @param[in]     drmFd   申请时用的那个 drmFd
 * @param[in,out] dumbBuf 要释放的 buffer，释放后 mmapAddress 置 nullptr、handle 置 0
 * @return 无
 * @note 对只申请了、还没映射的 buffer 也能调（mmapAddress 是 nullptr 就跳过 munmap）
 */
static void unmap_and_destroy_dumb(int drmFd, DumbBuffer& dumbBuf) {
    if (dumbBuf.mmapAddress != nullptr) {
        // munmap(地址, 长度)：长度要和 mmap 时一样
        munmap(dumbBuf.mmapAddress, dumbBuf.sizeBytes);
        dumbBuf.mmapAddress = nullptr;
    }
    if (dumbBuf.handle != 0) {
        // DESTROY_DUMB：告诉驱动"这个 handle 我不用了"。
        //   显存要等所有使用者都放手才真正释放：比如导出的 dma_buf 还有人拿着，就先不释放（引用计数，推断和 M2 一样）
        drm_mode_destroy_dumb destroyDumb = {};
        destroyDumb.handle                = dumbBuf.handle;
        ioctl(drmFd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroyDumb);
        dumbBuf.handle = 0;
    }
}

/**
 * @brief 申请一块 dumb buffer，并 mmap 到本进程
 * @param[in]  drmFd   open("/dev/dri/card0") 得到的 fd
 * @param[in]  width   宽，单位像素
 * @param[in]  height  高，单位像素
 * @param[out] dumbBuf 成功时填好 handle、pitchBytes、sizeBytes、mmapOffset、mmapAddress
 * @return true = 成功；false = 任何一步失败（已经申请到的显存在函数里释放掉）
 * @note 用完要调 unmap_and_destroy_dumb() 释放
 */
static bool create_and_map_dumb(int drmFd, uint32_t width, uint32_t height, DumbBuffer& dumbBuf) {
    // 第 1 步 申请：drm_mode_create_dumb 是 CREATE_DUMB 用的表。
    //   "dumb" = 笨的：只是一块能放像素的内存，GPU 不会在上面做加速运算，CPU 写好后交给显示控制器显示
    //   应用填 width、height、bpp；驱动写回 handle、pitch、size
    drm_mode_create_dumb createDumb = {};
    createDumb.width                = width;
    createDumb.height               = height;
    // bpp（bits per pixel）：每个像素多少位。32 位 = 4 字节，对应 XRGB8888（X 不用、R、G、B 各 8 位）
    createDumb.bpp = 32;
    // DRM_IOCTL_MODE_CREATE_DUMB = DRM_IOWR(0xB2, struct drm_mode_create_dumb)：WR，应用填、内核写回。
    //   这一步驱动就把显存分配好了，和摄像头 REQBUFS 一样：内存有了，但本进程还没有地址
    if (ioctl(drmFd, DRM_IOCTL_MODE_CREATE_DUMB, &createDumb) != 0) {
        LOGE(kTag, "DRM_IOCTL_MODE_CREATE_DUMB %ux%u: %s", width, height, strerror(errno));
        return false;
    }
    dumbBuf.handle     = createDumb.handle;
    dumbBuf.pitchBytes = createDumb.pitch;
    dumbBuf.sizeBytes  = createDumb.size;

    // 第 2 步 问 offset：和摄像头 QUERYBUF 写回 mem_offset 是一回事。
    //   应用填 handle（要哪一块），驱动写回 offset：mmap 时拿它告诉驱动"我要映射的是这一块"
    drm_mode_map_dumb mapDumb = {};
    mapDumb.handle            = dumbBuf.handle;
    // DRM_IOCTL_MODE_MAP_DUMB = DRM_IOWR(0xB3, struct drm_mode_map_dumb)：只是查一个编号，不分配内存
    if (ioctl(drmFd, DRM_IOCTL_MODE_MAP_DUMB, &mapDumb) != 0) {
        LOGE(kTag, "DRM_IOCTL_MODE_MAP_DUMB: %s", strerror(errno));
        unmap_and_destroy_dumb(drmFd, dumbBuf);
        return false;
    }
    dumbBuf.mmapOffset = mapDumb.offset;

    // 第 3 步 映射：和摄像头一样是 mmap(设备 fd, 驱动给的 offset)，6 个参数的意思见 camera_buffers.cpp。
    //   参数 2 用 CREATE_DUMB 写回的 size，不是 宽 × 高 × 4：pitch 比 宽 × 4 大时，size 也跟着大
    //   参数 5 drmFd：内核凭它找到 DRM 驱动；参数 6 mmapOffset：驱动凭它找到是哪一块
    void* mmapAddress = mmap(nullptr, dumbBuf.sizeBytes, PROT_READ | PROT_WRITE, MAP_SHARED, drmFd, dumbBuf.mmapOffset);
    // 失败返回 MAP_FAILED（就是 (void*)-1），不是 nullptr
    if (mmapAddress == MAP_FAILED) {
        LOGE(kTag, "mmap dumb buffer: %s", strerror(errno));
        unmap_and_destroy_dumb(drmFd, dumbBuf);
        return false;
    }
    dumbBuf.mmapAddress = mmapAddress;
    return true;
}

/**
 * @brief 按 pitch 一行一行写满像素，每个像素写 行号 × kRowValueFactor + 列号
 * @param[in] dumbBuf 已经 mmap 好的 dumb buffer
 * @param[in] width   宽，单位像素（每行只写这么多个像素，行末的填充不写）
 * @param[in] height  高，单位像素
 * @return 无
 */
static void fill_pixels_by_pitch(const DumbBuffer& dumbBuf, uint32_t width, uint32_t height) {
    // 用 uint8_t*（按字节走）算每行的起点：pitchBytes 的单位是字节。
    //   如果直接用 uint32_t* 加 rowIndex * pitchBytes，指针会走 4 倍远（指针加 1 = 走一个元素的大小）
    uint8_t* frameStart = static_cast<uint8_t*>(dumbBuf.mmapAddress);
    for (uint32_t rowIndex = 0; rowIndex < height; ++rowIndex) {
        // 第 rowIndex 行的起点 = 整块起点 + 行号 × pitch。这就是 M4 要记住的那一行：乘 pitch，不是乘 宽 × 4
        uint32_t* rowPixels = reinterpret_cast<uint32_t*>(frameStart + rowIndex * dumbBuf.pitchBytes);
        for (uint32_t columnIndex = 0; columnIndex < width; ++columnIndex) {
            rowPixels[columnIndex] = rowIndex * kRowValueFactor + columnIndex;
        }
    }
}

/**
 * @brief 找第 rowIndex 行的第一个像素：按 pitch 找一次、按 宽 × 4 找一次，打印两次读到的是第几行第几列
 * @param[in] dumbBuf  已经用 fill_pixels_by_pitch() 写满的 dumb buffer
 * @param[in] width    宽，单位像素
 * @param[in] rowIndex 要找第几行（从 0 数）
 * @return 无
 * @note pitch 等于 宽 × 4 时两次结果一样；pitch 更大时，按 宽 × 4 找会落到前面某一行的中间
 */
static void print_row_start_by_two_strides(const DumbBuffer& dumbBuf, uint32_t width, uint32_t rowIndex) {
    const uint8_t* frameStart = static_cast<const uint8_t*>(dumbBuf.mmapAddress);
    // 对：按驱动给的 pitch 算这一行的起点
    const uint32_t byPitchValue = *reinterpret_cast<const uint32_t*>(frameStart + rowIndex * dumbBuf.pitchBytes);
    // 错：以为每行正好 宽 × 4 字节、行和行之间没有填充
    const uint32_t byWidthValue = *reinterpret_cast<const uint32_t*>(frameStart + rowIndex * width * 4);
    // 读到的值 ÷ kRowValueFactor = 行号，% kRowValueFactor = 列号（fill_pixels_by_pitch 就是这样写进去的）
    LOGI(kTag,
        "    找第 %u 行第 0 列：按 pitch（%u × %u）读到第 %u 行第 %u 列；按 宽 × 4（%u × %u）读到第 %u 行第 %u 列",
        rowIndex, rowIndex, dumbBuf.pitchBytes, byPitchValue / kRowValueFactor, byPitchValue % kRowValueFactor,
        rowIndex, width * 4, byWidthValue / kRowValueFactor, byWidthValue % kRowValueFactor);
}

/**
 * @brief 把 dumb buffer 导出成 dma_buf fd（PRIME），再 mmap 一次，验证两个指针指向同一块显存
 * @param[in] drmFd   申请 dumb buffer 时用的 drmFd
 * @param[in] dumbBuf 已经 mmap 好的 dumb buffer（pointerA 就是它的 mmapAddress）
 * @return true = 导出、映射成功，并且两边互相看得到对方写的值；false = 任何一步失败或看不到
 * @note 函数里 mmap 的 pointerB 和导出的 dmaBufFd 都在函数结束前释放
 */
static bool check_export_to_dma_buf(int drmFd, const DumbBuffer& dumbBuf) {
    // PRIME：DRM 里"handle ↔ dma_buf fd"互相转换的机制。
    //   handle 只在本 drmFd 里有意义；转成 dma_buf fd 以后，就能交给别的驱动（编码器、RGA）或者别的进程
    drm_prime_handle primeHandle = {};
    primeHandle.handle           = dumbBuf.handle; // 输入：要导出哪一块
    // flags：新 fd 的属性。DRM_CLOEXEC = exec 时自动关掉（和 O_CLOEXEC 一样）；
    //   DRM_RDWR = 导出的 dma_buf 可读可写。不加就是只读，下面 PROT_WRITE 的 mmap 会失败（推断和 M2 只读 EXPBUF 一样是
    //   EACCES）
    primeHandle.flags = DRM_CLOEXEC | DRM_RDWR;
    // DRM_IOCTL_PRIME_HANDLE_TO_FD = DRM_IOWR(0x2d, struct drm_prime_handle)：驱动写回 primeHandle.fd。
    //   和摄像头 VIDIOC_EXPBUF 是同一件事：把驱动自己的 buffer 包成 dma_buf，给一个 fd
    if (ioctl(drmFd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &primeHandle) != 0) {
        LOGE(kTag, "DRM_IOCTL_PRIME_HANDLE_TO_FD: %s", strerror(errno));
        return false;
    }
    const int dmaBufFd = primeHandle.fd;
    LOGI(kTag, "    PRIME 导出：handle %u -> dmaBufFd %d", dumbBuf.handle, dmaBufFd);

    // mmap(dmaBufFd, 0)：dma_buf 的 offset 永远填 0（一个 fd 就是一整块，不用再选哪一块），和 M2 的 pointerB 一样
    void* mmapAddress = mmap(nullptr, dumbBuf.sizeBytes, PROT_READ | PROT_WRITE, MAP_SHARED, dmaBufFd, 0);
    if (mmapAddress == MAP_FAILED) {
        LOGE(kTag, "mmap dmaBufFd: %s", strerror(errno));
        close(dmaBufFd);
        return false;
    }
    uint32_t* pointerA = static_cast<uint32_t*>(dumbBuf.mmapAddress); // mmap(drmFd, mmapOffset) 得到的
    uint32_t* pointerB = static_cast<uint32_t*>(mmapAddress); // mmap(dmaBufFd, 0) 得到的
    print_maps_line_containing("    pointerA", pointerA);
    print_maps_line_containing("    pointerB", pointerB);

    // 两个方向各写一次：A 写、B 读；B 写、A 读。都对上才说明是同一块物理内存
    pointerA[0]             = 0x12345678;
    pointerB[1]             = 0x9abcdef0;
    const bool isSameMemory = pointerB[0] == 0x12345678 && pointerA[1] == 0x9abcdef0;
    LOGI(kTag, "    A 写 0x12345678，B 读到 0x%08x；B 写 0x9abcdef0，A 读到 0x%08x -> %s", pointerB[0], pointerA[1],
        isSameMemory ? "同一块内存" : "不是同一块");

    munmap(mmapAddress, dumbBuf.sizeBytes);
    // close(dmaBufFd)：只是还掉这个 fd，显存还被 drmFd 的 handle 拿着，不会释放
    close(dmaBufFd);
    return isSameMemory;
}

/**
 * @brief 对一种宽高做完整的一轮：申请映射 → 打印 pitch → 写满 → 两种方式找第 kCheckRowIndex 行 → PRIME 导出 → 释放
 * @param[in] drmFd  open("/dev/dri/card0") 得到的 fd
 * @param[in] width  宽，单位像素
 * @param[in] height 高，单位像素，必须大于 kCheckRowIndex
 * @return true = 每一步都成功；false = 任何一步失败
 */
static bool check_dumb_buffer_of_size(int drmFd, uint32_t width, uint32_t height) {
    LOGI(kTag, "----- %u x %u -----", width, height);
    DumbBuffer dumbBuf;
    if (!create_and_map_dumb(drmFd, width, height, dumbBuf)) {
        return false;
    }
    // 第 1 步 看 pitch：驱动给的每行字节数，和 宽 × 4 比一比
    LOGI(kTag, "    handle %u | pitch %u 字节（宽 × 4 = %u，每行多 %u）| size %llu 字节", dumbBuf.handle,
        dumbBuf.pitchBytes, width * 4, dumbBuf.pitchBytes - width * 4,
        static_cast<unsigned long long>(dumbBuf.sizeBytes));
    LOGI(kTag, "    MAP_DUMB offset 0x%llx -> mmapAddress %p", static_cast<unsigned long long>(dumbBuf.mmapOffset),
        dumbBuf.mmapAddress);

    // 第 2 步 写满，再用两种方式找同一行，看错位
    fill_pixels_by_pitch(dumbBuf, width, height);
    print_row_start_by_two_strides(dumbBuf, width, kCheckRowIndex);

    // 第 3 步 导出成 dma_buf，再映射一次
    const bool isExportOk = check_export_to_dma_buf(drmFd, dumbBuf);

    unmap_and_destroy_dumb(drmFd, dumbBuf);
    return isExportOk;
}

int run_m4_dumb_buffer() {
    LOGD(kTag, "run_m4_dumb_buffer()");
    LOGD(kTag, "===== M4：DRM dumb buffer，看 pitch 和 宽 × 4 =====");
    // O_RDWR：要申请、映射可写的显存，必须可读可写打开
    const int drmFd = open(kDrmDevicePath, O_RDWR | O_CLOEXEC);
    if (drmFd < 0) {
        LOGE(kTag, "open %s: %s", kDrmDevicePath, strerror(errno));
        return 1;
    }
    // 1920 × 4 = 7680 = 64 × 120，本来就是 64 的倍数；1000 × 4 = 4000 不是 64 的倍数
    const bool isWideOk = check_dumb_buffer_of_size(drmFd, 1920, 1080);
    const bool isOddOk  = check_dumb_buffer_of_size(drmFd, 1000, 1000);
    // close(drmFd)：没 DESTROY 的 dumb buffer 也会在这时被驱动释放
    close(drmFd);
    return isWideOk && isOddOk ? 0 : 1;
}
```

#### 6.3.3 `src/main.cpp` 和 `CMakeLists.txt` 要加的

```cpp
// main.cpp ① include 区，跟在 m3_sync.h 后面
#include "experiments/m4_dumb_buffer.h"

// main.cpp ② main() 里，跟在 m3 那个 if 后面
    if (strcmp(experimentName, "m4") == 0) {
        return run_m4_dumb_buffer();
    }
```

main.cpp 开头的注释里也加一行：`./day3_mmap m4   M4：DRM dumb buffer，看 pitch 和 宽 × 4 → src/experiments/m4_dumb_buffer.cpp`

```cmake
# CMakeLists.txt 的 add_executable 里，跟在 m3_sync.h 后面
        src/experiments/m4_dumb_buffer.cpp
        src/experiments/m4_dumb_buffer.h
```

`<drm/drm.h>`、`<drm/drm_mode.h>` 是内核头文件，工具链的 sysroot 里就有，**不用链接 libdrm**。

#### 6.3.4 运行

```bash
./run_on_board_with_mac.sh m4
```

CLion 里 Program arguments 填 `m4`。不用接显示器：只申请显存、CPU 读写，不往屏幕上送。

> 2026-10-31 这份代码上板跑过，输出见 6.4。2026-11-02 工作区被还原后按原样重写了一遍：交叉编译通过（`-Wall -Wextra` 没有 warning，最高只用到 GLIBC_2.34），clang-format 过了一遍；当时板子没连着，重写后没有再上板跑。

### 6.4 实测输出（2026-10-31，我用这份参考代码跑的）

```
D M4: run_m4_dumb_buffer()
D M4: ===== M4：DRM dumb buffer，看 pitch 和 宽 × 4 =====
I M4: ----- 1920 x 1080 -----
I M4:     handle 1 | pitch 7680 字节（宽 × 4 = 7680，每行多 0）| size 8294400 字节
I M4:     MAP_DUMB offset 0x100000000 -> mmapAddress 0x7f9a50a000
I M4:     找第 10 行第 0 列：按 pitch（10 × 7680）读到第 10 行第 0 列；按 宽 × 4（10 × 7680）读到第 10 行第 0 列
I M4:     PRIME 导出：handle 1 -> dmaBufFd 4
I Maps:     pointerA   0x7f9a50a000 -> 7f9a50a000-7f9acf3000 rw-s 00000000 00:05 254    /dev/dri/card0
I Maps:     pointerB   0x7f99d21000 -> 7f99d21000-7f9a50a000 rw-s 00000000 00:08 24     /dmabuf:1700-m4test
I M4:     A 写 0x12345678，B 读到 0x12345678；B 写 0x9abcdef0，A 读到 0x9abcdef0 -> 同一块内存
I M4: ----- 1000 x 1000 -----
I M4:     handle 1 | pitch 4032 字节（宽 × 4 = 4000，每行多 32）| size 4032000 字节
I M4:     MAP_DUMB offset 0x100000000 -> mmapAddress 0x7f9a91a000
I M4:     找第 10 行第 0 列：按 pitch（10 × 4032）读到第 10 行第 0 列；按 宽 × 4（10 × 4000）读到第 9 行第 928 列
I M4:     PRIME 导出：handle 1 -> dmaBufFd 4
I Maps:     pointerA   0x7f9a91a000 -> 7f9a91a000-7f9acf3000 rw-s 00000000 00:05 254    /dev/dri/card0
I Maps:     pointerB   0x7f9a541000 -> 7f9a541000-7f9a91a000 rw-s 00000000 00:08 25     /dmabuf:1700-m4test
I M4:     A 写 0x12345678，B 读到 0x12345678；B 写 0x9abcdef0，A 读到 0x9abcdef0 -> 同一块内存
退出码 0
```

（程序名你那边是 `day3_mmap`，`/dmabuf:<pid>-day3_mmap`；地址、pid、inode 每次不一样）

另外单独试过 1001 宽：pitch 也是 4032。

### 6.5 看懂这份输出

1. **pitch 会被补齐**：1920 宽时 pitch = 7680 = 宽 × 4；1000 宽时 pitch = 4032，比 4000 多 32；1001 宽（4004）也是 4032。7680、4032 都是 64 的倍数，推断驱动把每行补到 64 字节的倍数（Rockchip DRM 源码没核对）。原因推断：VOP 用 DMA 一次搬一整块，行首对齐搬得快。
2. **按 宽 × 4 找行会错位**：第 10 行按 宽 × 4 算，起点在 10 × 4000 = 40000 字节处。但按 pitch，第 9 行从 9 × 4032 = 36288 开始，40000 − 36288 = 3712 字节 = 928 个像素，所以读到的是"第 9 行第 928 列"，和输出一样。越往下错得越多，画到屏幕上就是一张斜的图。
3. **offset 只是个编号**：两次都是 `0x100000000`（4 GB），明显不是地址。它是 DRM 编出来给 mmap 用的"假偏移"，和摄像头 `mem_offset` 一个作用。maps 里偏移那一列却是 `00000000`：推断是 DRM 的 mmap 处理完以后把偏移清零了（源码没核对）。
4. **两条 maps 行，同一块内存**：pointerA 是 `00:05 254 /dev/dri/card0`（设备文件，和摄像头 `/dev/video22` 一样）；pointerB 是 `00:08 /dmabuf:<pid>-<程序名>`（dma_buf，和 M2 的 pointerB、M1 第 3 步 dma_heap 一样）。两边互相写读都对得上，说明是同一块物理内存。
5. **handle 两次都是 1**：第一块 DESTROY 以后编号被收回，第二块又拿到 1。handle 只在本 drmFd 里有意义，所以要导出成 fd 才能给别人。

我另外用 `/proc/<pid>/fdinfo/<dmaBufFd>` 看过（探针），`exp_name: rockchipdrm`：这个 dma_buf 是 Rockchip DRM 驱动导出的，摄像头那边是 `videobuf2_cma_sg`。

### 6.6 要回答的问题

1. 1000 宽的图，每行多出来的 32 字节放的是什么？显示时会显示出来吗？（每行末尾的填充，不显示；写图像时不用管它，但算下一行起点时必须跳过它）
2. 如果把 `fill_pixels_by_pitch` 里的 `rowIndex * dumbBuf.pitchBytes` 错写成 `rowIndex * width * 4`，1000 宽的图会怎样？（每一行都往前错 32 字节，送到屏幕上画面是斜的；最后 1000 × 32 字节 ≈ 31 KB 没写到）
3. 要把这块显存交给编码器，应该交 handle 还是 dmaBufFd？为什么？（dmaBufFd：handle 只在本 drmFd 里有意义）

---

## 7. main：按命令行参数选实验

```cpp
/**
 * @brief 按命令行参数选一个实验运行
 * @param[in] argc 参数个数
 * @param[in] argv argv[1] 是 m1 / m2 / m3 / m4
 * @return 实验的返回值；参数不对返回 1
 * @note 比较字符串用 strcmp，不用 atoi（atoi 会链接到 GLIBC_2.38，板子上跑不起来）
 */
int main(int argc, char **argv) {
    const char *experimentName = argc > 1 ? argv[1] : "";
    if (strcmp(experimentName, "m1") == 0) {
        return run_m1_maps();
    }
    if (strcmp(experimentName, "m2") == 0) {
        return run_m2_two_pointers();
    }
    if (strcmp(experimentName, "m3") == 0) {
        return run_m3_sync();
    }
    if (strcmp(experimentName, "m4") == 0) {
        return run_m4_dumb();
    }
    printf("用法：%s m1 | m2 | m3 | m4\n", argv[0]);
    return 1;
}
```

---

## 8. 写完以后的检查清单

| #  | 检查                                      | 怎么做                                                                                                           |
|----|-------------------------------------------|------------------------------------------------------------------------------------------------------------------|
| 1  | 交叉编译没有警告                          | CLion 用 RK3588-Debug profile 编译，看 Build 窗口                                                                |
| 2  | 没有用到 GLIBC_2.38                       | `aarch64-buildroot-linux-gnu-objdump -T cmake-build-rk3588-debug/day3_mmap \| grep GLIBC_2.38`，**没有输出**才对 |
| 3  | 板子连上了                                | `adb devices` 要看到 `3a6aff9a20ba7de4`（不是手机）                                                              |
| 4  | 摄像头没被占着                            | `adb shell 'fuser /dev/video22'` 没有输出（M1、M2 需要）                                                         |
| 5  | 有显示设备                                | `adb shell ls -l /dev/dri/` 能看到 card0（M4 需要）                                                              |
| 6  | 每个 mmap 都有 munmap，每个 fd 都有 close | 对着代码数一遍，顺序和申请相反                                                                                   |

上板跑完以后，把第 3.3、4.4、5.4、6.3 节的"⚠️ 猜测"换成实测输出，并写上日期。
