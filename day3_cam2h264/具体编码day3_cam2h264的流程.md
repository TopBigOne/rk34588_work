# 具体编码 day3_cam2h264 的流程

> 目标：IMX415 摄像头 → V4L2 采集 → **零拷贝**交给 MPP 硬件编码 → 录 10 秒 H.264，在 Mac 上能播放。
>
> **这份文档的代码可以直接抄**：每个里程碑都给了完整的 `src/main.cpp`，整个替换掉就行。
> 这些代码 2026-10-16 都在 Mac 上交叉编译过（`-Wall -Wextra` 0 warning），也推到板子上跑过——但那时**还没装摄像头**，所以只验证到"打开设备 → 设格式 → 要缓冲区 → mmap → EXPBUF → import"，到 `VIDIOC_STREAMON` 就因为没有 sensor 失败了（`Operation not permitted`）。STREAMON 之后的部分（取帧、编码）要等装好摄像头再验证，文档里的"应该看到"标了 **未实测** 的就是这部分。
>
> 本文的行号：`camera_source.c` 和 `mpi_enc_test.c` 都对应 `rk_code/external/mpp/`（MPP 官方源码）。

---

## 0. 先放下心理负担

Day 3 新东西只有一个：**V4L2**。编码器直接用 Day 1 的 `MppEncoder`（已经复制到 `src/encoder/`，加了 stride 和旋转两个参数），不用重写。

| 要写的 | 难不难 | 说明 |
|---|---|---|
| V4L2 采集（open → S_FMT → REQBUFS → mmap → QBUF → STREAMON → DQBUF） | ⭐⭐ 新东西 | 全是固定套路：填一个结构体 → `ioctl` → 检查返回值。M2 一次写完 |
| 零拷贝（EXPBUF → `mpp_buffer_import`） | ⭐ | 每块缓冲区多 2 个调用，十几行 |
| 编码 | 不用写 | `encoder.init` / `get_header` / `encode`，和 Day 1 一样 |
| 主循环 | ⭐ | DQBUF → encode → fwrite → QBUF，**QBUF 一定放在 encode 后面** |

**写法原则（和 Day 1 一样）：**
1. **第一遍不写类**，全写在 `src/main.cpp` 里（"面条代码"）。资源放全局变量，最后由 `cleanup()` 统一释放
2. **每次只写一个里程碑**，上板跑通、看到预期输出，再写下一个
3. **每个 `ioctl` 都检查返回值**：用 `V4L2_CHECK` 宏，失败会打印是哪个命令、什么原因、第几行
4. **每完成一个里程碑就 commit 一次**

> 为什么不用 Day 1 的 `goto CLEANUP`？C++ 里 `goto` 不能跳过带初始化的变量声明（编译报错 "jump bypasses variable initialization"），V4L2 这里要声明很多结构体。所以改成：`run()` 里出错直接 `return -1`，`main()` 里不管成功失败都调用一次 `cleanup()`。

---

## 1. 整体调用流程（先在脑子里有这张图）

```
main()
 ├─ run()
 │   ① open("/dev/video31")  →  VIDIOC_QUERYCAP（检查支持 MPLANE 采集和 streaming）
 │   ② VIDIOC_S_FMT（1920x1080 NV12）→ VIDIOC_G_FMT（⭐ 读回 bytesperline、sizeimage）
 │   ③ VIDIOC_REQBUFS（4 块，MMAP）
 │   ④ 每块 i：VIDIOC_QUERYBUF → mmap（CPU 地址）
 │             → VIDIOC_EXPBUF（dma_buf fd）
 │             → mpp_buffer_import（MppBuffer）        ← M3 新增
 │   ⑤ VIDIOC_QBUF × 4 → VIDIOC_STREAMON
 │   ⑥ encoder.init（stride 用 ② 读回来的）→ get_header → fwrite                  ← M3 新增
 │   ⑦ 丢掉前 30 帧：DQBUF → QBUF
 │   ⑧ 循环：DQBUF → encoder.encode(g_bufs[idx].mppBuf) → QBUF → fwrite           ← M2 只取 1 帧存文件
 │   ⑨ encode(EOS) 直到 packet.eos                                                ← M3 新增
 └─ cleanup()：STREAMOFF → mpp_buffer_put → munmap → close(dma_buf fd) → close(video fd) → fclose
```

一块摄像头缓冲区有三个"把手"，都存在 `CamBuffer` 里（guide/02 第 2.2 节）：

| 把手          | 字段 | 从哪来 | 给谁用 |
|---------------|---|---|---|
| ① CPU 地址    | `start` | `mmap` | CPU：M2 存文件、M6 拷贝 |
| ② dma_buf fd  | `dmaFd` | `VIDIOC_EXPBUF` | 任何支持 dma_buf 的硬件 |
| ③ MppBuffer   | `mppBuf` | `mpp_buffer_import` | 编码器 |

---

## 2. 准备工作（写 M2 之前）

### 2.1 已经准备好的
| 东西    | 位置                          | 说明 |
|---------|-------------------------------|---|
| CMake   | `CMakeLists.txt`              | 已经编译 `src/main.cpp` 和 `src/encoder/mpp_encoder.cpp`，链接 MPP，rpath 指向 `/userdata/mpp_build/lib` |
| 编码器  | `src/encoder/mpp_encoder.*`   | 从 Day 1 复制，`EncoderConfig` 加了 `horStride`、`verStride`、`rotation` 三个字段（默认 0，不填和 Day 1 一样） |
| 工具宏  | `src/common/mpp_utils.h`      | 编码器要用的 `ALIGN`、`MPP_CHECK`、RAII 句柄 |
| 脚本    | `run_on_board_with_mac.sh` 等 | 编译 → 推到板子 → 运行，全走 adb |

### 2.2 你要在 CLion 里做的
加一个 `RK3588-Debug` profile（guide/01 第 5.1 节），Build directory 填 `cmake-build-rk3588-debug`。

### 2.3 每次开始前
```bash
adb shell 'dmesg | grep imx415 | grep -i unexpected'   # 插的那个接口不应该再报
adb shell 'pidof rkaiq_3A_server'                      # 有 pid
adb shell 'v4l2-ctl --list-devices'                    # 确认节点，代码里写死的是 /dev/video31
```
节点不是 `video31` 的话，改代码第一段的 `kDevice`（M5 以后用 `-d` 参数）。

---

## 3. M2：V4L2 采集一帧，存成 NV12 文件（约 220 行，**今天的难点**）

M2 不编码，只验证"我能从摄像头拿到画面"。拿到的那一帧存成文件，拉回 Mac 用 ffplay 看。

### 步骤
| #   | 做什么                 | 关键调用                                      | 对照官方                                         |
|:---:|------------------------|-----------------------------------------------|--------------------------------------------------|
|  ①  | 打开设备，检查能力     | `open` → `VIDIOC_QUERYCAP`                    | `camera_source.c` 128、145～158                  |
|  ②  | 设格式，读回实际值     | `VIDIOC_S_FMT` → `VIDIOC_G_FMT`               | 161～206                                         |
|  ③  | 要 4 块缓冲区          | `VIDIOC_REQBUFS`                              | 209～221                                         |
|  ④  | 每块查长度、mmap       | `VIDIOC_QUERYBUF` → `mmap`                    | 224～264                                         |
|  ⑤  | 全部交给驱动，开始采集 | `VIDIOC_QBUF` × 4 → `VIDIOC_STREAMON`         | 287～311                                         |
|  ⑥  | 丢掉前 30 帧           | `VIDIOC_DQBUF` → `VIDIOC_QBUF`                | 314～318（官方丢 4 帧，mpi_enc_test 再丢 50 帧） |
|  ⑦  | 取一帧存文件，还回去   | `DQBUF` → `fwrite(start, bytesused)` → `QBUF` | 367～427                                         |

### Multiplanar 的三个注意点（不注意就 `EINVAL`）
板子上的 video 节点是 **Multiplanar** 类型（2026-10-15 实测）：
1. 类型用 `V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE`（代码里统一用 `kBufType`）
2. 格式读写 `fmt.fmt.pix_mp`，每行字节数在 `fmt.fmt.pix_mp.plane_fmt[0].bytesperline`
3. 每次 `QUERYBUF` / `QBUF` / `DQBUF`，`v4l2_buffer` 都要挂上 `planes` 数组：`buf.m.planes = planes; buf.length = 1;`。长度、偏移、实际字节数都在 `planes[0]` 里，不在 `buf` 里

### 完整代码（整个替换 `src/main.cpp`）

