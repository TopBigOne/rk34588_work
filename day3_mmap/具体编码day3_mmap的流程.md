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
├── 第 5 部分：M3  sync_dma_buf()、measure_sync_once_per_heap()、run_m3_sync()
├── 第 6 部分：M4  create_fill_and_destroy_dumb()、run_m4_dumb()
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

路径是 `/dev/video22`、4 行、每行 0x2fd000、`s`，都和猜的一样。**猜错了一个：偏移列 4 行全是 0，不等于 mem_offset**（原因见 doc/1-理解M1的运行结果.md 第 6.1 节）。第 3 步（dma_heap）还没测。

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

| 步 | 做什么 |
|:---:|---|
| 1 | 写一个函数 `sync_dma_buf(fd, flags)`：填 `dma_buf_sync`，调 `ioctl(fd, DMA_BUF_IOCTL_SYNC, &syncArgs)` |
| 2 | 从 `/dev/dma_heap/system`（cached）申请 3 MB，先 memset 一遍（预热，把物理页分配好） |
| 3 | 重复 10 轮：`sync(START \| WRITE)` → `memset` 写满 → `sync(END \| WRITE)`，三段分别计时 |
| 4 | 打印 10 轮平均 |
| 5 | 对 `/dev/dma_heap/system-uncached` 做同样的事 |

### 5.2 flags 怎么填

`linux/dma-buf.h` 里的定义（sysroot 第 30～34 行）：

| 宏 | 值 | 意思 |
|---|---|---|
| `DMA_BUF_SYNC_READ` | 1 | CPU 要读 |
| `DMA_BUF_SYNC_WRITE` | 2 | CPU 要写 |
| `DMA_BUF_SYNC_START` | 0 | 开始访问之前调 |
| `DMA_BUF_SYNC_END` | 4 | 访问完之后调 |

所以"开始写"是 `START | WRITE`，"写完了"是 `END | WRITE`。START 的值是 0，"或"上去不改变数字，写上只是为了让人读得懂。

### 5.3 代码

```cpp
/**
 * @brief 对一个 dma_buf fd 调一次 DMA_BUF_IOCTL_SYNC
 * @param[in] dmaBufFd dma_buf 的 fd
 * @param[in] flags    DMA_BUF_SYNC_START 或 DMA_BUF_SYNC_END，再"或"上 READ / WRITE
 * @return 成功返回 true；失败打印原因并返回 false
 * @note MPP 的 mpp_buffer_sync_begin / end 就是这一行（osal/mpp_dmabuf.cpp 第 33、49 行）
 */
static bool sync_dma_buf(int dmaBufFd, uint64_t flags) {
    dma_buf_sync syncArgs = {};
    syncArgs.flags = flags;
    if (ioctl(dmaBufFd, DMA_BUF_IOCTL_SYNC, &syncArgs) != 0) {
        printf("DMA_BUF_IOCTL_SYNC flags 0x%llx: %s\n", static_cast<unsigned long long>(flags), strerror(errno));
        return false;
    }
    return true;
}

/**
 * @brief 对一个 dma_heap 测 kSyncRepeatCount 轮：sync_start → memset 写满 → sync_end，各自计时取平均
 * @param[in] heapPath 堆的设备文件
 * @note 正式计时之前先 memset 一遍：第一次写会触发缺页（分配物理页），不算进 memset 的时间
 */
static void measure_sync_once_per_heap(const char *heapPath) {
    DmaHeapBuffer heapBuf;
    if (!alloc_dma_heap(heapPath, kHeapBufSizeBytes, heapBuf)) {
        return;
    }
    memset(heapBuf.mmapAddress, 0, heapBuf.sizeBytes); // 预热：先把物理页都分配好

    double syncStartMs = 0;
    double memsetMs = 0;
    double syncEndMs = 0;
    for (int roundIndex = 0; roundIndex < kSyncRepeatCount; ++roundIndex) {
        // 第 1 步：告诉内核"CPU 要开始写了"
        const double beforeSyncStartMs = now_ms();
        sync_dma_buf(heapBuf.dmaBufFd, DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE);
        const double afterSyncStartMs = now_ms();

        // 第 2 步：CPU 写满整块
        memset(heapBuf.mmapAddress, roundIndex, heapBuf.sizeBytes);
        const double afterMemsetMs = now_ms();

        // 第 3 步：告诉内核"CPU 写完了"，cached 的这里要把 cache 里的数据写回内存
        sync_dma_buf(heapBuf.dmaBufFd, DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE);
        const double afterSyncEndMs = now_ms();

        syncStartMs += afterSyncStartMs - beforeSyncStartMs;
        memsetMs += afterMemsetMs - afterSyncStartMs;
        syncEndMs += afterSyncEndMs - afterMemsetMs;
    }
    printf("%-32s sync_start %.3f ms   memset %.3f ms   sync_end %.3f ms\n", heapPath,
           syncStartMs / kSyncRepeatCount, memsetMs / kSyncRepeatCount, syncEndMs / kSyncRepeatCount);
    free_dma_heap(heapBuf);
}

/**
 * @brief M3：cached（system）和 uncached（system-uncached）两种堆，各测一次 sync 的耗时
 * @return 总是返回 0
 */
static int run_m3_sync() {
    printf("===== M3：自己调 DMA_BUF_IOCTL_SYNC（每项 %d 轮平均，每块 %zu 字节）=====\n", kSyncRepeatCount,
           kHeapBufSizeBytes);
    measure_sync_once_per_heap("/dev/dma_heap/system");
    measure_sync_once_per_heap("/dev/dma_heap/system-uncached");
    return 0;
}
```

### 5.4 期望看到的输出 ⚠️ 猜测（数字只是大概的量级）

```
/dev/dma_heap/system             sync_start 0.2xx ms   memset 0.3xx ms   sync_end 0.2xx ms
/dev/dma_heap/system-uncached    sync_start 0.00x ms   memset 3.xxx ms   sync_end 0.00x ms
```

猜测的理由：
- **cached**：sync 要真的操作 cache（END 时把 cache 里的脏数据写回内存），3 MB 要花一点时间。memset 快，因为写的是 cache
- **uncached**：内核的 system-uncached 堆在 sync 时直接返回，什么都不做，所以几乎是 0。但 memset 慢，因为每个字节都直接写内存。day3_camera_cache_test 实测 uncached 读 3 MB 要 3.07 ms，写应该是同一个量级
- 这就是 M6 里看到的：**cached 省了 CPU 访问的时间，代价是要 sync；uncached 不用 sync，代价是 CPU 访问慢**

### 5.5 要回答的问题

1. cached 那一块：sync_start + memset + sync_end 加起来，和 uncached 的 memset 比，哪个快？
2. uncached 的 sync 为什么几乎不花时间？（内核里 uncached 堆跳过 cache 操作；MPP 也一样，`mpp_buffer.cpp` 第 293～297 行）
3. 如果把 `DMA_BUF_SYNC_END` 那一行删掉，cached 那一块会出什么问题？（CPU 写的数据可能还在 cache 里，编码器硬件从内存读到的是旧数据）

---

## 6. M4：DRM dumb buffer

### 6.1 任务拆解

| 步 | 做什么 | ioctl / 函数 | 传回什么 |
|:---:|---|---|---|
| 1 | 打开显示设备 | `open("/dev/dri/card0")` | `drmFd` |
| 2 | 申请显存：填宽、高、bpp=32 | `DRM_IOCTL_MODE_CREATE_DUMB` | `handle`、`pitch`、`size` |
| 3 | 问驱动"用什么 offset 去 mmap" | `DRM_IOCTL_MODE_MAP_DUMB` | `offset`（假偏移，只是一个编号） |
| 4 | 映射 | `mmap(drmFd, size, offset)` | CPU 指针 |
| 5 | 按 pitch 一行一行写颜色，再读回第一个像素 | | |
| 6 | 释放 | `munmap` → `DRM_IOCTL_MODE_DESTROY_DUMB` → `close` | |