```cpp
// day3_cam2h264 M2：用 V4L2 从摄像头取一帧 NV12，存成文件（还不编码）
#include <cerrno>   // errno
#include <cstdint>  // uint32_t
#include <cstdio>   // printf / fopen / fwrite
#include <cstring>  // strerror

#include <fcntl.h>           // open / O_RDWR / O_CLOEXEC
#include <linux/videodev2.h> // V4L2 的所有结构体和 VIDIOC_xxx 命令
#include <sys/ioctl.h>       // ioctl
#include <sys/mman.h>        // mmap / munmap
#include <unistd.h>          // close

// ==================== 参数（M2 先写死，M5 再改成命令行参数） ====================
static const char* kDevice     = "/dev/video31";               // 摄像头节点（以 guide/01 第 2.3 节查到的为准）
static const int kWidth        = 1920;
static const int kHeight       = 1080;
static const int kSkipFrames   = 30;                           // 开头丢掉多少帧（自动曝光还没稳定）
static const char* kOutputFile = "/userdata/av/cam_m2.nv12";

static const int kBufCount = 4; // 向驱动要几块缓冲区，和官方 mpi_enc_test 一样用 4 块
// 板子上的 video 节点是 Multiplanar 类型（guide/02 第 1 节实测），所有地方都用这个类型
static const v4l2_buf_type kBufType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

// ==================== 全局状态（面条代码：资源都放全局，最后 cleanup() 统一释放） ====================
struct CamBuffer {
    void* start   = MAP_FAILED; // CPU 地址（mmap 出来的）
    size_t length = 0;          // mmap 的长度，munmap 时要用
};

static int g_videoFd = -1;          // /dev/video31 的文件描述符
static CamBuffer g_bufs[kBufCount]; // 4 块缓冲区
static bool g_streaming = false;    // 有没有 STREAMON，cleanup 时决定要不要 STREAMOFF
static FILE* g_out      = nullptr;  // 输出的 .nv12 文件

// ioctl 被信号打断（EINTR）时重试，和 camera_source.c 第 91 行的 camera_source_ioctl 一样的用途
static int xioctl(int fd, unsigned long request, void* arg) {
    int ret;
    do {
        ret = ioctl(fd, request, arg);
    } while (ret == -1 && errno == EINTR);
    return ret;
}

// 调用一个 V4L2 ioctl，失败就打印是哪一句、错误原因，然后 return -1（run() 返回后由 cleanup() 释放资源）
#define V4L2_CHECK(request, arg)                                                                          \
    do {                                                                                                  \
        if (xioctl(g_videoFd, request, arg) == -1) {                                                      \
            printf("[FAIL] %s: %s (line %d)\n", #request, strerror(errno), __LINE__);                     \
            return -1;                                                                                    \
        }                                                                                                 \
    } while (0)

// 释放顺序：STREAMOFF → munmap → close(video fd)
static void cleanup() {
    if (g_streaming) {
        v4l2_buf_type type = kBufType;
        xioctl(g_videoFd, VIDIOC_STREAMOFF, &type);
        g_streaming = false;
    }
    for (CamBuffer& b : g_bufs) {
        if (b.start != MAP_FAILED) {
            munmap(b.start, b.length);
            b.start = MAP_FAILED;
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

// VIDIOC_QBUF：把第 index 块缓冲区交给驱动，"往这里写下一帧"
static bool queue_buffer(int index) {
    v4l2_plane planes[1] = {};
    v4l2_buffer buf      = {};
    buf.type             = kBufType;
    buf.memory           = V4L2_MEMORY_MMAP;
    buf.index            = index;
    buf.m.planes         = planes; // ⚠️ MPLANE 类型必须挂上 planes 数组
    buf.length           = 1;      //    并告诉驱动有几个 plane（NV12 只有 1 个），否则返回 EINVAL
    if (xioctl(g_videoFd, VIDIOC_QBUF, &buf) == -1) {
        printf("[FAIL] VIDIOC_QBUF %d: %s\n", index, strerror(errno));
        return false;
    }
    return true;
}

// VIDIOC_DQBUF：取出一块写好画面的缓冲区（没有就阻塞等），返回它的序号；失败返回 -1
// bytesUsed：这一帧实际有多少字节（NV12 1080p 应该是 3,110,400）
static int dequeue_buffer(uint32_t* bytesUsed) {
    v4l2_plane planes[1] = {};
    v4l2_buffer buf      = {};
    buf.type             = kBufType;
    buf.memory           = V4L2_MEMORY_MMAP;
    buf.m.planes         = planes;
    buf.length           = 1;
    if (xioctl(g_videoFd, VIDIOC_DQBUF, &buf) == -1) {
        printf("[FAIL] VIDIOC_DQBUF: %s\n", strerror(errno));
        return -1;
    }
    if (buf.index >= kBufCount) { // camera_source.c 第 388 行写成了 >，差 1
        printf("[FAIL] VIDIOC_DQBUF 返回的序号 %u 越界\n", buf.index);
        return -1;
    }
    if (bytesUsed) {
        *bytesUsed = planes[0].bytesused;
    }
    return static_cast<int>(buf.index);
}

static int run() {
    // ==================== ① 打开摄像头，查能力 ====================
    g_videoFd = open(kDevice, O_RDWR | O_CLOEXEC);
    if (g_videoFd < 0) {
        printf("[FAIL] open %s: %s\n", kDevice, strerror(errno));
        return -1;
    }
    v4l2_capability cap = {};
    V4L2_CHECK(VIDIOC_QUERYCAP, &cap);
    printf("device %s: driver %s, card %s\n", kDevice, cap.driver, cap.card);
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) || !(cap.capabilities & V4L2_CAP_STREAMING)) {
        printf("[FAIL] %s 不支持 Multiplanar 采集或者 streaming\n", kDevice);
        return -1;
    }

    // ==================== ② 设格式，再读回实际值 ====================
    v4l2_format fmt            = {};
    fmt.type                   = kBufType;
    fmt.fmt.pix_mp.width       = kWidth;
    fmt.fmt.pix_mp.height      = kHeight;
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    fmt.fmt.pix_mp.field       = V4L2_FIELD_NONE;
    fmt.fmt.pix_mp.num_planes  = 1;
    V4L2_CHECK(VIDIOC_S_FMT, &fmt);
    V4L2_CHECK(VIDIOC_G_FMT, &fmt); // 驱动可能调整了宽高、每行字节数，永远以读回来的为准
    printf("format: %ux%u NV12, planes %u, bytesperline %u, sizeimage %u\n", fmt.fmt.pix_mp.width,
        fmt.fmt.pix_mp.height, fmt.fmt.pix_mp.num_planes, fmt.fmt.pix_mp.plane_fmt[0].bytesperline,
        fmt.fmt.pix_mp.plane_fmt[0].sizeimage);
    if (fmt.fmt.pix_mp.pixelformat != V4L2_PIX_FMT_NV12 || fmt.fmt.pix_mp.num_planes != 1) {
        printf("[FAIL] 驱动没有给 1 个 plane 的 NV12\n");
        return -1;
    }

    // ==================== ③ 要 4 块缓冲区 ====================
    v4l2_requestbuffers req = {};
    req.count               = kBufCount;
    req.type                = kBufType;
    req.memory              = V4L2_MEMORY_MMAP; // 缓冲区由驱动分配，我们 mmap 过来用
    V4L2_CHECK(VIDIOC_REQBUFS, &req);
    if (req.count != kBufCount) {
        printf("[FAIL] 要 %d 块缓冲区，驱动给了 %u 块\n", kBufCount, req.count);
        return -1;
    }

    // ==================== ④ 每块缓冲区：QUERYBUF + mmap ====================
    for (int i = 0; i < kBufCount; i++) {
        v4l2_plane planes[1] = {};
        v4l2_buffer buf      = {};
        buf.type             = kBufType;
        buf.memory           = V4L2_MEMORY_MMAP;
        buf.index            = i;
        buf.m.planes         = planes;
        buf.length           = 1;
        V4L2_CHECK(VIDIOC_QUERYBUF, &buf); // 查第 i 块的长度和 mmap 偏移

        g_bufs[i].length = planes[0].length;
        g_bufs[i].start  = mmap(nullptr, planes[0].length, PROT_READ | PROT_WRITE, MAP_SHARED, g_videoFd,
             planes[0].m.mem_offset);
        if (g_bufs[i].start == MAP_FAILED) {
            printf("[FAIL] mmap buffer %d: %s\n", i, strerror(errno));
            return -1;
        }
        printf("buffer %d: length %zu\n", i, g_bufs[i].length);
    }

    // ==================== ⑤ 4 块全部交给驱动，开始采集 ====================
    for (int i = 0; i < kBufCount; i++) {
        if (!queue_buffer(i)) {
            return -1;
        }
    }
    v4l2_buf_type type = kBufType;
    V4L2_CHECK(VIDIOC_STREAMON, &type);
    g_streaming = true;

    // ==================== ⑥ 丢掉开头的帧（自动曝光还没稳定） ====================
    for (int i = 0; i < kSkipFrames; i++) {
        const int index = dequeue_buffer(nullptr);
        if (index < 0 || !queue_buffer(index)) {
            return -1;
        }
    }

    // ==================== ⑦ 取一帧，存文件，还回去 ====================
    uint32_t bytesUsed = 0;
    const int index    = dequeue_buffer(&bytesUsed);
    if (index < 0) {
        return -1;
    }
    g_out = fopen(kOutputFile, "wb");
    if (!g_out) {
        printf("[FAIL] fopen %s: %s\n", kOutputFile, strerror(errno));
        queue_buffer(index);
        return -1;
    }
    // 只写 bytesused 个字节（= sizeimage），不是整块缓冲区（length 可能更大）
    fwrite(g_bufs[index].start, 1, bytesUsed, g_out);
    printf("frame: buffer %d, %u bytes -> %s\n", index, bytesUsed, kOutputFile);
    if (!queue_buffer(index)) {
        return -1;
    }
    return 0;
}

int main() {
    const int ret = run(); // 任何一步失败都直接 return -1
    cleanup();             // 不管成功失败，都在这里统一释放摄像头资源
    return ret == 0 ? 0 : 1;
}
```

### 运行

```bash
cd /Users/dev/Documents/AV/rk_work/day3_cam2h264
./run_on_board_with_mac.sh
```

应该看到：
```
device /dev/video31: driver rkisp_v6, card rkisp_mainpath        ← 2026-10-16 实测
format: 1920x1080 NV12, planes 1, bytesperline 1920, sizeimage 3110400   ← 实测
buffer 0: length 3133440                                         ← 实测（为什么比 sizeimage 大，见 3.1）
buffer 1: length 3133440
buffer 2: length 3133440
buffer 3: length 3133440
frame: buffer 2, 3110400 bytes -> /userdata/av/cam_m2.nv12      ← 未实测：装好摄像头才有这一行
```

没装摄像头时，最后一行会变成 `[FAIL] VIDIOC_STREAMON: Operation not permitted`（实测）。

拉回 Mac 看画面：
```bash
./run_move_file_from_board_via_usb.sh /userdata/av/cam_m2.nv12
ffplay -f rawvideo -pixel_format nv12 -video_size 1920x1080 result_from_board/cam_m2.nv12
```
画面正常、颜色正常（不发绿、不发紫）就是 M2 通过。和 guide/01 第 4 节用 `v4l2-ctl` 抓的那一帧比一比，应该一样。

### 3.1 为什么缓冲区长度 3,133,440 比 sizeimage 3,110,400 大？

2026-10-16 实测：G_FMT 说一帧 `sizeimage` = 3,110,400 = 1920 × **1080** × 3/2，但 QUERYBUF 给的缓冲区长度是 3,133,440 = 1920 × **1088** × 3/2。

也就是说，**驱动分配内存时把高度对齐到了 16（1088），但图像本身按 1080 行排**：Y 占前 1080 行，UV 紧跟在第 1080 行后面，最后多出来的 8 行 × 1.5 = 23,040 字节没用。Rockchip 的 rkisp 驱动这样做是为了让编码器（按 16 行一个宏块读）读到最后一行宏块时不越界（按驱动源码的写法推断，本地没有内核源码，未核对行号）。

所以：
- 存文件时写 **`bytesused`**（= 3,110,400），不是 `length`
- 编码器的 `verStride` 填 **1080**（= sizeimage ÷ bytesperline × 2/3），不是 1088。UV 到底从第几行开始，M2 播放时颜色对不对就能确认：用 `-video_size 1920x1080` 播放颜色正常，就说明 UV 在第 1080 行

### 易错点
- 忘了挂 `planes` 数组 → `QUERYBUF` / `QBUF` 返回 `Invalid argument`
- 写文件写成 `g_bufs[index].length` → 文件多出 23,040 字节，ffplay 播放时画面往上滚
- `QBUF` 忘了调用 → 4 块缓冲区都在你手里，驱动没地方写，第 5 次 `DQBUF` 永远等不到
- 节点写错（摄像头接到 `video22` 那个 ISP 上）→ `STREAMON` 返回 `Operation not permitted`，和没装摄像头一样