做两次：一次 1920 宽（1920 × 4 = 7680，是 64 的倍数），一次 1000 宽（1000 × 4 = 4000，**不是** 64 的倍数），对比 `pitch` 是不是等于 `width × 4`。

**和摄像头对比**，套路完全一样：

| | 摄像头 | DRM |
|---|---|---|
| 申请 | `VIDIOC_REQBUFS` | `DRM_IOCTL_MODE_CREATE_DUMB` |
| 问 offset | `VIDIOC_QUERYBUF` → `mem_offset` | `DRM_IOCTL_MODE_MAP_DUMB` → `offset` |
| 映射 | `mmap(videoFd, mem_offset)` | `mmap(drmFd, offset)` |
| 每行字节数 | `bytesperline`（stride） | `pitch` |

### 6.2 代码

```cpp
/**
 * @brief 申请一块 dumb buffer，mmap，写满一种颜色，打印 pitch，再释放
 * @param[in] drmFd    /dev/dri/card0 的 fd
 * @param[in] width    宽，单位像素
 * @param[in] height   高，单位像素
 * @param[in] color    XRGB8888 颜色，例如 0x00FF0000 是红色
 * @return 成功返回 true；失败打印原因并返回 false
 */
static bool create_fill_and_destroy_dumb(int drmFd, uint32_t width, uint32_t height, uint32_t color) {
    // 第 1 步：申请显存。填宽、高、每像素位数，驱动传回 handle、pitch（每行字节数）、size
    drm_mode_create_dumb createDumb = {};
    createDumb.width = width;
    createDumb.height = height;
    createDumb.bpp = 32;
    if (ioctl(drmFd, DRM_IOCTL_MODE_CREATE_DUMB, &createDumb) != 0) {
        printf("DRM_IOCTL_MODE_CREATE_DUMB: %s\n", strerror(errno));
        return false;
    }
    printf("%ux%u：handle %u，pitch %u（width × 4 = %u），size %llu\n", width, height, createDumb.handle,
           createDumb.pitch, width * 4, static_cast<unsigned long long>(createDumb.size));

    // 第 2 步：问驱动"这块显存用什么 offset 去 mmap"。这个 offset 是驱动编的"假偏移"，只是个编号
    drm_mode_map_dumb mapDumb = {};
    mapDumb.handle = createDumb.handle;
    bool isOk = ioctl(drmFd, DRM_IOCTL_MODE_MAP_DUMB, &mapDumb) == 0;
    if (!isOk) {
        printf("DRM_IOCTL_MODE_MAP_DUMB: %s\n", strerror(errno));
    }

    // 第 3 步：mmap(drmFd, 假偏移)，和摄像头 mmap(videoFd, mem_offset) 是一个套路
    void *mapped = MAP_FAILED;
    if (isOk) {
        mapped = mmap(nullptr, createDumb.size, PROT_READ | PROT_WRITE, MAP_SHARED, drmFd, mapDumb.offset);
        isOk = mapped != MAP_FAILED;
        if (!isOk) {
            printf("mmap dumb: %s\n", strerror(errno));
        }
    }

    // 第 4 步：按 pitch 一行一行写像素。一行的起点 = 行号 × pitch，不是 行号 × width × 4
    if (isOk) {
        uint8_t *frameStart = static_cast<uint8_t *>(mapped);
        printf("  offset 0x%llx -> mmapAddress %p\n", static_cast<unsigned long long>(mapDumb.offset), mapped);
        print_maps_line_containing("dumb buffer", mapped);
        for (uint32_t rowIndex = 0; rowIndex < height; ++rowIndex) {
            uint32_t *rowPixels = reinterpret_cast<uint32_t *>(frameStart + rowIndex * createDumb.pitch);
            for (uint32_t columnIndex = 0; columnIndex < width; ++columnIndex) {
                rowPixels[columnIndex] = color;
            }
        }
        const uint32_t *firstPixel = reinterpret_cast<const uint32_t *>(frameStart);
        printf("  写完，读回第一个像素：0x%08x\n", *firstPixel);
        munmap(mapped, createDumb.size);
    }

    // 第 5 步：释放显存
    drm_mode_destroy_dumb destroyDumb = {};
    destroyDumb.handle = createDumb.handle;
    ioctl(drmFd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroyDumb);
    return isOk;
}

/**
 * @brief M4：打开 /dev/dri/card0，申请两块不同宽度的 dumb buffer，看 pitch 是不是等于 width × 4
 * @return 成功返回 0；失败返回 1
 */
static int run_m4_dumb() {
    printf("===== M4：DRM dumb buffer =====\n");
    const int drmFd = open(kDrmDevicePath, O_RDWR | O_CLOEXEC);
    if (drmFd < 0) {
        printf("open %s: %s\n", kDrmDevicePath, strerror(errno));
        return 1;
    }
    const bool isWideOk = create_fill_and_destroy_dumb(drmFd, 1920, 1080, 0x00FF0000);
    const bool isOddOk = create_fill_and_destroy_dumb(drmFd, 1000, 100, 0x0000FF00); // 1000 × 4 = 4000，不是 64 的倍数
    close(drmFd);
    return isWideOk && isOddOk ? 0 : 1;
}
```