---

### 3.2 自己怎么测试 M2

见 [验证代码运行结果/验证M2代码.md](验证代码运行结果/验证M2代码.md)：正常流程 6 项、故意制造错误 4 项、资源泄漏、单步调试，以及测试记录。

---

## 4. M3：零拷贝导入 + 编码 1 帧（在 M2 上加约 90 行）

### M3 比 M2 多了什么
| 位置 | 新增 | 说明 |
|---|---|---|
| `#include` | `<vector>`、`"encoder/mpp_encoder.h"` | Day 1 的编码器 |
| `CamBuffer` | `dmaFd`、`mppBuf` 两个字段 | 把手 ② 和 ③ |
| `cleanup()` | `mpp_buffer_put`、`close(dmaFd)` | 顺序：STREAMOFF → put → munmap → close(dma fd) → close(video fd) |
| ④ 循环里 | `VIDIOC_EXPBUF` → `mpp_buffer_import` | **零拷贝的关键**，对照 `camera_source.c` 266～284 |
| ⑥ | `EncoderConfig` + `encoder.init` + `get_header` | ⭐ `horStride` / `verStride` 用 G_FMT 读回来的 |
| ⑧ | `encoder.encode(g_bufs[index].mppBuf, ...)` → **然后** `QBUF` | 编码器直接读摄像头的内存 |
| ⑨ | 送 EOS，取到 `packet.eos` 为止 | 和 Day 1 一样 |

M3 的代码已经写成循环了，`kFrames = 1` 时只编 1 帧；M4 只要把它改成 300。

### 完整代码（整个替换 `src/main.cpp`）

```cpp
// day3_cam2h264 M3：摄像头 → V4L2（MMAP + EXPBUF）→ mpp_buffer_import（零拷贝）→ MppEncoder，先只编 1 帧
#include <cerrno>   // errno
#include <cstdint>  // uint8_t / uint32_t
#include <cstdio>   // printf / fopen / fwrite
#include <cstring>  // strerror
#include <vector>

#include <fcntl.h>           // open / O_RDWR / O_CLOEXEC
#include <linux/videodev2.h> // V4L2 的所有结构体和 VIDIOC_xxx 命令
#include <sys/ioctl.h>       // ioctl
#include <sys/mman.h>        // mmap / munmap
#include <unistd.h>          // close

#include "encoder/mpp_encoder.h" // Day 1 的编码器（加了 horStride / verStride / rotation）

// ==================== 参数（先写死，M5 再改成命令行参数） ====================
static const char* kDevice     = "/dev/video31";       // 摄像头节点（以 guide/01 第 2.3 节查到的为准）
static const int kWidth        = 1920;
static const int kHeight       = 1080;
static const int kFrames       = 1;                       // 编多少帧
static const int kSkipFrames   = 30;                   // 开头丢掉多少帧（自动曝光还没稳定）
static const char* kOutputFile = "/userdata/av/cam_m3.h264";

static const int kBufCount = 4; // 向驱动要几块缓冲区，和官方 mpi_enc_test 一样用 4 块
// 板子上的 video 节点是 Multiplanar 类型（guide/02 第 1 节实测），所有地方都用这个类型
static const v4l2_buf_type kBufType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

// ==================== 全局状态（面条代码：资源都放全局，最后 cleanup() 统一释放） ====================
// 一块摄像头缓冲区的三个"把手"（guide/02 第 2.2 节）
struct CamBuffer {
    void* start      = MAP_FAILED; // ① CPU 地址（mmap 出来的）
    size_t length    = 0;          //    mmap 的长度，munmap 时要用
    int dmaFd        = -1;         // ② dma_buf fd（VIDIOC_EXPBUF 导出的），别的硬件凭它访问这块内存
    MppBuffer mppBuf = nullptr;    // ③ MppBuffer（mpp_buffer_import 包装的），交给编码器用
};

static int g_videoFd = -1;              // /dev/video31 的文件描述符
static CamBuffer g_bufs[kBufCount];     // 4 块缓冲区
static bool g_streaming = false;        // 有没有 STREAMON，cleanup 时决定要不要 STREAMOFF
static FILE* g_out      = nullptr;      // 输出的 .h264 文件

// ioctl 被信号打断（EINTR）时重试，和 camera_source.c 第 91 行的 camera_source_ioctl 一样的用途
static int xioctl(int fd, unsigned long request, void* arg) {
    int ret;
    do {
        ret = ioctl(fd, request, arg);
    } while (ret == -1 && errno == EINTR);
    return ret;
}

// 调用一个 V4L2 ioctl，失败就打印是哪一句、错误原因，然后 return -1（run() 返回后由 cleanup() 释放资源）
#define V4L2_CHECK(request, arg)                                                                          \
    do {                                                                                                  \
        if (xioctl(g_videoFd, request, arg) == -1) {                                                      \
            printf("[FAIL] %s: %s (line %d)\n", #request, strerror(errno), __LINE__);                     \
            return -1;                                                                                    \
        }                                                                                                 \
    } while (0)

// 释放顺序和 camera_source.c 第 329～364 行的 camera_source_deinit 一样：
// STREAMOFF → mpp_buffer_put → munmap → close(dma_buf fd) → close(video fd)
static void cleanup() {
    if (g_streaming) {
        v4l2_buf_type type = kBufType;
        xioctl(g_videoFd, VIDIOC_STREAMOFF, &type);
        g_streaming = false;
    }
    for (CamBuffer& b : g_bufs) {
        if (b.mppBuf) {
            mpp_buffer_put(b.mppBuf); // 只是解除 MPP 的引用，内存本身是摄像头驱动的
            b.mppBuf = nullptr;
        }
        if (b.start != MAP_FAILED) {
            munmap(b.start, b.length);
            b.start = MAP_FAILED;
        }
        if (b.dmaFd >= 0) {
            close(b.dmaFd);
            b.dmaFd = -1;
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

// VIDIOC_QBUF：把第 index 块缓冲区交给驱动，"往这里写下一帧"
static bool queue_buffer(int index) {
    v4l2_plane planes[1] = {};
    v4l2_buffer buf      = {};
    buf.type             = kBufType;
    buf.memory           = V4L2_MEMORY_MMAP;
    buf.index            = index;
    buf.m.planes         = planes; // ⚠️ MPLANE 类型必须挂上 planes 数组
    buf.length           = 1;      //    并告诉驱动有几个 plane（NV12 只有 1 个），否则返回 EINVAL
    if (xioctl(g_videoFd, VIDIOC_QBUF, &buf) == -1) {
        printf("[FAIL] VIDIOC_QBUF %d: %s\n", index, strerror(errno));
        return false;
    }
    return true;
}

// VIDIOC_DQBUF：取出一块写好画面的缓冲区（没有就阻塞等），返回它的序号；失败返回 -1
// bytesUsed：这一帧实际有多少字节（NV12 1080p 应该是 3,110,400）
static int dequeue_buffer(uint32_t* bytesUsed) {
    v4l2_plane planes[1] = {};
    v4l2_buffer buf      = {};
    buf.type             = kBufType;
    buf.memory           = V4L2_MEMORY_MMAP;
    buf.m.planes         = planes;
    buf.length           = 1;
    if (xioctl(g_videoFd, VIDIOC_DQBUF, &buf) == -1) {
        printf("[FAIL] VIDIOC_DQBUF: %s\n", strerror(errno));
        return -1;
    }
    if (buf.index >= kBufCount) { // camera_source.c 第 388 行写成了 >，差 1
        printf("[FAIL] VIDIOC_DQBUF 返回的序号 %u 越界\n", buf.index);
        return -1;
    }
    if (bytesUsed) {
        *bytesUsed = planes[0].bytesused;
    }
    return static_cast<int>(buf.index);
}

static int run() {
    // ==================== ① 打开摄像头，查能力 ====================
    g_videoFd = open(kDevice, O_RDWR | O_CLOEXEC);
    if (g_videoFd < 0) {
        printf("[FAIL] open %s: %s\n", kDevice, strerror(errno));
        return -1;
    }
    v4l2_capability cap = {};
    V4L2_CHECK(VIDIOC_QUERYCAP, &cap);
    printf("device %s: driver %s, card %s\n", kDevice, cap.driver, cap.card);
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) || !(cap.capabilities & V4L2_CAP_STREAMING)) {
        printf("[FAIL] %s 不支持 Multiplanar 采集或者 streaming\n", kDevice);
        return -1;
    }

    // ==================== ② 设格式，再读回实际值 ====================
    v4l2_format fmt           = {};
    fmt.type                  = kBufType;
    fmt.fmt.pix_mp.width       = kWidth;
    fmt.fmt.pix_mp.height      = kHeight;
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    fmt.fmt.pix_mp.field       = V4L2_FIELD_NONE;
    fmt.fmt.pix_mp.num_planes  = 1;
    V4L2_CHECK(VIDIOC_S_FMT, &fmt);
    V4L2_CHECK(VIDIOC_G_FMT, &fmt); // 驱动可能调整了宽高、每行字节数，永远以读回来的为准
    const int width             = static_cast<int>(fmt.fmt.pix_mp.width);
    const int height            = static_cast<int>(fmt.fmt.pix_mp.height);
    const int bytesPerLine      = static_cast<int>(fmt.fmt.pix_mp.plane_fmt[0].bytesperline);
    const uint32_t sizeImage    = fmt.fmt.pix_mp.plane_fmt[0].sizeimage;
    printf("format: %dx%d NV12, planes %u, bytesperline %d, sizeimage %u\n", width, height,
        fmt.fmt.pix_mp.num_planes, bytesPerLine, sizeImage);
    if (fmt.fmt.pix_mp.pixelformat != V4L2_PIX_FMT_NV12 || fmt.fmt.pix_mp.num_planes != 1 || bytesPerLine <= 0) {
        printf("[FAIL] 驱动没有给 1 个 plane 的 NV12\n");
        return -1;
    }

    // ==================== ③ 要 4 块缓冲区 ====================
    v4l2_requestbuffers req = {};
    req.count               = kBufCount;
    req.type                = kBufType;
    req.memory              = V4L2_MEMORY_MMAP; // 缓冲区由驱动分配，我们 mmap 过来用
    V4L2_CHECK(VIDIOC_REQBUFS, &req);
    if (req.count != kBufCount) {
        printf("[FAIL] 要 %d 块缓冲区，驱动给了 %u 块\n", kBufCount, req.count);
        return -1;
    }

    // ==================== ④ 每块缓冲区：QUERYBUF + mmap + EXPBUF + import ====================
    for (int i = 0; i < kBufCount; i++) {
        // 4.1 查第 i 块的长度和 mmap 偏移
        v4l2_plane planes[1] = {};
        v4l2_buffer buf      = {};
        buf.type             = kBufType;
        buf.memory           = V4L2_MEMORY_MMAP;
        buf.index            = i;
        buf.m.planes         = planes;
        buf.length           = 1;
        V4L2_CHECK(VIDIOC_QUERYBUF, &buf);

        // 4.2 把手 ①：mmap 成 CPU 地址（M5 用不到，M2 存文件、M6 拷贝时用）
        g_bufs[i].length = planes[0].length;
        g_bufs[i].start  = mmap(nullptr, planes[0].length, PROT_READ | PROT_WRITE, MAP_SHARED, g_videoFd,
             planes[0].m.mem_offset);
        if (g_bufs[i].start == MAP_FAILED) {
            printf("[FAIL] mmap buffer %d: %s\n", i, strerror(errno));
            return -1;
        }

        // 4.3 把手 ②：导出成 dma_buf fd（camera_source.c 第 266～271 行）
        v4l2_exportbuffer exp = {};
        exp.type              = kBufType;
        exp.index             = i;
        exp.plane             = 0;
        exp.flags             = O_CLOEXEC;
        V4L2_CHECK(VIDIOC_EXPBUF, &exp);
        g_bufs[i].dmaFd = exp.fd;

        // 4.4 把手 ③：把 dma_buf fd 包装成 MppBuffer（camera_source.c 第 276～282 行）
        // EXT_DMA：外部来的 dma_buf；import 只包装，不分配内存，也不拷贝
        MppBufferInfo info = {};
        info.type          = MPP_BUFFER_TYPE_EXT_DMA;
        info.fd            = exp.fd;
        info.size          = planes[0].length;
        info.index         = i;
        if (mpp_buffer_import(&g_bufs[i].mppBuf, &info) != MPP_OK) {
            printf("[FAIL] mpp_buffer_import buffer %d\n", i);
            return -1;
        }
        printf("buffer %d: length %zu, dma_buf fd %d\n", i, g_bufs[i].length, g_bufs[i].dmaFd);
    }

    // ==================== ⑤ 4 块全部交给驱动，开始采集 ====================
    for (int i = 0; i < kBufCount; i++) {
        if (!queue_buffer(i)) {
            return -1;
        }
    }
    v4l2_buf_type type = kBufType;
    V4L2_CHECK(VIDIOC_STREAMON, &type);
    g_streaming = true;

    // ==================== ⑥ 编码器：stride 用摄像头的（guide/03 第 3 节） ====================
    EncoderConfig cfg;
    cfg.width     = width;
    cfg.height    = height;
    cfg.horStride = bytesPerLine;                                    // 1920
    cfg.verStride = static_cast<int>(sizeImage / bytesPerLine * 2 / 3); // 3110400 / 1920 * 2 / 3 = 1080
    MppEncoder encoder; // run() 返回时自动析构（在 cleanup() 释放摄像头缓冲区之前）
    if (!encoder.init(cfg)) {
        return -1;
    }
    printf("encoder ready: %dx%d stride %dx%d\n", width, height, encoder.hor_stride(), encoder.ver_stride());

    g_out = fopen(kOutputFile, "wb");
    if (!g_out) {
        printf("[FAIL] fopen %s: %s\n", kOutputFile, strerror(errno));
        return -1;
    }
    std::vector<uint8_t> header;
    if (!encoder.get_header(header)) {
        return -1;
    }
    fwrite(header.data(), 1, header.size(), g_out);
    printf("header: %zu bytes\n", header.size());

    // ==================== ⑦ 丢掉开头的帧（自动曝光还没稳定） ====================
    for (int i = 0; i < kSkipFrames; i++) {
        const int index = dequeue_buffer(nullptr);
        if (index < 0 || !queue_buffer(index)) {
            return -1;
        }
    }

    // ==================== ⑧ 编码 kFrames 帧（M3 只编 1 帧；M4 把 kFrames 改成 300 就是循环）====================
    EncodedPacket packet;
    size_t totalBytes = header.size();
    for (int n = 0; n < kFrames; n++) {
        uint32_t bytesUsed = 0;
        const int index    = dequeue_buffer(&bytesUsed);
        if (index < 0) {
            return -1;
        }

        // 零拷贝：编码器直接读摄像头刚写好的这块内存
        if (!encoder.encode(g_bufs[index].mppBuf, false, packet)) {
            queue_buffer(index);
            return -1;
        }

        // ⭐ encode() 返回时硬件已经读完这一帧，现在才能还给摄像头（guide/02 第 3.2 节）
        if (!queue_buffer(index)) {
            return -1;
        }

        if (!packet.data.empty()) {
            fwrite(packet.data.data(), 1, packet.data.size(), g_out);
            totalBytes += packet.data.size();
        }
        if (n % 30 == 0) { // 每 30 帧打印一次（M3 只有第 0 帧）
            printf("frame %3d: buffer %d, in %u bytes, out %zu bytes%s\n", n, index, bytesUsed,
                packet.data.size(), packet.isKeyFrame ? " (I)" : "");
        }
    }

    // ==================== ⑨ 送 EOS，把编码器里剩下的码流取完 ====================
    do {
        if (!encoder.encode(nullptr, true, packet)) {
            return -1;
        }
        if (!packet.data.empty()) {
            fwrite(packet.data.data(), 1, packet.data.size(), g_out);
            totalBytes += packet.data.size();
        }
    } while (!packet.eos);

    printf("done: %d frames, %zu bytes -> %s\n", kFrames, totalBytes, kOutputFile);
    return 0;
}

int main() {
    const int ret = run(); // 任何一步失败都直接 return -1
    cleanup();             // 不管成功失败，都在这里统一释放摄像头资源
    return ret == 0 ? 0 : 1;
}
```