### 6.3 期望看到的输出 ⚠️ 猜测

```
1920x1080：handle 1，pitch 7680（width × 4 = 7680），size 8294400
  offset 0x10xxxxxxx -> mmapAddress 0x7f9...
  dumb buffer    0x7f9...
    -> 7f9...-7f9... rw-s 10xxxxxxx 00:06 xxx  /dev/dri/card0
  写完，读回第一个像素：0x00ff0000
1000x100：handle 1，pitch 4032（width × 4 = 4000），size 403200
  ...
```

- 1000 宽的 pitch 我猜是 **4032**（往上补齐到 64 的倍数）。Rockchip DRM 驱动申请 dumb buffer 时会把每行字节数对齐，这和摄像头 stride 要对齐是同一个原因：硬件一次搬一整块，喜欢整齐的数字。**要上板看实际数字**
- maps 里那一行的偏移列，应该就是 `MAP_DUMB` 给的那个假偏移

### 6.4 要回答的问题

1. `pitch` 和 `width × 4` 一样吗？不一样的话，差的那些字节是什么？（每行末尾的填充，不显示）
2. 第 4 步写像素时，如果把 `rowIndex * createDumb.pitch` 错写成 `rowIndex * width * 4`，1000 宽那块会怎样？（从第 2 行开始，每行都往前错位 32 字节，画面斜了）
3. maps 里 dumb buffer 那一行的路径是什么？

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

| # | 检查 | 怎么做 |
|---|---|---|
| 1 | 交叉编译没有警告 | CLion 用 RK3588-Debug profile 编译，看 Build 窗口 |
| 2 | 没有用到 GLIBC_2.38 | `aarch64-buildroot-linux-gnu-objdump -T cmake-build-rk3588-debug/day3_mmap \| grep GLIBC_2.38`，**没有输出**才对 |
| 3 | 板子连上了 | `adb devices` 要看到 `3a6aff9a20ba7de4`（不是手机） |
| 4 | 摄像头没被占着 | `adb shell 'fuser /dev/video22'` 没有输出（M1、M2 需要） |
| 5 | 有显示设备 | `adb shell ls -l /dev/dri/` 能看到 card0（M4 需要） |
| 6 | 每个 mmap 都有 munmap，每个 fd 都有 close | 对着代码数一遍，顺序和申请相反 |

上板跑完以后，把第 3.3、4.4、5.4、6.3 节的"⚠️ 猜测"换成实测输出，并写上日期。