### 运行和验证

```bash
./run_on_board_with_mac.sh
```

应该看到（`buffer` 那几行是 2026-10-16 实测，`encoder ready` 以后**未实测**）：
```
device /dev/video31: driver rkisp_v6, card rkisp_mainpath
format: 1920x1080 NV12, planes 1, bytesperline 1920, sizeimage 3110400
buffer 0: length 3133440, dma_buf fd 12          ← fd 的数字每次可能不一样
buffer 1: length 3133440, dma_buf fd 13
buffer 2: length 3133440, dma_buf fd 14
buffer 3: length 3133440, dma_buf fd 15
encoder ready: 1920x1080 stride 1920x1080        ← ⭐ 一定是 1080，不是 1088
header: 40 bytes                                 ← 和 Day 1 一样
frame   0: buffer 2, in 3110400 bytes, out xxxxx bytes (I)
done: 1 frames, xxxxx bytes -> /userdata/av/cam_m3.h264
```

> "stride 1920x1080 + header 40 bytes"这一步单独测过（2026-10-16，写了个小程序只调 `encoder.init` + `get_header`，rotation 0 / 1 / 3 都成功）。

```bash
./run_move_file_from_board_via_usb.sh /userdata/av/cam_m3.h264
xxd result_from_board/cam_m3.h264 | head -2                       # 开头 00 00 00 01 67（SPS）
ffprobe -v error -show_frames result_from_board/cam_m3.h264 | grep pict_type   # pict_type=I
ffmpeg -y -i result_from_board/cam_m3.h264 -frames:v 1 result_from_board/cam_m3.png && open result_from_board/cam_m3.png
```
图片和 M2 那一帧一样（亮度、颜色、没有绿条），M3 就通过了。**颜色错位、底部有一条绿色**的话，就是 stride 不对，回去看 `encoder ready` 那一行。

### 易错点
- `verStride` 写成 `ALIGN(height, 16)` 或者不填（默认按 16 对齐成 1088）→ 颜色往下错 8 行
- `encode` 之前就 `QBUF` → 摄像头可能正在往这块内存写下一帧，画面撕裂
- `info.type` 写成 `MPP_BUFFER_TYPE_DRM` → 不对，摄像头的内存是外部来的，要用 `MPP_BUFFER_TYPE_EXT_DMA`
- 忘了 `mpp_buffer_put` → MPP 退出时报 buffer 泄漏

---

## 5. M4：编 300 帧（改 3 行）

在 M3 的基础上改这 3 行就行：

```cpp
// 第 1 行的注释（可改可不改）
// day3_cam2h264 M4：摄像头 → V4L2（MMAP + EXPBUF）→ mpp_buffer_import（零拷贝）→ MppEncoder，编 300 帧写成 .h264

static const int kFrames       = 300;                     // 编多少帧：30fps × 10 秒
static const char* kOutputFile = "/userdata/av/cam_m4.h264";
```

### 运行和验证（未实测）

```bash
./run_on_board_with_mac.sh
./run_move_file_from_board_via_usb.sh /userdata/av/cam_m4.h264
ffprobe -v error -count_frames -show_entries stream=nb_read_frames,width,height result_from_board/cam_m4.h264   # nb_read_frames=300
ffplay result_from_board/cam_m4.h264
```

应该看到每 30 帧打印一行，`frame 0` 和 `frame 60`、`frame 120`…… 带 `(I)`（GOP = 60）。播放约 10 秒，画面连续、不撕裂、不卡顿。

### 易错点
- 程序跑完 300 帧应该刚好 10 秒左右（摄像头 30fps）。如果远远超过 10 秒，说明每帧处理太慢、在丢帧，M5 加了统计以后能看出来是 DQBUF 等得久还是编码慢
- 画面方向不对（倒着或者横着）→ M5 用 `-r` 参数转

---

## 6. M5：命令行参数 + 统计 + 旋转（完整代码约 400 行）

### M5 比 M4 多了什么
| 位置 | 新增 | 说明 |
|---|---|---|
| 参数 | `struct Args` + `parse_args()` | `-d -w -h -n -s -o -t -r`，替换掉 M4 写死的常量 |
| ⑥ | `cfg.type = args.type`、`cfg.rotation = args.rotation` | H.265、旋转 |
| ⑧ | `std::chrono::steady_clock` 计时 | 分别统计"等摄像头出帧"和"编码"各花多久 |
| ⑩ | 一行汇总 | 帧数、I 帧数、用时、fps、码率、平均 dqbuf / encode 耗时 |

旋转是在编码器里做的：`EncoderConfig::rotation` → `mpp_enc_cfg_set_s32(cfg, "prep:rotation", N)`（`src/encoder/mpp_encoder.cpp` 已经加好了），N = 0 / 1（逆时针 90°）/ 2（180°）/ 3（逆时针 270°），MPP 开发参考第 658 行。

### 完整代码（整个替换 `src/main.cpp`）

```cpp
// day3_cam2h264 M5：摄像头 → V4L2（MMAP + EXPBUF）→ mpp_buffer_import（零拷贝）→ MppEncoder → .h264
// 用法：day3_cam2h264 [-d /dev/video31] [-w 1920] [-h 1080] [-n 300] [-s 30] [-o /userdata/av/out.h264] [-t h264|h265] [-r 0|1|2|3]
#include <cerrno>   // errno
#include <chrono>   // 计时
#include <cstdint>  // uint8_t / uint32_t
#include <cstdio>   // printf / fopen / fwrite
#include <cstdlib>  // atoi
#include <cstring>  // strerror / strcmp
#include <vector>

#include <fcntl.h>           // open / O_RDWR / O_CLOEXEC
#include <linux/videodev2.h> // V4L2 的所有结构体和 VIDIOC_xxx 命令
#include <sys/ioctl.h>       // ioctl
#include <sys/mman.h>        // mmap / munmap
#include <unistd.h>          // close

#include "encoder/mpp_encoder.h" // Day 1 的编码器（加了 horStride / verStride / rotation）

// ==================== 参数（M5：命令行可以改） ====================
struct Args {
    const char* device = "/dev/video31";          // 摄像头节点（以 guide/01 第 2.3 节查到的为准）
    int width          = 1920;
    int height         = 1080;
    int frames         = 300;                     // 编多少帧：30fps × 10 秒
    int skip           = 30;                      // 开头丢掉多少帧（自动曝光还没稳定）
    const char* output = "/userdata/av/out.h264";
    MppCodingType type = MPP_VIDEO_CodingAVC;     // -t h264 / h265
    int rotation       = 0;                       // -r 0/1/2/3：不转 / 逆时针 90° / 180° / 逆时针 270°
};

static const int kBufCount = 4; // 向驱动要几块缓冲区，和官方 mpi_enc_test 一样用 4 块
// 板子上的 video 节点是 Multiplanar 类型（guide/02 第 1 节实测），所有地方都用这个类型
static const v4l2_buf_type kBufType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

// ==================== 全局状态（面条代码：资源都放全局，最后 cleanup() 统一释放） ====================
// 一块摄像头缓冲区的三个"把手"（guide/02 第 2.2 节）
struct CamBuffer {
    void* start      = MAP_FAILED; // ① CPU 地址（mmap 出来的）
    size_t length    = 0;          //    mmap 的长度，munmap 时要用
    int dmaFd        = -1;         // ② dma_buf fd（VIDIOC_EXPBUF 导出的），别的硬件凭它访问这块内存
    MppBuffer mppBuf = nullptr;    // ③ MppBuffer（mpp_buffer_import 包装的），交给编码器用
};

static int g_videoFd = -1;              // /dev/video31 的文件描述符
static CamBuffer g_bufs[kBufCount];     // 4 块缓冲区
static bool g_streaming = false;        // 有没有 STREAMON，cleanup 时决定要不要 STREAMOFF
static FILE* g_out      = nullptr;      // 输出的 .h264 文件

// ioctl 被信号打断（EINTR）时重试，和 camera_source.c 第 91 行的 camera_source_ioctl 一样的用途
static int xioctl(int fd, unsigned long request, void* arg) {
    int ret;
    do {
        ret = ioctl(fd, request, arg);
    } while (ret == -1 && errno == EINTR);
    return ret;
}

// 调用一个 V4L2 ioctl，失败就打印是哪一句、错误原因，然后 return -1（run() 返回后由 cleanup() 释放资源）
#define V4L2_CHECK(request, arg)                                                                          \
    do {                                                                                                  \
        if (xioctl(g_videoFd, request, arg) == -1) {                                                      \
            printf("[FAIL] %s: %s (line %d)\n", #request, strerror(errno), __LINE__);                     \
            return -1;                                                                                    \
        }                                                                                                 \
    } while (0)

// 释放顺序和 camera_source.c 第 329～364 行的 camera_source_deinit 一样：
// STREAMOFF → mpp_buffer_put → munmap → close(dma_buf fd) → close(video fd)
static void cleanup() {
    if (g_streaming) {
        v4l2_buf_type type = kBufType;
        xioctl(g_videoFd, VIDIOC_STREAMOFF, &type);
        g_streaming = false;
    }
    for (CamBuffer& b : g_bufs) {
        if (b.mppBuf) {
            mpp_buffer_put(b.mppBuf); // 只是解除 MPP 的引用，内存本身是摄像头驱动的
            b.mppBuf = nullptr;
        }
        if (b.start != MAP_FAILED) {
            munmap(b.start, b.length);
            b.start = MAP_FAILED;
        }
        if (b.dmaFd >= 0) {
            close(b.dmaFd);
            b.dmaFd = -1;
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

// VIDIOC_QBUF：把第 index 块缓冲区交给驱动，"往这里写下一帧"
static bool queue_buffer(int index) {
    v4l2_plane planes[1] = {};
    v4l2_buffer buf      = {};
    buf.type             = kBufType;
    buf.memory           = V4L2_MEMORY_MMAP;
    buf.index            = index;
    buf.m.planes         = planes; // ⚠️ MPLANE 类型必须挂上 planes 数组
    buf.length           = 1;      //    并告诉驱动有几个 plane（NV12 只有 1 个），否则返回 EINVAL
    if (xioctl(g_videoFd, VIDIOC_QBUF, &buf) == -1) {
        printf("[FAIL] VIDIOC_QBUF %d: %s\n", index, strerror(errno));
        return false;
    }
    return true;
}

// VIDIOC_DQBUF：取出一块写好画面的缓冲区（没有就阻塞等），返回它的序号；失败返回 -1
// bytesUsed：这一帧实际有多少字节（NV12 1080p 应该是 3,110,400）
static int dequeue_buffer(uint32_t* bytesUsed) {
    v4l2_plane planes[1] = {};
    v4l2_buffer buf      = {};
    buf.type             = kBufType;
    buf.memory           = V4L2_MEMORY_MMAP;
    buf.m.planes         = planes;
    buf.length           = 1;
    if (xioctl(g_videoFd, VIDIOC_DQBUF, &buf) == -1) {
        printf("[FAIL] VIDIOC_DQBUF: %s\n", strerror(errno));
        return -1;
    }
    if (buf.index >= kBufCount) { // camera_source.c 第 388 行写成了 >，差 1
        printf("[FAIL] VIDIOC_DQBUF 返回的序号 %u 越界\n", buf.index);
        return -1;
    }
    if (bytesUsed) {
        *bytesUsed = planes[0].bytesused;
    }
    return static_cast<int>(buf.index);
}

static double ms_between(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

static int run(const Args& args) {
    // ==================== ① 打开摄像头，查能力 ====================
    g_videoFd = open(args.device, O_RDWR | O_CLOEXEC);
    if (g_videoFd < 0) {
        printf("[FAIL] open %s: %s\n", args.device, strerror(errno));
        return -1;
    }
    v4l2_capability cap = {};
    V4L2_CHECK(VIDIOC_QUERYCAP, &cap);
    printf("device %s: driver %s, card %s\n", args.device, cap.driver, cap.card);
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) || !(cap.capabilities & V4L2_CAP_STREAMING)) {
        printf("[FAIL] %s 不支持 Multiplanar 采集或者 streaming\n", args.device);
        return -1;
    }

    // ==================== ② 设格式，再读回实际值 ====================
    v4l2_format fmt           = {};
    fmt.type                  = kBufType;
    fmt.fmt.pix_mp.width       = args.width;
    fmt.fmt.pix_mp.height      = args.height;
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    fmt.fmt.pix_mp.field       = V4L2_FIELD_NONE;
    fmt.fmt.pix_mp.num_planes  = 1;
    V4L2_CHECK(VIDIOC_S_FMT, &fmt);
    V4L2_CHECK(VIDIOC_G_FMT, &fmt); // 驱动可能调整了宽高、每行字节数，永远以读回来的为准
    const int width             = static_cast<int>(fmt.fmt.pix_mp.width);
    const int height            = static_cast<int>(fmt.fmt.pix_mp.height);
    const int bytesPerLine      = static_cast<int>(fmt.fmt.pix_mp.plane_fmt[0].bytesperline);
    const uint32_t sizeImage    = fmt.fmt.pix_mp.plane_fmt[0].sizeimage;
    printf("format: %dx%d NV12, planes %u, bytesperline %d, sizeimage %u\n", width, height,
        fmt.fmt.pix_mp.num_planes, bytesPerLine, sizeImage);
    if (fmt.fmt.pix_mp.pixelformat != V4L2_PIX_FMT_NV12 || fmt.fmt.pix_mp.num_planes != 1 || bytesPerLine <= 0) {
        printf("[FAIL] 驱动没有给 1 个 plane 的 NV12\n");
        return -1;
    }

    // ==================== ③ 要 4 块缓冲区 ====================
    v4l2_requestbuffers req = {};
    req.count               = kBufCount;
    req.type                = kBufType;
    req.memory              = V4L2_MEMORY_MMAP; // 缓冲区由驱动分配，我们 mmap 过来用
    V4L2_CHECK(VIDIOC_REQBUFS, &req);
    if (req.count != kBufCount) {
        printf("[FAIL] 要 %d 块缓冲区，驱动给了 %u 块\n", kBufCount, req.count);
        return -1;
    }

    // ==================== ④ 每块缓冲区：QUERYBUF + mmap + EXPBUF + import ====================
    for (int i = 0; i < kBufCount; i++) {
        // 4.1 查第 i 块的长度和 mmap 偏移
        v4l2_plane planes[1] = {};
        v4l2_buffer buf      = {};
        buf.type             = kBufType;
        buf.memory           = V4L2_MEMORY_MMAP;
        buf.index            = i;
        buf.m.planes         = planes;
        buf.length           = 1;
        V4L2_CHECK(VIDIOC_QUERYBUF, &buf);

        // 4.2 把手 ①：mmap 成 CPU 地址（M5 用不到，M2 存文件、M6 拷贝时用）
        g_bufs[i].length = planes[0].length;
        g_bufs[i].start  = mmap(nullptr, planes[0].length, PROT_READ | PROT_WRITE, MAP_SHARED, g_videoFd,
             planes[0].m.mem_offset);
        if (g_bufs[i].start == MAP_FAILED) {
            printf("[FAIL] mmap buffer %d: %s\n", i, strerror(errno));
            return -1;
        }

        // 4.3 把手 ②：导出成 dma_buf fd（camera_source.c 第 266～271 行）
        v4l2_exportbuffer exp = {};
        exp.type              = kBufType;
        exp.index             = i;
        exp.plane             = 0;
        exp.flags             = O_CLOEXEC;
        V4L2_CHECK(VIDIOC_EXPBUF, &exp);
        g_bufs[i].dmaFd = exp.fd;

        // 4.4 把手 ③：把 dma_buf fd 包装成 MppBuffer（camera_source.c 第 276～282 行）
        // EXT_DMA：外部来的 dma_buf；import 只包装，不分配内存，也不拷贝
        MppBufferInfo info = {};
        info.type          = MPP_BUFFER_TYPE_EXT_DMA;
        info.fd            = exp.fd;
        info.size          = planes[0].length;
        info.index         = i;
        if (mpp_buffer_import(&g_bufs[i].mppBuf, &info) != MPP_OK) {
            printf("[FAIL] mpp_buffer_import buffer %d\n", i);
            return -1;
        }
        printf("buffer %d: length %zu, dma_buf fd %d\n", i, g_bufs[i].length, g_bufs[i].dmaFd);
    }

    // ==================== ⑤ 4 块全部交给驱动，开始采集 ====================
    for (int i = 0; i < kBufCount; i++) {
        if (!queue_buffer(i)) {
            return -1;
        }
    }
    v4l2_buf_type type = kBufType;
    V4L2_CHECK(VIDIOC_STREAMON, &type);
    g_streaming = true;

    // ==================== ⑥ 编码器：stride 用摄像头的（guide/03 第 3 节） ====================
    EncoderConfig cfg;
    cfg.width     = width;
    cfg.height    = height;
    cfg.type      = args.type;
    cfg.horStride = bytesPerLine;                                    // 1920
    cfg.verStride = static_cast<int>(sizeImage / bytesPerLine * 2 / 3); // 3110400 / 1920 * 2 / 3 = 1080
    cfg.rotation  = args.rotation;
    MppEncoder encoder; // run() 返回时自动析构（在 cleanup() 释放摄像头缓冲区之前）
    if (!encoder.init(cfg)) {
        return -1;
    }
    printf("encoder ready: %dx%d stride %dx%d rotation %d\n", width, height, encoder.hor_stride(),
        encoder.ver_stride(), args.rotation);

    g_out = fopen(args.output, "wb");
    if (!g_out) {
        printf("[FAIL] fopen %s: %s\n", args.output, strerror(errno));
        return -1;
    }
    std::vector<uint8_t> header;
    if (!encoder.get_header(header)) {
        return -1;
    }
    fwrite(header.data(), 1, header.size(), g_out);
    printf("header: %zu bytes\n", header.size());

    // ==================== ⑦ 丢掉开头的帧（自动曝光还没稳定） ====================
    for (int i = 0; i < args.skip; i++) {
        const int index = dequeue_buffer(nullptr);
        if (index < 0 || !queue_buffer(index)) {
            return -1;
        }
    }

    // ==================== ⑧ 主循环：DQBUF → 编码 → 写文件 → QBUF ====================
    EncodedPacket packet;
    size_t totalBytes = header.size();
    int keyFrames     = 0;
    double dqbufMs    = 0; // 等摄像头出帧一共花了多久
    double encodeMs   = 0; // 编码一共花了多久
    const auto loopBegin = std::chrono::steady_clock::now();
    for (int n = 0; n < args.frames; n++) {
        const auto t0       = std::chrono::steady_clock::now();
        uint32_t bytesUsed  = 0;
        const int index     = dequeue_buffer(&bytesUsed);
        if (index < 0) {
            return -1;
        }
        const auto t1 = std::chrono::steady_clock::now();

        // 零拷贝：编码器直接读摄像头刚写好的这块内存
        if (!encoder.encode(g_bufs[index].mppBuf, false, packet)) {
            queue_buffer(index);
            return -1;
        }
        const auto t2 = std::chrono::steady_clock::now();

        // ⭐ encode() 返回时硬件已经读完这一帧，现在才能还给摄像头（guide/02 第 3.2 节）
        if (!queue_buffer(index)) {
            return -1;
        }

        if (!packet.data.empty()) {
            fwrite(packet.data.data(), 1, packet.data.size(), g_out);
            totalBytes += packet.data.size();
        }
        if (packet.isKeyFrame) {
            keyFrames++;
        }
        dqbufMs += ms_between(t0, t1);
        encodeMs += ms_between(t1, t2);
        if (n % 30 == 0) { // 每 30 帧（约 1 秒）打印一次，每帧都打印会拖慢循环
            printf("frame %3d: buffer %d, in %u bytes, out %zu bytes%s, dqbuf %.1f ms, encode %.1f ms\n", n,
                index, bytesUsed, packet.data.size(), packet.isKeyFrame ? " (I)" : "", ms_between(t0, t1),
                ms_between(t1, t2));
        }
    }
    const auto loopEnd = std::chrono::steady_clock::now();

    // ==================== ⑨ 送 EOS，把编码器里剩下的码流取完 ====================
    do {
        if (!encoder.encode(nullptr, true, packet)) {
            return -1;
        }
        if (!packet.data.empty()) {
            fwrite(packet.data.data(), 1, packet.data.size(), g_out);
            totalBytes += packet.data.size();
        }
    } while (!packet.eos);

    // ==================== ⑩ 统计 ====================
    const double seconds = ms_between(loopBegin, loopEnd) / 1000.0;
    printf("done: %d frames (%d I), %.2f s, %.1f fps, %.2f Mbps, avg dqbuf %.1f ms, avg encode %.1f ms -> %s\n",
        args.frames, keyFrames, seconds, args.frames / seconds, totalBytes * 8 / seconds / 1e6,
        dqbufMs / args.frames, encodeMs / args.frames, args.output);
    return 0;
}

static void usage(const char* prog) {
    printf("usage: %s [-d /dev/video31] [-w 1920] [-h 1080] [-n 300] [-s 30] [-o /userdata/av/out.h264] "
           "[-t h264|h265] [-r 0|1|2|3]\n",
        prog);
}

// 每个参数都是"-x 值"成对出现
static bool parse_args(int argc, char** argv, Args& args) {
    for (int i = 1; i < argc; i += 2) {
        const char* key   = argv[i];
        const char* value = (i + 1 < argc) ? argv[i + 1] : nullptr;
        if (!value) {
            printf("[FAIL] %s 后面缺少值\n", key);
            return false;
        }
        if (!strcmp(key, "-d")) {
            args.device = value;
        } else if (!strcmp(key, "-w")) {
            args.width = atoi(value);
        } else if (!strcmp(key, "-h")) {
            args.height = atoi(value);
        } else if (!strcmp(key, "-n")) {
            args.frames = atoi(value);
        } else if (!strcmp(key, "-s")) {
            args.skip = atoi(value);
        } else if (!strcmp(key, "-o")) {
            args.output = value;
        } else if (!strcmp(key, "-r")) {
            args.rotation = atoi(value);
        } else if (!strcmp(key, "-t")) {
            if (!strcmp(value, "h264")) {
                args.type = MPP_VIDEO_CodingAVC;
            } else if (!strcmp(value, "h265")) {
                args.type = MPP_VIDEO_CodingHEVC;
            } else {
                printf("[FAIL] -t 只支持 h264 / h265\n");
                return false;
            }
        } else {
            printf("[FAIL] 不认识的参数 %s\n", key);
            return false;
        }
    }
    if (args.width <= 0 || args.height <= 0 || args.frames <= 0 || args.skip < 0 || args.rotation < 0 ||
        args.rotation > 3) {
        printf("[FAIL] 参数取值不对\n");
        return false;
    }
    return true;
}

int main(int argc, char** argv) {
    Args args;
    if (!parse_args(argc, argv, args)) {
        usage(argv[0]);
        return 1;
    }
    const int ret = run(args); // 任何一步失败都直接 return -1
    cleanup();                 // 不管成功失败，都在这里统一释放摄像头资源
    return ret == 0 ? 0 : 1;
}
```

### 运行（未实测）

```bash
./run_on_board_with_mac.sh                                               # 全用默认值：/dev/video31、1080p、300 帧、H.264
./run_on_board_with_mac.sh -n 300 -t h265 -o /userdata/av/out.h265       # H.265
./run_on_board_with_mac.sh -r 2 -o /userdata/av/out_r2.h264              # 转 180°
./run_on_board_with_mac.sh -w 1280 -h 720 -o /userdata/av/out_720p.h264  # 720p
./run_on_board_with_mac.sh -x 1                                          # 参数错误：打印用法，退出码 1（实测）
```

最后一行应该类似：
```
done: 300 frames (5 I), 10.00 s, 30.0 fps, 4.xx Mbps, avg dqbuf 3x.x ms, avg encode x.x ms -> /userdata/av/out.h264
```
怎么看这一行：
- **fps ≈ 30**：瓶颈在摄像头（每 33ms 出一帧），这是正常的。和 Day 1 读文件时几百 fps 不一样
- **avg dqbuf 大、avg encode 小**：大部分时间都在等摄像头出帧，编码器很闲 —— 说明零拷贝 + 硬件编码一点都不吃力
- **I 帧数 = 300 ÷ 60 = 5**：GOP 是 60

### 旋转的实验（选做）
`-r 1` / `-r 3` 转 90° 以后画面变成竖的。MPP 会不会自动把输出宽高换成 1080×1920、播放器能不能正确显示，**做实验确认**，结果记进笔记。

---

## 7. M6：拷贝版本，对比 CPU 占用（在 M5 上加约 40 行）

M6 在 M5 的基础上加一个参数 `-c 1`：不用零拷贝，而是每帧用 CPU 把摄像头缓冲区 `memcpy` 到自己申请的 MppBuffer（和 Day 1 一样从 DRM + CACHABLE 内存池申请），再交给编码器。

### M6 比 M5 多了什么（代码里用 `★ M6` 标出来了）
| 位置 | 新增 |
|---|---|
| `Args` | `bool copy` |
| 全局 | `g_copyGroup`、`g_copyBuf` |
| `cleanup()` | 先 `mpp_buffer_put(g_copyBuf)`，再 `mpp_buffer_group_put(g_copyGroup)` |
| ⑥ 后面 | `-c 1` 时申请内存池和一块 MppBuffer |
| ⑧ | `-c 1` 时：`sync_begin` → `memcpy` → `sync_end` → **马上 QBUF**（数据已经拷走了）→ `encode(g_copyBuf)` |
| `parse_args` | `-c` |

注意拷贝模式下 QBUF 的位置：数据已经拷到自己的 buffer 里了，**拷完就能还给摄像头**，不用等编码完。这是拷贝方式唯一的好处：摄像头缓冲区占用的时间短。

### 完整代码（整个替换 `src/main.cpp`）

```cpp
// day3_cam2h264 M6：在 M5 的基础上加 -c 1（拷贝模式），和零拷贝对比 CPU 占用
// 用法：day3_cam2h264 [-d /dev/video31] [-w 1920] [-h 1080] [-n 300] [-s 30] [-o /userdata/av/out.h264] [-t h264|h265] [-r 0|1|2|3] [-c 0|1]
#include <cerrno>   // errno
#include <chrono>   // 计时
#include <cstdint>  // uint8_t / uint32_t
#include <cstdio>   // printf / fopen / fwrite
#include <cstdlib>  // atoi
#include <cstring>  // strerror / strcmp
#include <vector>

#include <fcntl.h>           // open / O_RDWR / O_CLOEXEC
#include <linux/videodev2.h> // V4L2 的所有结构体和 VIDIOC_xxx 命令
#include <sys/ioctl.h>       // ioctl
#include <sys/mman.h>        // mmap / munmap
#include <unistd.h>          // close

#include "encoder/mpp_encoder.h" // Day 1 的编码器（加了 horStride / verStride / rotation）

// ==================== 参数（M5：命令行可以改） ====================
struct Args {
    const char* device = "/dev/video31";          // 摄像头节点（以 guide/01 第 2.3 节查到的为准）
    int width          = 1920;
    int height         = 1080;
    int frames         = 300;                     // 编多少帧：30fps × 10 秒
    int skip           = 30;                      // 开头丢掉多少帧（自动曝光还没稳定）
    const char* output = "/userdata/av/out.h264";
    MppCodingType type = MPP_VIDEO_CodingAVC;     // -t h264 / h265
    int rotation       = 0;                       // -r 0/1/2/3：不转 / 逆时针 90° / 180° / 逆时针 270°
    bool copy          = false;                   // ★ M6：-c 1 = 拷贝模式（memcpy 到自己申请的 MppBuffer 再编码）
};

static const int kBufCount = 4; // 向驱动要几块缓冲区，和官方 mpi_enc_test 一样用 4 块
// 板子上的 video 节点是 Multiplanar 类型（guide/02 第 1 节实测），所有地方都用这个类型
static const v4l2_buf_type kBufType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

// ==================== 全局状态（面条代码：资源都放全局，最后 cleanup() 统一释放） ====================
// 一块摄像头缓冲区的三个"把手"（guide/02 第 2.2 节）
struct CamBuffer {
    void* start      = MAP_FAILED; // ① CPU 地址（mmap 出来的）
    size_t length    = 0;          //    mmap 的长度，munmap 时要用
    int dmaFd        = -1;         // ② dma_buf fd（VIDIOC_EXPBUF 导出的），别的硬件凭它访问这块内存
    MppBuffer mppBuf = nullptr;    // ③ MppBuffer（mpp_buffer_import 包装的），交给编码器用
};

static int g_videoFd = -1;              // /dev/video31 的文件描述符
static CamBuffer g_bufs[kBufCount];     // 4 块缓冲区
static bool g_streaming = false;        // 有没有 STREAMON，cleanup 时决定要不要 STREAMOFF
static FILE* g_out      = nullptr;      // 输出的 .h264 文件
// ★ M6 拷贝模式用：和 Day 1 一样，从 DRM + CACHABLE 内存池申请一块 MppBuffer
static MppBufferGroup g_copyGroup = nullptr;
static MppBuffer g_copyBuf        = nullptr;

// ioctl 被信号打断（EINTR）时重试，和 camera_source.c 第 91 行的 camera_source_ioctl 一样的用途
static int xioctl(int fd, unsigned long request, void* arg) {
    int ret;
    do {
        ret = ioctl(fd, request, arg);
    } while (ret == -1 && errno == EINTR);
    return ret;
}

// 调用一个 V4L2 ioctl，失败就打印是哪一句、错误原因，然后 return -1（run() 返回后由 cleanup() 释放资源）
#define V4L2_CHECK(request, arg)                                                                          \
    do {                                                                                                  \
        if (xioctl(g_videoFd, request, arg) == -1) {                                                      \
            printf("[FAIL] %s: %s (line %d)\n", #request, strerror(errno), __LINE__);                     \
            return -1;                                                                                    \
        }                                                                                                 \
    } while (0)

// 释放顺序和 camera_source.c 第 329～364 行的 camera_source_deinit 一样：
// STREAMOFF → mpp_buffer_put → munmap → close(dma_buf fd) → close(video fd)
static void cleanup() {
    if (g_streaming) {
        v4l2_buf_type type = kBufType;
        xioctl(g_videoFd, VIDIOC_STREAMOFF, &type);
        g_streaming = false;
    }
    for (CamBuffer& b : g_bufs) {
        if (b.mppBuf) {
            mpp_buffer_put(b.mppBuf); // 只是解除 MPP 的引用，内存本身是摄像头驱动的
            b.mppBuf = nullptr;
        }
        if (b.start != MAP_FAILED) {
            munmap(b.start, b.length);
            b.start = MAP_FAILED;
        }
        if (b.dmaFd >= 0) {
            close(b.dmaFd);
            b.dmaFd = -1;
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
    // ★ M6：先还 buffer，再销毁内存池
    if (g_copyBuf) {
        mpp_buffer_put(g_copyBuf);
        g_copyBuf = nullptr;
    }
    if (g_copyGroup) {
        mpp_buffer_group_put(g_copyGroup);
        g_copyGroup = nullptr;
    }
}

// VIDIOC_QBUF：把第 index 块缓冲区交给驱动，"往这里写下一帧"
static bool queue_buffer(int index) {
    v4l2_plane planes[1] = {};
    v4l2_buffer buf      = {};
    buf.type             = kBufType;
    buf.memory           = V4L2_MEMORY_MMAP;
    buf.index            = index;
    buf.m.planes         = planes; // ⚠️ MPLANE 类型必须挂上 planes 数组
    buf.length           = 1;      //    并告诉驱动有几个 plane（NV12 只有 1 个），否则返回 EINVAL
    if (xioctl(g_videoFd, VIDIOC_QBUF, &buf) == -1) {
        printf("[FAIL] VIDIOC_QBUF %d: %s\n", index, strerror(errno));
        return false;
    }
    return true;
}

// VIDIOC_DQBUF：取出一块写好画面的缓冲区（没有就阻塞等），返回它的序号；失败返回 -1
// bytesUsed：这一帧实际有多少字节（NV12 1080p 应该是 3,110,400）
static int dequeue_buffer(uint32_t* bytesUsed) {
    v4l2_plane planes[1] = {};
    v4l2_buffer buf      = {};
    buf.type             = kBufType;
    buf.memory           = V4L2_MEMORY_MMAP;
    buf.m.planes         = planes;
    buf.length           = 1;
    if (xioctl(g_videoFd, VIDIOC_DQBUF, &buf) == -1) {
        printf("[FAIL] VIDIOC_DQBUF: %s\n", strerror(errno));
        return -1;
    }
    if (buf.index >= kBufCount) { // camera_source.c 第 388 行写成了 >，差 1
        printf("[FAIL] VIDIOC_DQBUF 返回的序号 %u 越界\n", buf.index);
        return -1;
    }
    if (bytesUsed) {
        *bytesUsed = planes[0].bytesused;
    }
    return static_cast<int>(buf.index);
}

static double ms_between(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

static int run(const Args& args) {
    // ==================== ① 打开摄像头，查能力 ====================
    g_videoFd = open(args.device, O_RDWR | O_CLOEXEC);
    if (g_videoFd < 0) {
        printf("[FAIL] open %s: %s\n", args.device, strerror(errno));
        return -1;
    }
    v4l2_capability cap = {};
    V4L2_CHECK(VIDIOC_QUERYCAP, &cap);
    printf("device %s: driver %s, card %s\n", args.device, cap.driver, cap.card);
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) || !(cap.capabilities & V4L2_CAP_STREAMING)) {
        printf("[FAIL] %s 不支持 Multiplanar 采集或者 streaming\n", args.device);
        return -1;
    }

    // ==================== ② 设格式，再读回实际值 ====================
    v4l2_format fmt           = {};
    fmt.type                  = kBufType;
    fmt.fmt.pix_mp.width       = args.width;
    fmt.fmt.pix_mp.height      = args.height;
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    fmt.fmt.pix_mp.field       = V4L2_FIELD_NONE;
    fmt.fmt.pix_mp.num_planes  = 1;
    V4L2_CHECK(VIDIOC_S_FMT, &fmt);
    V4L2_CHECK(VIDIOC_G_FMT, &fmt); // 驱动可能调整了宽高、每行字节数，永远以读回来的为准
    const int width             = static_cast<int>(fmt.fmt.pix_mp.width);
    const int height            = static_cast<int>(fmt.fmt.pix_mp.height);
    const int bytesPerLine      = static_cast<int>(fmt.fmt.pix_mp.plane_fmt[0].bytesperline);
    const uint32_t sizeImage    = fmt.fmt.pix_mp.plane_fmt[0].sizeimage;
    printf("format: %dx%d NV12, planes %u, bytesperline %d, sizeimage %u\n", width, height,
        fmt.fmt.pix_mp.num_planes, bytesPerLine, sizeImage);
    if (fmt.fmt.pix_mp.pixelformat != V4L2_PIX_FMT_NV12 || fmt.fmt.pix_mp.num_planes != 1 || bytesPerLine <= 0) {
        printf("[FAIL] 驱动没有给 1 个 plane 的 NV12\n");
        return -1;
    }

    // ==================== ③ 要 4 块缓冲区 ====================
    v4l2_requestbuffers req = {};
    req.count               = kBufCount;
    req.type                = kBufType;
    req.memory              = V4L2_MEMORY_MMAP; // 缓冲区由驱动分配，我们 mmap 过来用
    V4L2_CHECK(VIDIOC_REQBUFS, &req);
    if (req.count != kBufCount) {
        printf("[FAIL] 要 %d 块缓冲区，驱动给了 %u 块\n", kBufCount, req.count);
        return -1;
    }

    // ==================== ④ 每块缓冲区：QUERYBUF + mmap + EXPBUF + import ====================
    for (int i = 0; i < kBufCount; i++) {
        // 4.1 查第 i 块的长度和 mmap 偏移
        v4l2_plane planes[1] = {};
        v4l2_buffer buf      = {};
        buf.type             = kBufType;
        buf.memory           = V4L2_MEMORY_MMAP;
        buf.index            = i;
        buf.m.planes         = planes;
        buf.length           = 1;
        V4L2_CHECK(VIDIOC_QUERYBUF, &buf);

        // 4.2 把手 ①：mmap 成 CPU 地址（M5 用不到，M2 存文件、M6 拷贝时用）
        g_bufs[i].length = planes[0].length;
        g_bufs[i].start  = mmap(nullptr, planes[0].length, PROT_READ | PROT_WRITE, MAP_SHARED, g_videoFd,
             planes[0].m.mem_offset);
        if (g_bufs[i].start == MAP_FAILED) {
            printf("[FAIL] mmap buffer %d: %s\n", i, strerror(errno));
            return -1;
        }

        // 4.3 把手 ②：导出成 dma_buf fd（camera_source.c 第 266～271 行）
        v4l2_exportbuffer exp = {};
        exp.type              = kBufType;
        exp.index             = i;
        exp.plane             = 0;
        exp.flags             = O_CLOEXEC;
        V4L2_CHECK(VIDIOC_EXPBUF, &exp);
        g_bufs[i].dmaFd = exp.fd;

        // 4.4 把手 ③：把 dma_buf fd 包装成 MppBuffer（camera_source.c 第 276～282 行）
        // EXT_DMA：外部来的 dma_buf；import 只包装，不分配内存，也不拷贝
        MppBufferInfo info = {};
        info.type          = MPP_BUFFER_TYPE_EXT_DMA;
        info.fd            = exp.fd;
        info.size          = planes[0].length;
        info.index         = i;
        if (mpp_buffer_import(&g_bufs[i].mppBuf, &info) != MPP_OK) {
            printf("[FAIL] mpp_buffer_import buffer %d\n", i);
            return -1;
        }
        printf("buffer %d: length %zu, dma_buf fd %d\n", i, g_bufs[i].length, g_bufs[i].dmaFd);
    }

    // ==================== ⑤ 4 块全部交给驱动，开始采集 ====================
    for (int i = 0; i < kBufCount; i++) {
        if (!queue_buffer(i)) {
            return -1;
        }
    }
    v4l2_buf_type type = kBufType;
    V4L2_CHECK(VIDIOC_STREAMON, &type);
    g_streaming = true;

    // ==================== ⑥ 编码器：stride 用摄像头的（guide/03 第 3 节） ====================
    EncoderConfig cfg;
    cfg.width     = width;
    cfg.height    = height;
    cfg.type      = args.type;
    cfg.horStride = bytesPerLine;                                    // 1920
    cfg.verStride = static_cast<int>(sizeImage / bytesPerLine * 2 / 3); // 3110400 / 1920 * 2 / 3 = 1080
    cfg.rotation  = args.rotation;
    MppEncoder encoder; // run() 返回时自动析构（在 cleanup() 释放摄像头缓冲区之前）
    if (!encoder.init(cfg)) {
        return -1;
    }
    printf("encoder ready: %dx%d stride %dx%d rotation %d\n", width, height, encoder.hor_stride(),
        encoder.ver_stride(), args.rotation);

    g_out = fopen(args.output, "wb");
    if (!g_out) {
        printf("[FAIL] fopen %s: %s\n", args.output, strerror(errno));
        return -1;
    }
    // ★ M6：拷贝模式要自己申请一块编码器能访问的内存（和 Day 1 一样：DRM + CACHABLE）
    if (args.copy) {
        if (mpp_buffer_group_get_internal(&g_copyGroup, MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE) != MPP_OK ||
            mpp_buffer_get(g_copyGroup, &g_copyBuf, g_bufs[0].length) != MPP_OK) {
            printf("[FAIL] 申请拷贝用的 MppBuffer\n");
            return -1;
        }
        printf("copy mode: memcpy %u bytes per frame\n", sizeImage);
    }

    std::vector<uint8_t> header;
    if (!encoder.get_header(header)) {
        return -1;
    }
    fwrite(header.data(), 1, header.size(), g_out);
    printf("header: %zu bytes\n", header.size());

    // ==================== ⑦ 丢掉开头的帧（自动曝光还没稳定） ====================
    for (int i = 0; i < args.skip; i++) {
        const int index = dequeue_buffer(nullptr);
        if (index < 0 || !queue_buffer(index)) {
            return -1;
        }
    }

    // ==================== ⑧ 主循环：DQBUF → 编码 → 写文件 → QBUF ====================
    EncodedPacket packet;
    size_t totalBytes = header.size();
    int keyFrames     = 0;
    double dqbufMs    = 0; // 等摄像头出帧一共花了多久
    double encodeMs   = 0; // 编码一共花了多久
    const auto loopBegin = std::chrono::steady_clock::now();
    for (int n = 0; n < args.frames; n++) {
        const auto t0       = std::chrono::steady_clock::now();
        uint32_t bytesUsed  = 0;
        const int index     = dequeue_buffer(&bytesUsed);
        if (index < 0) {
            return -1;
        }
        const auto t1 = std::chrono::steady_clock::now();

        MppBuffer input = g_bufs[index].mppBuf; // 零拷贝：编码器直接读摄像头刚写好的这块内存
        if (args.copy) {
            // ★ M6 拷贝模式：CPU 把整帧拷到自己的 MppBuffer，拷完马上就能还给摄像头
            mpp_buffer_sync_begin(g_copyBuf);
            memcpy(mpp_buffer_get_ptr(g_copyBuf), g_bufs[index].start, bytesUsed);
            mpp_buffer_sync_end(g_copyBuf); // CACHABLE 内存：CPU 写完一定要刷 cache，编码器才看得到
            if (!queue_buffer(index)) {
                return -1;
            }
            input = g_copyBuf;
        }
        if (!encoder.encode(input, false, packet)) {
            if (!args.copy) {
                queue_buffer(index);
            }
            return -1;
        }
        const auto t2 = std::chrono::steady_clock::now();

        // ⭐ 零拷贝模式：encode() 返回时硬件已经读完这一帧，现在才能还给摄像头（guide/02 第 3.2 节）
        if (!args.copy && !queue_buffer(index)) {
            return -1;
        }

        if (!packet.data.empty()) {
            fwrite(packet.data.data(), 1, packet.data.size(), g_out);
            totalBytes += packet.data.size();
        }
        if (packet.isKeyFrame) {
            keyFrames++;
        }
        dqbufMs += ms_between(t0, t1);
        encodeMs += ms_between(t1, t2);
        if (n % 30 == 0) { // 每 30 帧（约 1 秒）打印一次，每帧都打印会拖慢循环
            printf("frame %3d: buffer %d, in %u bytes, out %zu bytes%s, dqbuf %.1f ms, encode %.1f ms\n", n,
                index, bytesUsed, packet.data.size(), packet.isKeyFrame ? " (I)" : "", ms_between(t0, t1),
                ms_between(t1, t2));
        }
    }
    const auto loopEnd = std::chrono::steady_clock::now();

    // ==================== ⑨ 送 EOS，把编码器里剩下的码流取完 ====================
    do {
        if (!encoder.encode(nullptr, true, packet)) {
            return -1;
        }
        if (!packet.data.empty()) {
            fwrite(packet.data.data(), 1, packet.data.size(), g_out);
            totalBytes += packet.data.size();
        }
    } while (!packet.eos);

    // ==================== ⑩ 统计 ====================
    const double seconds = ms_between(loopBegin, loopEnd) / 1000.0;
    printf("done%s: %d frames (%d I), %.2f s, %.1f fps, %.2f Mbps, avg dqbuf %.1f ms, avg encode %.1f ms -> %s\n",
        args.copy ? " (copy)" : " (zero-copy)", args.frames, keyFrames, seconds, args.frames / seconds, totalBytes * 8 / seconds / 1e6,
        dqbufMs / args.frames, encodeMs / args.frames, args.output);
    return 0;
}

static void usage(const char* prog) {
    printf("usage: %s [-d /dev/video31] [-w 1920] [-h 1080] [-n 300] [-s 30] [-o /userdata/av/out.h264] "
           "[-t h264|h265] [-r 0|1|2|3] [-c 0|1]\n",
        prog);
}

// 每个参数都是"-x 值"成对出现
static bool parse_args(int argc, char** argv, Args& args) {
    for (int i = 1; i < argc; i += 2) {
        const char* key   = argv[i];
        const char* value = (i + 1 < argc) ? argv[i + 1] : nullptr;
        if (!value) {
            printf("[FAIL] %s 后面缺少值\n", key);
            return false;
        }
        if (!strcmp(key, "-d")) {
            args.device = value;
        } else if (!strcmp(key, "-w")) {
            args.width = atoi(value);
        } else if (!strcmp(key, "-h")) {
            args.height = atoi(value);
        } else if (!strcmp(key, "-n")) {
            args.frames = atoi(value);
        } else if (!strcmp(key, "-s")) {
            args.skip = atoi(value);
        } else if (!strcmp(key, "-o")) {
            args.output = value;
        } else if (!strcmp(key, "-r")) {
            args.rotation = atoi(value);
        } else if (!strcmp(key, "-c")) { // ★ M6
            args.copy = atoi(value) != 0;
        } else if (!strcmp(key, "-t")) {
            if (!strcmp(value, "h264")) {
                args.type = MPP_VIDEO_CodingAVC;
            } else if (!strcmp(value, "h265")) {
                args.type = MPP_VIDEO_CodingHEVC;
            } else {
                printf("[FAIL] -t 只支持 h264 / h265\n");
                return false;
            }
        } else {
            printf("[FAIL] 不认识的参数 %s\n", key);
            return false;
        }
    }
    if (args.width <= 0 || args.height <= 0 || args.frames <= 0 || args.skip < 0 || args.rotation < 0 ||
        args.rotation > 3) {
        printf("[FAIL] 参数取值不对\n");
        return false;
    }
    return true;
}

int main(int argc, char** argv) {
    Args args;
    if (!parse_args(argc, argv, args)) {
        usage(argv[0]);
        return 1;
    }
    const int ret = run(args); // 任何一步失败都直接 return -1
    cleanup();                 // 不管成功失败，都在这里统一释放摄像头资源
    return ret == 0 ? 0 : 1;
}
```

### 对比实验（未实测）

开两个终端：

```bash
# 终端 1：看 CPU（每秒刷新一次，找 day3_cam2h264 那一行的 %CPU）
adb shell top -d 1

# 终端 2：先跑零拷贝，再跑拷贝
./run_on_board_with_mac.sh -n 600 -o /userdata/av/zero.h264          # 20 秒，方便看 top
./run_on_board_with_mac.sh -n 600 -o /userdata/av/copy.h264 -c 1
```

| | 零拷贝 | 拷贝 |
|---|---|---|
| `top` 里的 %CPU | | |
| `done:` 那行的 fps | | |
| avg encode（拷贝模式的计时包含 memcpy） | | |

1080p30 拷贝模式每秒要 `memcpy` 3MB × 30 ≈ 90MB。摄像头的 mmap 内存可能是不走 cache 的，CPU 从这种内存读会更慢，CPU 占用的差别可能比想象的大（按驱动常见做法推断，未实测）。

---

## 8. M6 之后：整理成类

和 Day 1、Day 2 一样，面条代码跑通以后再拆：

| 类 / 文件 | 从 main.cpp 里拿走什么 |
|---|---|
| `src/source/v4l2_camera.*`：`V4l2Camera` | ①～⑤ 和 `cleanup()` 的摄像头部分，`queue_buffer` / `dequeue_buffer`。RAII：析构时 STREAMOFF、put、munmap、close |
| `src/encoder/mpp_encoder.*` | 已经有了 |
| `src/app/args.*` | `Args` 和 `parse_args` |
| `src/pipeline/` | ⑦～⑩ 的循环和统计 |

接口可以照总计划里的设计：`open(dev, w, h, fourcc, bufCount)` / `dequeue()` / `mpp_buffer(idx)` / `enqueue(idx)`。到时候再细化。
