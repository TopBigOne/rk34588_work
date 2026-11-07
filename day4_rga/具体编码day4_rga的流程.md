# 具体编码 day4_rga 的流程

> 2026-11-03 写。参考代码已经交叉编译通过（`-Wall -Wextra` 0 warning，最高只用到 GLIBC_2.34）。
> 2026-11-08 更新：M1、M2、M3 都已经上板跑通，代码重构过（目录见第 3.1 节）。逐段讲解和更多实测见 [doc/1_理解M1的需求和代码.md](doc/1_理解M1的需求和代码.md)、[doc/2_理解M2的需求和代码.md](doc/2_理解M2的需求和代码.md)、[doc/3_理解M3的需求和代码.md](doc/3_理解M3的需求和代码.md)。NV12 的 Y、UV 在内存里怎么排、stride 填充在哪，见 [doc/0_base_NV12内存排布.md](doc/0_base_NV12内存排布.md)。

## 0. 今天学完，能回答什么

**一句话**：MPP 编码器只会"给什么尺寸就编什么尺寸"。一路摄像头要同时出 **1080p 主码流 + 720p 子码流**（监控、直播都这么做：主码流录像，子码流给手机预览或者网络差的时候用），就得在送进编码器之前，用 RGA 把画面缩小。

> 用 RGA 把画面缩小
跑完能回答这 3 个问题：

1. 一路画面，怎么同时编出两路不同分辨率的码流？（两个 MppEncoder 实例 + RGA 缩放）
2. 两路码流的参数怎么配？码率给多少、文件差多大？（`bps`、`gop`，和 Day 1 学的是同一套 `mpp_enc_cfg`）
3. RGA 和 MPP 编码器之间怎么交接画面，才不经过 CPU 拷贝？（RGA 的输出 buffer 直接从 MPP 申请，缩完原样交给 `encode()`）

**今天不学**（够用就行，别学偏）：RGA 驱动和寄存器、im2d 的全部 API（alpha 混合、OSD、马赛克、填充）、RGA 内部怎么调度多个核。RGA 只用一个函数：`imresize`。

## 1. 整体流程

```
in_1080p_60f.nv12（Day 1 的 60 帧）
   │ 每帧读进 mainPicture.mppBuf（1920x1080，stride 1920x1088）
   ├─────────────────────────────────────► MppEncoder #1  4 Mbps ─► main_1080p.h264（主码流）
   │
   └─► RGA imresize ─► subPicture.mppBuf（1280x720）─► MppEncoder #2  1.5 Mbps ─► sub_720p.h264（子码流）
          ↑                       ↑
     读 mainPicture 的 fd    这块 buffer 也是从 MPP 申请的，RGA 写完直接交给编码器 #2
```

关键点：**两块 buffer 都从 MPP 的 buffer group（DRM 类型）申请**。RGA 通过 `mpp_buffer_get_fd()` 拿到 fd 去读写，编码器直接用 MppBuffer，三者用的是同一块物理内存，没有 CPU 拷贝。

## 2. 里程碑

|           | 做什么                                                          | 跑完能回答                                                   |   要摄像头吗    |
|-----------|-----------------------------------------------------------------|--------------------------------------------------------------|:---------------:|
| **M1**    | 读一帧 1080p → RGA 缩成 720p → 写成 NV12 文件，Mac 上 ffplay 看 | RGA 的输入输出怎么描述（宽高 + stride + 格式）；缩一帧要多久 |       ❌        |
| **M2** ⭐ | 文件双码流：每帧原图送编码器 #1，RGA 缩小后送编码器 #2          | 问题 1、2、3                                                 |       ❌        |
| **M3**    | 把 M2 的输入换成摄像头（Day 3 的零拷贝 import）                 | 实时双码流，30 fps 下两路编码跟不跟得上                      | ✅ 等摄像头装回 |

格式转换（NV12 → RGB）和旋转，这次**不单独做**：
- 旋转：Day 3 已经用编码器自带的 `prep:rotation` 做过（`cfg.rotation = 3`），编码前转正不需要 RGA。
- NV12 → RGB：是给 AI 推理、屏幕显示用的，和编码无关。以后做 AI 时再学，用法和 `imresize` 一样，换成 `imcvtcolor`。

## 3. 工程准备

### 3.1 目录

> 2026-11-08 重构：M1～M3 跑通以后，main.cpp 有 470 行，按"一个文件做一件事"拆开了。下面是重构后的样子；第 4～6 节贴的是重构**前**的代码（写的时候都在 main.cpp 里），逻辑和现在一样，对照下表找。

```
day4_rga/
├── CMakeLists.txt
├── run_on_board_with_mac.sh
└── src/
    ├── main.cpp                         只看参数 m1 / m2 / m3，调对应的函数
    ├── app/
    │   ├── app_config.h                 共用常量：宽高、板子上的文件路径、录多少帧
    │   ├── milestones.h                 声明 run_m1/m2/m3
    │   ├── m1_resize_one_frame.cpp      M1：RGA 缩放一帧
    │   ├── m2_dual_stream_from_file.cpp M2：文件双码流
    │   └── m3_dual_stream_from_camera.cpp M3：摄像头双码流
    ├── common/
    │   ├── log.h、mpp_utils.h           日志；MPP_CHECK、ALIGN、FilePtr / MppBufferPtr 等 RAII 句柄
    │   ├── nv12_picture.h/.cpp          Nv12Picture 结构体；申请 buffer、按 stride 读写一帧
    │   └── time_utils.h                 get_now_ms
    ├── encoder/
    │   ├── mpp_encoder.h/.cpp           一个 MPP 编码器（用户手写）
    │   └── dual_stream_encoder.h/.cpp   M2、M3 共用：两个编码器 + 子码流 buffer + 两个码流文件
    ├── rga/rga_resizer.h/.cpp           用 RGA 缩放一帧 NV12
    └── camera/camera_capture.h/.cpp     摄像头采集（M3）
```

| 重构前（`603dbee` 的 main.cpp） | 重构后 |
|---|---|
| `alloc_nv12_picture` | `common/nv12_picture.cpp` 的 `alloc_nv12_buffer`：返回 `MppBufferPtr`，离开作用域自动 `mpp_buffer_put` |
| 手写 `{nullptr, w, h, ALIGN(w,16), ALIGN(h,16)}` | `make_aligned_nv12_picture(w, h)` |
| `read_nv12_frame`、`write_nv12_frame` | `common/nv12_picture.cpp`，代码没改 |
| `get_now_ms` | `common/time_utils.h` |
| `write_packet`、`init_encoder_and_write_header`、`drain_encoder` | `encoder/dual_stream_encoder.cpp`，代码没改 |
| M2、M3 里各写一遍的"两个编码器 + 子码流 buffer + 两个文件 + 统计字节数" | `DualStreamEncoder`：`open` → 每帧 `encode_main_and_resize_to_sub` + `encode_sub` → `drain_both` |
| 每个 run 函数结尾一项项 `mpp_buffer_put`、`fclose`、`mpp_buffer_group_put` | 用 `mpp_utils.h` 里的 `FilePtr`、`MppBufferPtr`、`MppBufferGroupPtr`，函数返回时自动释放 |
| `run_m1_*`、`run_m2_*`、`run_m3_*` | `app/` 下各一个文件 |

M3 里 QBUF 夹在 `encode_main_and_resize_to_sub` 和 `encode_sub` 之间，`DualStreamEncoder` 拆成这两步就是为了它（见 6.2）。

重构后 2026-11-08 上板验证：M1 输出 md5 还是 `d00d3d5e92a2b7dcab9e580a2b6ff6db`；M2 两个码流和重构前**逐字节相同**（md5 `da5d76fe…`、`4ec9db88…`，和 `result_from_board/` 里的一样）；M3 300 帧 30.0 fps。

### 3.2 CMakeLists.txt

和 day3_cam2h264 的一样，多链接一个 `rga`（工具链 sysroot 里有 `librga.so.2` 和头文件 `rga/im2d.hpp`）：

```cmake
cmake_minimum_required(VERSION 4.3)
project(day4_rga)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(day4_rga
        src/main.cpp
        src/encoder/mpp_encoder.cpp
        src/rga/rga_resizer.cpp
)

target_include_directories(day4_rga PRIVATE src)
target_compile_options(day4_rga PRIVATE -Wall -Wextra)

if (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    # rockchip_mpp：MPP 编码；rga：RGA 缩放（-lrga → sysroot 里的 librga.so）
    target_link_libraries(day4_rga PRIVATE rockchip_mpp rga)
    target_link_options(day4_rga PRIVATE "-Wl,-rpath,/userdata/mpp_build/lib")
endif ()
```

### 3.3 板子上要先确认的事（板子能用时）

```bash
adb shell 'ls -l /userdata/av/in_1080p_60f.nv12'   # Day 1 的输入文件还在不在（应该是 1920*1080*1.5*60 = 186624000 字节）
adb shell 'ls -l /usr/lib/librga.so*'              # 板子上有没有 librga.so.2（程序运行时要用）
adb shell 'ls -l /dev/rga'                         # RGA 设备节点
```

## 4. M1：RGA 缩放一帧

### 4.1 要写的东西

|  步  | 做什么                                           | 用到的函数                                                      |
|:----:|--------------------------------------------------|-----------------------------------------------------------------|
|  1   | 从 MPP 申请两块 buffer：1080p 输入、720p 输出    | `mpp_buffer_group_get_internal`、`mpp_buffer_get`（Day 1 学过） |
|  2   | 读一帧 1080p 进输入 buffer（按 stride 一行行读） | 和 Day 1 的 `read_nv12_rows` 一样                               |
|  3   | 两块 buffer 导入 RGA，换成 RGA 的 handle         | `mpp_buffer_get_fd` → `importbuffer_fd`                         |
|  4   | 用 handle + 宽高 + stride + 格式描述画面         | `wrapbuffer_handle`                                             |
|  5   | 检查参数，再缩放                                 | `imcheck` → `imresize`                                          |
|  6   | 释放 handle，把 720p 写进文件                    | `releasebuffer_handle`                                          |

**和 MPP 唯一的新交点是第 4 步的 stride**：`wrapbuffer_handle` 的 `wstride`、`hstride` 就是 MPP 的 `hor_stride`、`ver_stride`。输入用的是 Day 1 编码器那组：1920 × 1088（`ALIGN(1080, 16)`）。填成 1080，RGA 会从第 1080 行去找 UV，可数据其实在第 1088 行开始，输出的颜色就会错乱。

### 4.2 `src/rga/rga_resizer.h`

```cpp
/*
 * rga_resizer.h：用 RGA 把一帧 NV12 缩放到另一个尺寸，输入、输出都是 MppBuffer
 *
 * 为什么输入输出都用 MppBuffer：
 *   RGA 的输出要直接送进 MPP 编码器。输出 buffer 从 MPP 的 buffer group 申请，
 *   RGA 写完，这块 MppBuffer 原样交给 encoder.encode()，中间不用 CPU 拷贝。
 */
#pragma once

#include <rockchip/rk_mpi.h> // MppBuffer

// 一帧 NV12 画面：放在哪块 MppBuffer 里、画面多大、内存里怎么排
struct Nv12Picture {
    MppBuffer mppBuf = nullptr; // 存放这一帧的 MPP 硬件 buffer（DRM 分配，RGA 和编码器都能直接读写）
    int width        = 0;       // 画面宽，单位像素
    int height       = 0;       // 画面高，单位像素
    // 水平跨距：一行在内存里占多少字节（NV12 的 Y 是 1 字节 / 像素，所以数值上也等于像素数），可能比 width 大
    int horStride = 0;
    // 垂直跨距：Y 平面在内存里占多少行。UV 平面从第 verStride 行开始，可能比 height 大（例如 1080 → 1088）
    int verStride = 0;
};

/**
 * @brief 用 RGA 把 srcPicture 缩放成 dstPicture 的尺寸（NV12 → NV12）
 * @param[in] srcPicture 输入画面，mppBuf 里已经有一帧完整的 NV12
 * @param[in] dstPicture 输出画面：mppBuf 已经申请好，width / height / 两个 stride 填好
 * @return true = 缩放成功，dstPicture.mppBuf 里是缩好的画面；false = 导入或缩放失败（原因已打印）
 * @note 每次调用都把两块 buffer 导入 RGA（importbuffer_fd），用完马上释放（releasebuffer_handle）
 * @note 是同步调用：函数返回时 RGA 已经写完，可以直接把 dstPicture.mppBuf 交给编码器
 */
bool resize_nv12_by_rga(const Nv12Picture& srcPicture, const Nv12Picture& dstPicture);
```

### 4.3 `src/rga/rga_resizer.cpp`

```cpp
#include "rga/rga_resizer.h"

#include "common/log.h"
#include <cstring>   // memset：im2d 头文件里的 wrapbuffer_handle 等宏用到了它，但头文件自己没 include
#include <rga/rga.h> // RK_FORMAT_YCbCr_420_SP（就是 NV12）

#include <rga/im2d.hpp> // im2d API：importbuffer_fd、wrapbuffer_handle、imcheck、imresize、releasebuffer_handle

static const char* kTag = "RGA";

/**
 * @brief 把一块 MppBuffer 导入 RGA，得到 RGA 自己的 handle
 * @param[in] mppBuf 要导入的 MPP buffer
 * @return 成功返回非 0 的 handle；失败返回 0
 * @note 用完要调 releasebuffer_handle(handle) 释放
 */
static rga_buffer_handle_t import_mpp_buffer_to_rga(MppBuffer mppBuf) {
    // mpp_buffer_get_fd：这块 MPP buffer 背后的 dma_buf fd。RGA 拿着 fd 就能直接访问这块内存，不经过 CPU
    const int dmaBufFd = mpp_buffer_get_fd(mppBuf);
    // mpp_buffer_get_size：这块 buffer 一共多少字节。RGA 要知道边界，越界访问时它会报错而不是乱写
    const int sizeBytes = static_cast<int>(mpp_buffer_get_size(mppBuf));
    // importbuffer_fd：告诉 RGA 驱动"以后用这块内存"，返回一个编号（handle）。后面 RGA 的操作都认 handle
    return importbuffer_fd(dmaBufFd, sizeBytes);
}

bool resize_nv12_by_rga(const Nv12Picture& srcPicture, const Nv12Picture& dstPicture) {
    // 第 1 步 导入：两块 MppBuffer 各换一个 RGA handle
    const rga_buffer_handle_t srcHandle = import_mpp_buffer_to_rga(srcPicture.mppBuf);
    const rga_buffer_handle_t dstHandle = import_mpp_buffer_to_rga(dstPicture.mppBuf);
    if (srcHandle == 0 || dstHandle == 0) {
        LOGE(kTag, "importbuffer_fd 失败：src handle %u，dst handle %u", srcHandle, dstHandle);
        if (srcHandle != 0) {
            releasebuffer_handle(srcHandle);
        }
        if (dstHandle != 0) {
            releasebuffer_handle(dstHandle);
        }
        return false;
    }

    // 第 2 步 描述画面：handle + 宽高 + 格式 + 跨距，打包成 rga_buffer_t。
    //   wstride / hstride 就是 MPP 里的 hor_stride / ver_stride：RGA 靠它们找到每一行和 UV 平面的起点，
    //   和编码器用的必须是同一组数，否则 RGA 读到的 UV 位置不对，输出颜色错乱
    rga_buffer_t srcImage = wrapbuffer_handle(srcHandle, srcPicture.width, srcPicture.height, RK_FORMAT_YCbCr_420_SP,
        srcPicture.horStride, srcPicture.verStride);
    rga_buffer_t dstImage = wrapbuffer_handle(dstHandle, dstPicture.width, dstPicture.height, RK_FORMAT_YCbCr_420_SP,
        dstPicture.horStride, dstPicture.verStride);

    // 第 3 步 检查：imcheck 在真正干活前检查参数（格式、对齐、尺寸是否支持），有问题直接告诉你原因
    bool isResizeOk          = false;
    const IM_STATUS checkRet = static_cast<IM_STATUS>(imcheck(srcImage, dstImage, {}, {}));
    if (checkRet != IM_STATUS_NOERROR) {
        LOGE(kTag, "imcheck: %s", imStrError(checkRet));
    } else {
        // 第 4 步 缩放：按 dstImage 的宽高缩放。默认同步：函数返回时 RGA 已经写完
        const IM_STATUS resizeRet = imresize(srcImage, dstImage);
        isResizeOk                = resizeRet == IM_STATUS_SUCCESS;
        if (!isResizeOk) {
            LOGE(kTag, "imresize: %s", imStrError(resizeRet));
        }
    }

    // 第 5 步 释放 handle：只是告诉 RGA"不用了"，两块 MppBuffer 本身不受影响
    releasebuffer_handle(srcHandle);
    releasebuffer_handle(dstHandle);
    return isResizeOk;
}
```

### 4.4 `src/main.cpp`（M1 和 M2 都在里面）

M1 是 `run_m1_resize_one_frame()`，M2 是 `run_m2_dual_stream_from_file()`。读写文件、申请 buffer 的几个小函数两个里程碑共用。

```cpp
/*
 * day4_rga：RGA 缩放 + MPP 双码流
 *
 *   ./day4_rga m1   M1：读一帧 1080p NV12 → RGA 缩成 720p → 写成 NV12 文件，Mac 上用 ffplay 看
 *   ./day4_rga m2   M2：文件双码流。每一帧：原图送编码器 #1（1080p 主码流），
 *                       RGA 缩成 720p 送编码器 #2（720p 子码流），得到 main.h264 和 sub.h264
 *
 * 编码器 MppEncoder 从 day3_cam2h264 原样复制过来（src/encoder/、src/common/）
 */
#include "common/log.h"
#include "common/mpp_utils.h"
#include "encoder/mpp_encoder.h"
#include "rga/rga_resizer.h"
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

static const char* kTag = "Main";

// 输入：Day 1 用过的 60 帧 1080p NV12 文件（每帧紧挨着存，没有填充）
static const char* kInputFilePath   = "/userdata/av/in_1080p_60f.nv12";
static const char* kResizedFilePath = "/userdata/av/out_720p.nv12";   // M1 的输出
static const char* kMainStreamPath  = "/userdata/av/main_1080p.h264"; // M2 主码流
static const char* kSubStreamPath   = "/userdata/av/sub_720p.h264";   // M2 子码流

static const int kMainWidth  = 1920;
static const int kMainHeight = 1080;
static const int kSubWidth   = 1280;
static const int kSubHeight  = 720;

/**
 * @brief 取当前时间，单位毫秒（只用来算两次之间的差）
 * @return 从某个固定起点开始的毫秒数
 */
static double get_now_ms() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

/**
 * @brief 从 MPP 的 DRM buffer group 里申请一块能放下一帧 NV12 的 buffer，填进 picture.mppBuf
 * @param[in]     bufGroup MPP buffer group（DRM + CACHABLE）
 * @param[in,out] picture  进来时 width / height / horStride / verStride 已经填好；成功时填 mppBuf
 * @return true = 申请成功；false = 失败
 * @note 用完调 mpp_buffer_put(picture.mppBuf) 释放
 */
static bool alloc_nv12_picture(MppBufferGroup bufGroup, Nv12Picture& picture) {
    // NV12 一帧的大小：Y 平面 horStride × verStride 字节，UV 平面是它的一半
    const size_t frameSizeBytes = static_cast<size_t>(picture.horStride) * picture.verStride * 3 / 2;
    if (mpp_buffer_get(bufGroup, &picture.mppBuf, frameSizeBytes) != MPP_OK) {
        LOGE(kTag, "mpp_buffer_get %zu 字节失败", frameSizeBytes);
        return false;
    }
    return true;
}

/**
 * @brief 从 NV12 文件读一帧，按 picture 的 stride 排进 picture.mppBuf
 * @param[in] inputFile 已经打开的 NV12 文件（每帧 width × height × 3 / 2 字节，没有填充）
 * @param[in] picture   目标画面，mppBuf 已经申请好
 * @return true = 读到完整的一帧；false = 文件读完了或者读错了
 * @note 和 Day 1 的 ReadYUV::read_nv12_rows 一样：一行一行读，因为文件里每行 width 字节，buffer 里每行 horStride 字节
 */
static bool read_nv12_frame(FILE* inputFile, const Nv12Picture& picture) {
    auto* frameStart = static_cast<uint8_t*>(mpp_buffer_get_ptr(picture.mppBuf));
    // CPU 要写 cached 的 buffer：写之前 sync_begin，写完 sync_end，保证 RGA / 编码器读到的是 CPU 刚写的
    mpp_buffer_sync_begin(picture.mppBuf);
    bool isReadOk = true;
    // Y 平面：height 行
    for (int rowIndex = 0; rowIndex < picture.height && isReadOk; ++rowIndex) {
        isReadOk = fread(frameStart + rowIndex * picture.horStride, 1, picture.width, inputFile)
                == static_cast<size_t>(picture.width);
    }
    // UV 平面：从第 verStride 行开始，height / 2 行
    uint8_t* uvStart = frameStart + picture.horStride * picture.verStride;
    for (int rowIndex = 0; rowIndex < picture.height / 2 && isReadOk; ++rowIndex) {
        isReadOk = fread(uvStart + rowIndex * picture.horStride, 1, picture.width, inputFile)
                == static_cast<size_t>(picture.width);
    }
    mpp_buffer_sync_end(picture.mppBuf);
    return isReadOk;
}

/**
 * @brief 把 picture 里的一帧 NV12 按 width × height 紧凑地写进文件（去掉 stride 填充），Mac 上 ffplay 能直接看
 * @param[in] outputFile 已经打开的输出文件
 * @param[in] picture    要写的画面
 * @return true = 写完；false = 写失败
 */
static bool write_nv12_frame(FILE* outputFile, const Nv12Picture& picture) {
    auto* frameStart = static_cast<const uint8_t*>(mpp_buffer_get_ptr(picture.mppBuf));
    // RGA 刚用 DMA 写过这块 buffer，CPU 读之前 sync_begin，避免读到 CPU 缓存里的旧数据
    mpp_buffer_sync_begin(picture.mppBuf);
    bool isWriteOk = true;
    for (int rowIndex = 0; rowIndex < picture.height && isWriteOk; ++rowIndex) {
        isWriteOk = fwrite(frameStart + rowIndex * picture.horStride, 1, picture.width, outputFile)
                 == static_cast<size_t>(picture.width);
    }
    const uint8_t* uvStart = frameStart + picture.horStride * picture.verStride;
    for (int rowIndex = 0; rowIndex < picture.height / 2 && isWriteOk; ++rowIndex) {
        isWriteOk = fwrite(uvStart + rowIndex * picture.horStride, 1, picture.width, outputFile)
                 == static_cast<size_t>(picture.width);
    }
    mpp_buffer_sync_end(picture.mppBuf);
    return isWriteOk;
}

/**
 * @brief 把一个编码包写进码流文件（空包跳过）
 * @param[in] streamFile    码流文件
 * @param[in] encodedPacket encode() 取到的包
 * @return true = 写完或者是空包；false = 写失败
 */
static bool write_packet(FILE* streamFile, const EncodedPacket& encodedPacket) {
    if (encodedPacket.data.empty()) {
        return true;
    }
    return fwrite(encodedPacket.data.data(), 1, encodedPacket.data.size(), streamFile) == encodedPacket.data.size();
}

/**
 * @brief 打开编码器，并把 SPS/PPS 写到码流文件开头
 * @param[in]  encoderCfg 编码参数（宽高、stride、码率、GOP）
 * @param[in]  streamFile 码流文件
 * @param[out] encoder    要初始化的编码器
 * @return true = 成功；false = init 或 get_header 或写文件失败
 */
static bool init_encoder_and_write_header(const EncoderConfig& encoderCfg, FILE* streamFile, MppEncoder& encoder) {
    if (!encoder.init(encoderCfg)) {
        return false;
    }
    std::vector<uint8_t> header;
    if (!encoder.get_header(header)) {
        return false;
    }
    return fwrite(header.data(), 1, header.size(), streamFile) == header.size();
}

/**
 * @brief 给编码器送 EOS，把还没吐出来的包全部取出来写进文件
 * @param[in] encoder    编码器
 * @param[in] streamFile 码流文件
 * @return true = 取到了 EOS 包；false = encode 失败
 */
static bool drain_encoder(MppEncoder& encoder, FILE* streamFile) {
    EncodedPacket encodedPacket;
    // 最多取 30 次，防止编码器一直不给 EOS 时死循环
    for (int tryIndex = 0; tryIndex < 30; ++tryIndex) {
        if (!encoder.encode(nullptr, true, encodedPacket)) {
            return false;
        }
        write_packet(streamFile, encodedPacket);
        if (encodedPacket.isEos) {
            return true;
        }
    }
    LOGW(kTag, "送了 30 次 EOS 还没拿到 EOS 包");
    return true;
}

/**
 * @brief M1：读一帧 1080p → RGA 缩成 720p → 写 NV12 文件，并打印 RGA 缩放一帧的耗时
 * @return 0 = 成功；1 = 失败
 */
static int run_m1_resize_one_frame() {
    LOGI(kTag, "===== M1：RGA 把一帧 1080p 缩成 720p =====");
    // DRM：硬件（RGA、编码器）能直接访问的内存；CACHABLE：CPU 读写走缓存，CPU 读写前后要 sync
    MppBufferGroup bufGroup = nullptr;
    if (mpp_buffer_group_get_internal(&bufGroup, MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE) != MPP_OK) {
        LOGE(kTag, "mpp_buffer_group_get_internal 失败");
        return 1;
    }
    // 输入 1080p：verStride 用 ALIGN(1080, 16) = 1088，和 Day 1 编码器的 ver_stride 一样
    Nv12Picture srcPicture = {nullptr, kMainWidth, kMainHeight, ALIGN(kMainWidth, 16), ALIGN(kMainHeight, 16)};
    // 输出 720p：1280、720 本来就是 16 的倍数，stride = 宽高
    Nv12Picture dstPicture = {nullptr, kSubWidth, kSubHeight, ALIGN(kSubWidth, 16), ALIGN(kSubHeight, 16)};
    FILE* inputFile        = fopen(kInputFilePath, "rb");
    FILE* outputFile       = fopen(kResizedFilePath, "wb");
    int exitCode           = 1;
    if (inputFile == nullptr || outputFile == nullptr) {
        LOGE(kTag, "打开文件失败：%s", strerror(errno));
    } else if (alloc_nv12_picture(bufGroup, srcPicture) && alloc_nv12_picture(bufGroup, dstPicture)
               && read_nv12_frame(inputFile, srcPicture)) {
        const double beforeResizeMs = get_now_ms();
        const bool isResizeOk       = resize_nv12_by_rga(srcPicture, dstPicture);
        const double resizeMs       = get_now_ms() - beforeResizeMs;
        if (isResizeOk && write_nv12_frame(outputFile, dstPicture)) {
            LOGI(kTag, "%dx%d（stride %dx%d）-> %dx%d（stride %dx%d），RGA 用时 %.2f ms，写到 %s", srcPicture.width,
                srcPicture.height, srcPicture.horStride, srcPicture.verStride, dstPicture.width, dstPicture.height,
                dstPicture.horStride, dstPicture.verStride, resizeMs, kResizedFilePath);
            exitCode = 0;
        }
    }
    if (srcPicture.mppBuf != nullptr) {
        mpp_buffer_put(srcPicture.mppBuf);
    }
    if (dstPicture.mppBuf != nullptr) {
        mpp_buffer_put(dstPicture.mppBuf);
    }
    if (inputFile != nullptr) {
        fclose(inputFile);
    }
    if (outputFile != nullptr) {
        fclose(outputFile);
    }
    mpp_buffer_group_put(bufGroup);
    return exitCode;
}

/**
 * @brief M2：文件双码流。每一帧：原图 → 编码器 #1（1080p）；RGA 缩成 720p → 编码器 #2（720p）
 * @return 0 = 两路都编完；1 = 任何一步失败
 */
static int run_m2_dual_stream_from_file() {
    LOGI(kTag, "===== M2：文件双码流（1080p 主码流 + 720p 子码流）=====");
    MppBufferGroup bufGroup = nullptr;
    if (mpp_buffer_group_get_internal(&bufGroup, MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE) != MPP_OK) {
        LOGE(kTag, "mpp_buffer_group_get_internal 失败");
        return 1;
    }
    Nv12Picture mainPicture = {nullptr, kMainWidth, kMainHeight, ALIGN(kMainWidth, 16), ALIGN(kMainHeight, 16)};
    Nv12Picture subPicture  = {nullptr, kSubWidth, kSubHeight, ALIGN(kSubWidth, 16), ALIGN(kSubHeight, 16)};

    // 两路编码参数：只有宽高、stride、码率不一样。子码流像素是主码流的 4/9，码率给得少一些
    EncoderConfig mainCfg;
    mainCfg.width     = mainPicture.width;
    mainCfg.height    = mainPicture.height;
    mainCfg.horStride = mainPicture.horStride;
    mainCfg.verStride = mainPicture.verStride;
    mainCfg.bps       = 4 * 1000 * 1000;
    EncoderConfig subCfg;
    subCfg.width     = subPicture.width;
    subCfg.height    = subPicture.height;
    subCfg.horStride = subPicture.horStride;
    subCfg.verStride = subPicture.verStride;
    subCfg.bps       = 1500 * 1000;

    FILE* inputFile      = fopen(kInputFilePath, "rb");
    FILE* mainStreamFile = fopen(kMainStreamPath, "wb");
    FILE* subStreamFile  = fopen(kSubStreamPath, "wb");
    MppEncoder mainEncoder; // 两个独立的编码器实例（两个 MppCtx），各管各的参数和码流
    MppEncoder subEncoder;
    int exitCode = 1;
    if (inputFile == nullptr || mainStreamFile == nullptr || subStreamFile == nullptr) {
        LOGE(kTag, "打开文件失败：%s", strerror(errno));
    } else if (alloc_nv12_picture(bufGroup, mainPicture) && alloc_nv12_picture(bufGroup, subPicture)
               && init_encoder_and_write_header(mainCfg, mainStreamFile, mainEncoder)
               && init_encoder_and_write_header(subCfg, subStreamFile, subEncoder)) {
        int frameCount         = 0;
        size_t mainStreamBytes = 0;
        size_t subStreamBytes  = 0;
        double resizeTotalMs   = 0;
        bool isAllOk           = true;
        EncodedPacket mainPacket;
        EncodedPacket subPacket;
        // 每一轮处理一帧：读 → 主码流编码 → RGA 缩放 → 子码流编码
        while (isAllOk && read_nv12_frame(inputFile, mainPicture)) {
            // 第 1 路：原图直接送编码器 #1
            isAllOk =
                mainEncoder.encode(mainPicture.mppBuf, false, mainPacket) && write_packet(mainStreamFile, mainPacket);
            // 第 2 路：RGA 缩成 720p，写进 subPicture.mppBuf，这块 buffer 原样送编码器 #2
            const double beforeResizeMs = get_now_ms();
            isAllOk                     = isAllOk && resize_nv12_by_rga(mainPicture, subPicture);
            resizeTotalMs += get_now_ms() - beforeResizeMs;
            isAllOk = isAllOk && subEncoder.encode(subPicture.mppBuf, false, subPacket)
                   && write_packet(subStreamFile, subPacket);
            mainStreamBytes += mainPacket.data.size();
            subStreamBytes += subPacket.data.size();
            ++frameCount;
        }
        if (isAllOk && drain_encoder(mainEncoder, mainStreamFile) && drain_encoder(subEncoder, subStreamFile)) {
            LOGI(kTag, "%d 帧；RGA 缩放平均 %.2f ms/帧", frameCount, frameCount > 0 ? resizeTotalMs / frameCount : 0);
            LOGI(kTag, "主码流 %dx%d：%zu 字节 -> %s", mainCfg.width, mainCfg.height, mainStreamBytes, kMainStreamPath);
            LOGI(kTag, "子码流 %dx%d：%zu 字节 -> %s", subCfg.width, subCfg.height, subStreamBytes, kSubStreamPath);
            exitCode = 0;
        }
    }
    if (mainPicture.mppBuf != nullptr) {
        mpp_buffer_put(mainPicture.mppBuf);
    }
    if (subPicture.mppBuf != nullptr) {
        mpp_buffer_put(subPicture.mppBuf);
    }
    for (FILE* file : {inputFile, mainStreamFile, subStreamFile}) {
        if (file != nullptr) {
            fclose(file);
        }
    }
    mpp_buffer_group_put(bufGroup);
    return exitCode;
}

int main(int argc, char** argv) {
    const char* experimentName = argc > 1 ? argv[1] : "m1";
    if (strcmp(experimentName, "m1") == 0) {
        return run_m1_resize_one_frame();
    }
    if (strcmp(experimentName, "m2") == 0) {
        return run_m2_dual_stream_from_file();
    }
    LOGE(kTag, "用法：%s m1 | m2", argv[0]);
    return 1;
}
```

### 4.5 运行和验证

```bash
./run_on_board_with_mac.sh m1
adb pull /userdata/av/out_720p.nv12 .
ffplay -f rawvideo -pixel_format nv12 -video_size 1280x720 out_720p.nv12
```

实测输出（2026-11-05）：

```
I Main: ===== M1：RGA 把一帧 1080p 缩成 720p =====
rga_api version 1.10.0_[8]
I Main: 1920x1080（stride 1920x1088）-> 1280x720（stride 1280x720），RGA 用时 3.75 ms，写到 /userdata/av/out_720p.nv12
```

- RGA 用时：几次运行在 3.11～3.75 ms 之间（写文档时猜的是 1.xx ms，猜少了）。M1 只缩一帧，是程序里第一次调用 RGA，推断包含了 librga 初始化的时间；M2 连续缩 60 帧，平均 2.65 ms
- 输出 1,382,400 字节 = 1280 × 720 × 1.5，md5 `d00d3d5e92a2b7dcab9e580a2b6ff6db`，每次运行都一样
- 和输入第 1 帧并排看：画面一样，只是小了，颜色正常

### 4.6 要回答的问题

1. 把 `srcPicture` 的 `verStride` 从 1088 改成 1080 再跑，画面会怎样？为什么？
   2026-11-06 实测：**画面顶部多一条绿边**，其他地方颜色整体往下错 16 行（这张图很平滑，肉眼看不出）。RGA 以为 UV 从第 1080 行开始，把 8 行填充（全是 0）当成了 UV 的前 8 行，U = V = 0 就是绿色。`imcheck` 照样通过，不报任何错。写文档时预测的"颜色错乱"比实测严重。详见 [doc/1_理解M1的需求和代码.md](doc/1_理解M1的需求和代码.md) 第 5 节
2. RGA 缩一帧要多少 ms？30 fps 时每帧只有 33 ms，RGA 占多少？
   实测：第一次 3.1～3.75 ms，连续缩时 2.65 ms，占一帧时间的 8%～10%

## 5. M2：文件双码流 ⭐

### 5.1 要写的东西

`run_m2_dual_stream_from_file()`（代码在 4.4 的 main.cpp 里）：

|  步  | 做什么                                                                 | MPP 知识点                                                                  |
|:----:|------------------------------------------------------------------------|-----------------------------------------------------------------------------|
|  1   | 申请 1080p、720p 两块 buffer                                           | 同 M1                                                                       |
|  2   | **开两个编码器**，各自 `init` + `get_header`，SPS/PPS 写到各自文件开头 | 两个 `MppCtx` 互不影响；每路码流都要自己的 SPS/PPS                          |
|  3   | 两路参数只差三项：宽高、stride、码率（4 Mbps / 1.5 Mbps）              | `mpp_enc_cfg` 的 `prep:width/height/hor_stride/ver_stride`、`rc:bps_target` |
|  4   | 每一帧：读 → 编码器 #1 编原图 → RGA 缩小 → 编码器 #2 编小图            | 同一帧进两个编码器，两路帧数一样                                            |
|  5   | 文件读完，给两个编码器都送 EOS，把剩下的包取完                         | Day 1 学过的 EOS 流程                                                       |

### 5.2 运行和验证

```bash
./run_on_board_with_mac.sh m2
adb pull /userdata/av/main_1080p.h264 .
adb pull /userdata/av/sub_720p.h264 .
ffprobe -v error -show_entries stream=width,height,nb_read_frames -count_frames main_1080p.h264
ffprobe -v error -show_entries stream=width,height,nb_read_frames -count_frames sub_720p.h264
ffplay sub_720p.h264
```

> 也可以用 `./get_result_from_board.sh` 一次把两个码流拷到 `result_from_board/`，会核对大小。

实测输出（2026-11-06）：

```
I Main: ===== M2：文件双码流（1080p 主码流 + 720p 子码流）=====
rga_api version 1.10.0_[8]
I Main: 60 帧；RGA 缩放平均 2.65 ms/帧
I Main: 主码流 1920x1080：898584 字节 -> /userdata/av/main_1080p.h264
I Main: 子码流 1280x720：337734 字节 -> /userdata/av/sub_720p.h264
```

（60 帧 ÷ 30 fps = 2 秒。按"文件 ≈ 码率 × 时长"估算是 1 MB 和 0.375 MB；实际主码流 ≈ 3.6 Mbps、子码流 ≈ 1.35 Mbps，都比设定值低 10% 左右，推断是 2 秒太短、码率控制还没收敛。程序打印的字节数比文件实际大小少 40 和 39 字节，是开头的 SPS/PPS 没算进统计）

| | 文件大小 | profile / level | 宽 × 高 | 帧数 | ffmpeg 完整解码 |
|---|---|---|---|---|---|
| main_1080p.h264 | 898,624 字节 | High / 4.0 | 1920 × 1080 | 60 | 无错 |
| sub_720p.h264 | 337,773 字节 | High / 3.1 | 1280 × 720 | 60 | 无错 |

验收（2026-11-06 都通过）：
- [x] 两个文件 ffprobe 都是 60 帧，宽高分别是 1920x1080、1280x720
- [x] 两个文件都能播放，内容一样，只是清晰度不同
- [x] 两个文件的大小比（38%），和码率比（1.5 ÷ 4 = 37.5%）接近

顺带的对照实验：子码流码率设成和主码流一样的 4 Mbps 时，子码流 859,490 字节（主码流的 96%），几乎一样大。**分辨率降了、码率不降，子码流就白做了**。详见 [doc/1_理解M2的需求和代码.md](doc/2_理解M2的需求和代码.md) 第 3.6 节

### 5.3 要回答的问题

1. 为什么两路码流要各自写 SPS/PPS？把子码流文件开头的 SPS/PPS 去掉，还能播吗？（SPS 里有宽高、profile，解码器没有它不知道怎么解）
2. 子码流的码率从 1.5 Mbps 改成 0.5 Mbps，文件变多小？ffplay 看画质差多少？
3. 子码流改成 H.265（`subCfg.type = MPP_VIDEO_CodingHEVC`），同样码率，画质和 H.264 比怎么样？

## 6. M3：摄像头双码流（摄像头装回后再做）

> 2026-11-06 写，交叉编译通过（`-Wall -Wextra` 0 warning）。2026-11-08 用户照着写进 `src/`，上板跑通，6.7 换成了实测。

### 6.1 和 M2 比，只差 3 处

|          | M2                                      | M3                                                                         |
|----------|-----------------------------------------|----------------------------------------------------------------------------|
| 输入     | 每帧从文件读进 `pictureMain.mppBuf`     | 摄像头 DQBUF 出来的那块 buffer（Day 3 的 `mpp_buffer_import`）             |
| stride   | 自己申请，1920 × **1088**               | 用摄像头驱动给的，1920 × **1080**（Day 3 实测）                            |
| 旋转     | 不用                                    | 两个编码器都设 `rotation = 3`；主码流转正后 1080 × 1920，子码流 720 × 1280 |

其他（两个编码器、RGA 缩放、写 SPS/PPS、drain）都和 M2 一样，直接复用 `main.cpp` 里已有的函数。

### 6.2 每一帧的数据流

```
摄像头 DQBUF ─► cameraBuf（1920x1080，stride 1920x1080，驱动的内存）
                  │
                  ├──────────────► 编码器 #1（rotation 3）─► cam_main_1080p.h264
                  │
                  └─► RGA imresize ─► pictureSub（1280x720，MPP 申请的）
                                         │
   ⭐ 编码器 #1 和 RGA 都返回了 ─► QBUF 把 cameraBuf 还给摄像头
                                         │
                                         └─► 编码器 #2（rotation 3）─► cam_sub_720p.h264
```

**这一节要学的点：摄像头的 buffer 什么时候能还？** 读它的有两个硬件：编码器 #1 和 RGA。两个调用都是同步的（返回时硬件已经读完），所以两个都返回以后就能 QBUF。编码器 #2 读的是 `pictureSub`，不是摄像头的 buffer，不用等它。

CPU 从头到尾不碰画面，所以：不用 `mmap` 摄像头的 buffer，也不用 `mpp_buffer_sync_begin/end`（sync 只在 CPU 读写 cached 内存时才需要）。

### 6.3 要写的东西

| 文件                                     | 做什么                                                                                                                      |
|------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------|
| `src/camera/camera_capture.h/.cpp`（新） | 把 Day 3 `main.cpp` 里的 V4L2 步骤收进 `CameraCapture` 类：开摄像头、取一帧、还一帧，析构时自动释放。去掉了 mmap 和拷贝版本 |
| `src/main.cpp`（加）                     | 加 `run_m3_dual_stream_from_camera()`，`main()` 里加 `m3`                                                                   |
| `CMakeLists.txt`（加）                   | `add_executable` 里加 camera 的两个文件                                                                                     |

`CMakeLists.txt` 的 `add_executable` 改成：

```cmake
add_executable(day4_rga
        src/main.cpp
        src/common/log.h
        src/common/mpp_utils.h
        src/encoder/mpp_encoder.cpp
        src/encoder/mpp_encoder.h
        src/rga/rga_resizer.cpp
        src/rga/rga_resizer.h
        src/camera/camera_capture.cpp
        src/camera/camera_capture.h)
```

### 6.4 `src/camera/camera_capture.h`

```cpp
//
// M3：摄像头采集。把 Day 3 main.cpp 里的 V4L2 步骤收进一个类，main.cpp 只管"取一帧、还一帧"
//
#pragma once

#include <linux/videodev2.h>
#include <rockchip/rk_mpi.h>

/**
 * @brief 一块摄像头 buffer：驱动分配的内存，导出成 dma_buf fd，再包装成 MppBuffer
 * @note M3 里 CPU 不读画面（编码器、RGA 都是硬件，直接用 fd），所以不 mmap，没有 CPU 地址
 */
struct CameraBuffer {
    int dmaFd = -1;          // VIDIOC_EXPBUF 写回的 dma_buf fd，用完要 close
    MppBuffer mppBuf = nullptr; // mpp_buffer_import 包装出来的 MppBuffer，编码器、RGA 都用它
};

/**
 * @brief 摄像头采集：打开 /dev/videoN → 设格式 → 申请 buffer → 导出成 MppBuffer → 开拍；
 *        之后每帧 dequeue_filled_buffer() 取、queue_buffer() 还
 * @note 析构时自动 STREAMOFF、释放 MppBuffer、close fd，调用方不用管
 */
class CameraCapture {
public:
    static constexpr int kBufCount = 4; // 向驱动要几块 buffer（Day 3 实测：4 块才能跑满 30 fps）

    CameraCapture() = default;
    ~CameraCapture();
    CameraCapture(const CameraCapture &) = delete;
    CameraCapture &operator=(const CameraCapture &) = delete;

    /**
     * @brief 打开摄像头并开拍：open → QUERYCAP → S_FMT / G_FMT → REQBUFS → QUERYBUF + EXPBUF + import → QBUF × 4 → STREAMON
     * @param[in] devicePath  摄像头节点，例如 "/dev/video22"（ISP0 的 mainpath，Day 3 用 CameraFinder 找到的）
     * @param[in] width       想要的宽，单位像素，例如 1920
     * @param[in] height      想要的高，单位像素，例如 1080
     * @return true = 已经开拍；false = 某一步失败（已打印原因，已拿到的资源由析构函数释放）
     * @note 只能调用一次。驱动可能改宽高、stride，以 get_width() 等返回的实际值为准
     */
    bool open_and_start(const char *devicePath, int width, int height);

    /**
     * @brief VIDIOC_DQBUF：取出一块摄像头已经写好画面的 buffer
     * @return 成功返回 buffer 序号（0 ～ kBufCount - 1）；失败返回 -1
     * @note 没有写好的帧时会阻塞等（30 fps 时最多约 33 ms）。取出来的 buffer 用完一定要 queue_buffer() 还回去
     */
    int dequeue_filled_buffer();

    /**
     * @brief VIDIOC_QBUF：把第 bufIndex 块 buffer 还给驱动，让摄像头往里写下一帧
     * @param[in] bufIndex  dequeue_filled_buffer() 返回的序号
     * @return true = 成功；false = ioctl 失败
     * @note ⭐ 所有读这块 buffer 的硬件（编码器、RGA）都读完以后才能还，否则摄像头可能边写边被读，画面撕裂
     */
    bool queue_buffer(int bufIndex);

    /**
     * @brief 取第 bufIndex 块 buffer 对应的 MppBuffer
     * @param[in] bufIndex  0 ～ kBufCount - 1
     * @return MppBuffer，所有权还在本类，调用方不要 mpp_buffer_put
     */
    MppBuffer get_mpp_buffer(int bufIndex) const { return cameraBuffers_[bufIndex].mppBuf; }

    /** @brief 驱动实际给的宽，单位像素 @return 宽 */
    int get_width() const { return width_; }
    /** @brief 驱动实际给的高，单位像素 @return 高 */
    int get_height() const { return height_; }
    /** @brief 一行在内存里占多少字节（驱动的 bytesperline），Day 3 实测 1920 @return 行跨度 */
    int get_hor_stride() const { return horStride_; }
    /** @brief Y 平面在内存里占多少行，由 sizeimage 算出来，Day 3 实测 1080（不是 1088） @return 行数 */
    int get_ver_stride() const { return verStride_; }

private:
    int videoFd_ = -1;      // open("/dev/video22") 得到的 fd，所有 V4L2 ioctl 都发给它
    bool isStreaming_ = false; // STREAMON 成功后为 true，析构时据此决定要不要 STREAMOFF
    CameraBuffer cameraBuffers_[kBufCount];
    int width_ = 0;
    int height_ = 0;
    int horStride_ = 0;
    int verStride_ = 0;
};
```

### 6.5 `src/camera/camera_capture.cpp`

和 Day 3 `main.cpp` 的 `run()` 第 1～5 步、`queue_buffer`、`dequeue_buffer`、`release_resources` 一一对应，V4L2 的细节注释见 Day 3 的代码。

```cpp
//
// M3：摄像头采集。步骤和 Day 3 main.cpp 的 run() 第 1～5 步一样，去掉了 mmap 和拷贝版本
//
#include "camera_capture.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "common/log.h"

static const char *kTag = "Camera";
// 缓冲区类型：多平面视频采集。板子的 rkisp mainpath 只支持多平面类型，填单平面会 EINVAL
static const v4l2_buf_type kBufType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

/**
 * @brief ioctl 的包装：被信号打断（EINTR）时自动重试
 * @param[in]     fd       设备 fd
 * @param[in]     request  命令号，例如 VIDIOC_DQBUF
 * @param[in,out] arg      命令对应的结构体地址
 * @return 和 ioctl 一样：0 = 成功，-1 = 失败（原因在 errno）
 * @note 阻塞在 DQBUF 时收到信号，ioctl 会返回 -1、errno = EINTR，这不是真的失败，重来一次就行
 */
static int xioctl(int fd, unsigned long request, void *arg) {
    int ret;
    do {
        ret = ioctl(fd, request, arg);
    } while (ret == -1 && errno == EINTR);
    return ret;
}

bool CameraCapture::open_and_start(const char *devicePath, int width, int height) {
    // 第 1 步 打开设备。O_RDWR：要读写；O_CLOEXEC：以后 exec 别的程序时自动关掉这个 fd。没加 O_NONBLOCK，所以 DQBUF 会阻塞等
    videoFd_ = open(devicePath, O_RDWR | O_CLOEXEC);
    if (videoFd_ < 0) {
        LOGE(kTag, "open %s: %s", devicePath, strerror(errno));
        return false;
    }

    // 第 2 步 设格式：1 个 plane 的 NV12。S_FMT 是"我想要"，G_FMT 读回"驱动实际给的"
    v4l2_format format = {};
    format.type = kBufType;
    format.fmt.pix_mp.width = width;
    format.fmt.pix_mp.height = height;
    format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12; // NV12：Y 一块，UV 交替一块（见 doc/2_NV12内存排布.md）
    format.fmt.pix_mp.field = V4L2_FIELD_NONE;           // 逐行扫描，不是隔行
    format.fmt.pix_mp.num_planes = 1;                    // Y 和 UV 放在同一块内存里
    if (xioctl(videoFd_, VIDIOC_S_FMT, &format) == -1 || xioctl(videoFd_, VIDIOC_G_FMT, &format) == -1) {
        LOGE(kTag, "VIDIOC_S_FMT / G_FMT: %s", strerror(errno));
        return false;
    }
    width_ = static_cast<int>(format.fmt.pix_mp.width);
    height_ = static_cast<int>(format.fmt.pix_mp.height);
    // bytesperline：一行在内存里占多少字节，就是 horStride
    horStride_ = static_cast<int>(format.fmt.pix_mp.plane_fmt[0].bytesperline);
    // sizeimage：一帧一共多少字节 = horStride × verStride × 1.5，反过来算 verStride。Day 3 实测 3110400 / 1920 × 2 / 3 = 1080
    const uint32_t sizeImageBytes = format.fmt.pix_mp.plane_fmt[0].sizeimage;
    if (format.fmt.pix_mp.pixelformat != V4L2_PIX_FMT_NV12 || format.fmt.pix_mp.num_planes != 1 || horStride_ <= 0) {
        LOGE(kTag, "驱动没有给 1 个 plane 的 NV12");
        return false;
    }
    verStride_ = static_cast<int>(sizeImageBytes / horStride_ * 2 / 3);
    LOGI(kTag, "%s: %dx%d NV12，stride %dx%d，sizeimage %u", devicePath, width_, height_, horStride_, verStride_,
         sizeImageBytes);

    // 第 3 步 申请 4 块 buffer。MMAP 类型：内存由摄像头驱动分配（不是我们自己分配）
    v4l2_requestbuffers requestBufs = {};
    requestBufs.count = kBufCount;
    requestBufs.type = kBufType;
    requestBufs.memory = V4L2_MEMORY_MMAP;
    if (xioctl(videoFd_, VIDIOC_REQBUFS, &requestBufs) == -1 || requestBufs.count != kBufCount) {
        LOGE(kTag, "VIDIOC_REQBUFS: 要 %d 块，给了 %u 块，%s", kBufCount, requestBufs.count, strerror(errno));
        return false;
    }

    // 第 4 步 每块 buffer：查大小 → 导出成 dma_buf fd → 包装成 MppBuffer
    for (int bufIndex = 0; bufIndex < kBufCount; ++bufIndex) {
        // 4.1 QUERYBUF：问驱动第 bufIndex 块有多大（planes[0].length，Day 3 实测 3133440）
        v4l2_plane planes[1] = {};
        v4l2_buffer v4l2Buf = {};
        v4l2Buf.type = kBufType;
        v4l2Buf.memory = V4L2_MEMORY_MMAP;
        v4l2Buf.index = bufIndex;
        v4l2Buf.m.planes = planes; // 多平面类型必须挂上 planes 数组，驱动往里写每个 plane 的信息
        v4l2Buf.length = 1;        // 多平面类型里 length 是 planes 数组有几项，不是字节数
        if (xioctl(videoFd_, VIDIOC_QUERYBUF, &v4l2Buf) == -1) {
            LOGE(kTag, "VIDIOC_QUERYBUF %d: %s", bufIndex, strerror(errno));
            return false;
        }

        // 4.2 EXPBUF：把这块内存导出成 dma_buf fd。不分配新内存，只是给同一块内存一个别的硬件也认的 fd
        v4l2_exportbuffer exportBuf = {};
        exportBuf.type = kBufType; // buffer 类型，必须和 REQBUFS 时一样；内核用它找到对应的队列
        exportBuf.index = bufIndex; // 导出第几块
        exportBuf.plane = 0;        // 导出第几个 plane，NV12 这里只有 1 个
        exportBuf.flags = O_CLOEXEC; // 新 fd 的属性
        if (xioctl(videoFd_, VIDIOC_EXPBUF, &exportBuf) == -1) {
            LOGE(kTag, "VIDIOC_EXPBUF %d: %s", bufIndex, strerror(errno));
            return false;
        }
        cameraBuffers_[bufIndex].dmaFd = exportBuf.fd; // 驱动写回的 fd，析构时 close

        // 4.3 mpp_buffer_import：把 dma_buf fd 包装成 MppBuffer。编码器认 MppBuffer，RGA 通过 mpp_buffer_get_fd 拿回 fd
        MppBufferInfo bufInfo = {};
        bufInfo.type = MPP_BUFFER_TYPE_EXT_DMA; // 外部来的 dma_buf（不是 MPP 自己分配的）
        bufInfo.fd = exportBuf.fd;
        bufInfo.size = planes[0].length; // 整块 buffer 的字节数
        bufInfo.index = bufIndex;
        if (mpp_buffer_import(&cameraBuffers_[bufIndex].mppBuf, &bufInfo) != MPP_OK) {
            LOGE(kTag, "mpp_buffer_import %d 失败", bufIndex);
            return false;
        }
    }

    // 第 5 步 4 块空 buffer 全部交给驱动，再开拍。顺序不能反：开拍时驱动手里没有空 buffer 就没地方写
    for (int bufIndex = 0; bufIndex < kBufCount; ++bufIndex) {
        if (!queue_buffer(bufIndex)) {
            return false;
        }
    }
    v4l2_buf_type bufType = kBufType; // STREAMON 的参数是"启动哪一种队列"，必须传地址
    if (xioctl(videoFd_, VIDIOC_STREAMON, &bufType) == -1) {
        LOGE(kTag, "VIDIOC_STREAMON: %s", strerror(errno));
        return false;
    }
    isStreaming_ = true;
    return true;
}

int CameraCapture::dequeue_filled_buffer() {
    v4l2_plane planes[1] = {};
    v4l2_buffer v4l2Buf = {};
    v4l2Buf.type = kBufType;
    v4l2Buf.memory = V4L2_MEMORY_MMAP;
    v4l2Buf.m.planes = planes;
    v4l2Buf.length = 1;
    // 进去时只填类型；驱动把"哪一块写好了"写回 v4l2Buf.index
    if (xioctl(videoFd_, VIDIOC_DQBUF, &v4l2Buf) == -1) {
        LOGE(kTag, "VIDIOC_DQBUF: %s", strerror(errno));
        return -1;
    }
    if (v4l2Buf.index >= kBufCount) {
        LOGE(kTag, "VIDIOC_DQBUF 返回的序号 %u 越界", v4l2Buf.index);
        return -1;
    }
    return static_cast<int>(v4l2Buf.index);
}

bool CameraCapture::queue_buffer(int bufIndex) {
    v4l2_plane planes[1] = {};
    v4l2_buffer v4l2Buf = {};
    v4l2Buf.type = kBufType;
    v4l2Buf.memory = V4L2_MEMORY_MMAP;
    v4l2Buf.index = bufIndex; // 还哪一块
    v4l2Buf.m.planes = planes;
    v4l2Buf.length = 1;
    if (xioctl(videoFd_, VIDIOC_QBUF, &v4l2Buf) == -1) {
        LOGE(kTag, "VIDIOC_QBUF %d: %s", bufIndex, strerror(errno));
        return false;
    }
    return true;
}

CameraCapture::~CameraCapture() {
    // 先停摄像头，再释放 buffer：停了以后驱动不会再往里写
    if (isStreaming_) {
        v4l2_buf_type bufType = kBufType;
        xioctl(videoFd_, VIDIOC_STREAMOFF, &bufType);
    }
    for (CameraBuffer &cameraBuffer: cameraBuffers_) {
        if (cameraBuffer.mppBuf != nullptr) {
            mpp_buffer_put(cameraBuffer.mppBuf); // 只释放包装，不释放摄像头的内存（内存归驱动管）
        }
        if (cameraBuffer.dmaFd >= 0) {
            close(cameraBuffer.dmaFd);
        }
    }
    if (videoFd_ >= 0) {
        close(videoFd_); // 关掉以后，驱动才真正释放 REQBUFS 申请的 4 块内存
    }
}
```

### 6.6 `src/main.cpp` 里加的东西

**① 开头的 include 加一行**（放在 `#include "rga/rga_resizer.h"` 下面）：

```cpp
#include "camera/camera_capture.h" // M3
```

**② 常量加 5 个**（放在 `kSubHeight` 下面）：

```cpp
// M3：摄像头双码流
static const char *kCameraDevicePath = "/dev/video22"; // ISP0 的 mainpath（Day 3 用 CameraFinder 找到的）
static const char *kCameraMainStreamPath = "/userdata/av/cam_main_1080p.h264";
static const char *kCameraSubStreamPath = "/userdata/av/cam_sub_720p.h264";
static const int kSkipFrameCount = 30;    // 开头丢掉的帧数（等自动曝光稳定，约 1 秒）
static const int kRecordFrameCount = 300; // 录多少帧：30 fps 下 10 秒
```

**③ 新函数**（放在 `run_m2_dual_stream_from_file()` 后面、`main()` 前面）：

```cpp
/**
 * @brief M3 摄像头双码流：摄像头每一帧 → 编码器 #1（1080p 主码流）；同一帧 → RGA 缩成 720p → 编码器 #2（子码流）
 * @return 0 = 成功；1 = 失败（已打印原因）
 * @note 摄像头 buffer 从头到尾 CPU 不碰：编码器 #1 直接读它（Day 3 的零拷贝），RGA 通过它的 dma_buf fd 读。
 *       和 M2 只差 3 处：输入从文件换成摄像头；摄像头的 stride 是 1920 × 1080（不是 1088）；两个编码器都转 270°
 */
static int run_m3_dual_stream_from_camera() {
    LOGI(kTag, "===== M3：摄像头双码流（1080p 主码流 + 720p 子码流）=====");

    // 第 1 步 开摄像头。stride 用驱动给的，不自己算
    CameraCapture camera;
    if (!camera.open_and_start(kCameraDevicePath, kMainWidth, kMainHeight)) {
        return 1;
    }

    // 第 2 步 描述两路画面
    // 主码流：就是摄像头的 buffer，所以宽高、stride 全部用摄像头的；mppBuf 每帧换成 DQBUF 出来的那一块
    Nv12Picture cameraPicture = {.mppBuf = nullptr,
                                 .width = camera.get_width(),
                                 .height = camera.get_height(),
                                 .horStride = camera.get_hor_stride(),
                                 .verStride = camera.get_ver_stride()};
    // 子码流：RGA 的输出，和 M2 一样从 MPP 申请（720 是 16 的倍数，stride 就是 1280 × 720）
    Nv12Picture pictureSub = {.mppBuf = nullptr,
                              .width = kSubWidth,
                              .height = kSubHeight,
                              .horStride = ALIGN(kSubWidth, 16),
                              .verStride = ALIGN(kSubHeight, 16)};

    // 第 3 步 两个编码器的参数。和 M2 比：stride 用摄像头的；rotation = 3（摄像头侧装，Day 3 的设置）
    EncoderConfigArg mainCfg;
    mainCfg.width = cameraPicture.width;
    mainCfg.height = cameraPicture.height;
    mainCfg.horStride = cameraPicture.horStride; // 1920
    mainCfg.verStride = cameraPicture.verStride; // ⭐ 1080：填 0 的话编码器自己按 16 对齐算成 1088，UV 就找错了
    mainCfg.bps = 4 * 1000 * 1000;
    mainCfg.rotation = 3; // 编码前转 270°，码流画面是 1080 × 1920（竖的）

    EncoderConfigArg subCfg;
    subCfg.width = pictureSub.width;
    subCfg.height = pictureSub.height;
    subCfg.horStride = pictureSub.horStride;
    subCfg.verStride = pictureSub.verStride;
    subCfg.bps = 1500 * 1000;
    subCfg.rotation = 3; // RGA 只缩不转；转交给编码器，码流画面是 720 × 1280

    // 第 4 步 申请子码流的 buffer、打开文件、初始化编码器（和 M2 一样）
    MppBufferGroup mppBufferGroup = nullptr;
    if (mpp_buffer_group_get_internal(&mppBufferGroup, MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE) != MPP_OK) {
        LOGE(kTag, "mpp_buffer_group_get_internal 失败");
        return 1;
    }
    FILE *mainStreamFile = fopen(kCameraMainStreamPath, "wb");
    FILE *subStreamFile = fopen(kCameraSubStreamPath, "wb");
    MppEncoder mainEncoder;
    MppEncoder subEncoder;
    int exitCode = 1;

    if (mainStreamFile == nullptr || subStreamFile == nullptr) {
        LOGE(kTag, "打开码流文件失败：%s", strerror(errno));
    } else if (alloc_nv12_picture(mppBufferGroup, pictureSub) &&
               init_encoder_and_write_header(mainCfg, mainStreamFile, mainEncoder) &&
               init_encoder_and_write_header(subCfg, subStreamFile, subEncoder)) {

        // 第 5 步 开头的帧丢掉：摄像头刚开时自动曝光、白平衡还没稳定，画面偏暗偏色（Day 3 的做法）
        bool isAllOk = true;
        for (int frameIndex = 0; frameIndex < kSkipFrameCount && isAllOk; ++frameIndex) {
            const int filledBufIndex = camera.dequeue_filled_buffer();
            isAllOk = filledBufIndex >= 0 && camera.queue_buffer(filledBufIndex);
        }

        // 第 6 步 主循环：取 → 编主码流 → RGA 缩 → 还给摄像头 → 编子码流
        EncodedPacket mainPacket;
        EncodedPacket subPacket;
        size_t mainStreamBytes = 0;
        size_t subStreamBytes = 0;
        int encodedFrameCount = 0;
        const double loopStartMs = get_now_ms();
        while (isAllOk && encodedFrameCount < kRecordFrameCount) {
            // 6.1 取一块摄像头写好的 buffer（阻塞，30 fps 时最多等约 33 ms）
            const int filledBufIndex = camera.dequeue_filled_buffer();
            if (filledBufIndex < 0) {
                isAllOk = false;
                break;
            }
            cameraPicture.mppBuf = camera.get_mpp_buffer(filledBufIndex);

            // 6.2 主码流：编码器直接读摄像头的 buffer（零拷贝）
            isAllOk = mainEncoder.encode(cameraPicture.mppBuf, false, mainPacket) &&
                      write_packet(mainStreamFile, mainPacket);
            // 6.3 子码流的输入：RGA 读摄像头的 buffer，缩好写进 pictureSub
            isAllOk = isAllOk && resize_nv12_by_rga(cameraPicture, pictureSub);

            // 6.4 ⭐ 编码器 #1 和 RGA 都读完了（两个调用都是同步的，返回时硬件已经做完），现在才能还给摄像头。
            //     子码流编码读的是 pictureSub，不是摄像头的 buffer，所以不用等它编完
            //     失败时也要还：少还一块，摄像头就少一块可写的 buffer
            isAllOk = camera.queue_buffer(filledBufIndex) && isAllOk;

            // 6.5 子码流：编 RGA 缩好的那一块
            isAllOk = isAllOk && subEncoder.encode(pictureSub.mppBuf, false, subPacket) &&
                      write_packet(subStreamFile, subPacket);

            mainStreamBytes += mainPacket.data.size();
            subStreamBytes += subPacket.data.size();
            ++encodedFrameCount;
        }
        const double loopMs = get_now_ms() - loopStartMs;

        // 第 7 步 排空两个编码器，打印结果
        if (isAllOk && drain_encoder(mainEncoder, mainStreamFile) && drain_encoder(subEncoder, subStreamFile)) {
            // 实际帧率：摄像头是 30 fps，两路编码 + RGA 跟得上的话这里接近 30；跟不上就会明显低于 30
            LOGI(kTag, "%d 帧，用时 %.2f s，实际 %.1f fps", encodedFrameCount, loopMs / 1000,
                 encodedFrameCount * 1000 / loopMs);
            LOGI(kTag, "主码流（转正后 %dx%d）：%zu 字节 -> %s", mainCfg.height, mainCfg.width, mainStreamBytes,
                 kCameraMainStreamPath);
            LOGI(kTag, "子码流（转正后 %dx%d）：%zu 字节 -> %s", subCfg.height, subCfg.width, subStreamBytes,
                 kCameraSubStreamPath);
            exitCode = 0;
        }
    }

    // 第 8 步 释放。摄像头由 camera 的析构函数释放（函数返回时自动 STREAMOFF、close）
    if (pictureSub.mppBuf != nullptr) {
        mpp_buffer_put(pictureSub.mppBuf);
    }
    for (FILE *file: {mainStreamFile, subStreamFile}) {
        if (file != nullptr) {
            fclose(file);
        }
    }
    mpp_buffer_group_put(mppBufferGroup);
    return exitCode;
}
```

**④ `main()` 里加 m3**：

```cpp
    if (strcmp(experimentName, "m3") == 0) {
        return run_m3_dual_stream_from_camera();
    }

    LOGE(kTag, "用法：%s m1 | m2 | m3", argv[0]);
```

### 6.7 运行和验证

```bash
./run_on_board_with_mac.sh m3
adb pull /userdata/av/cam_main_1080p.h264 .
adb pull /userdata/av/cam_sub_720p.h264 .
ffprobe -v error -show_entries stream=width,height,nb_read_frames -count_frames cam_main_1080p.h264
ffprobe -v error -show_entries stream=width,height,nb_read_frames -count_frames cam_sub_720p.h264
ffplay cam_sub_720p.h264
```

实测输出（2026-11-08）：

```
I Main: ===== M3：摄像头双码流（1080p 主码流 + 720p 子码流）=====
I Camera: /dev/video22: 1920x1080 NV12，stride 1920x1080，sizeimage 3110400
rga_api version 1.10.0_[8]
I Main: 300 帧，用时 10.01 s，实际 30.0 fps
I Main: 主码流（转正后 1080x1920）：4831950 字节 -> /userdata/av/cam_main_1080p.h264
I Main: 子码流（转正后 720x1280）：1788451 字节 -> /userdata/av/cam_sub_720p.h264
```

| | 文件大小 | 实际码率（÷ 10 s） | profile / level | 宽 × 高 | 帧数 | ffmpeg 完整解码 |
|---|---|---|---|---|---|---|
| cam_main_1080p.h264 | 4,831,990 字节 | 3.87 Mbps（设 4） | High / 4.0 | 1080 × 1920 | 300 | 无错 |
| cam_sub_720p.h264 | 1,788,490 字节 | 1.43 Mbps（设 1.5） | High / 3.1 | 720 × 1280 | 300 | 无错 |

（程序打印的字节数比文件少 40 和 39 字节，和 M2 一样，是开头的 SPS/PPS 没算进统计）

验收（2026-11-08 都通过）：
- [x] 两个文件 ffprobe 都是 300 帧，宽高分别是 1080x1920、720x1280；抽第 150 帧看，画面是正的，颜色正常，没有绿边
- [x] 实际帧率 30.0 fps：两路编码 + RGA 跟得上摄像头
- [x] 两个文件内容一样，子码流是主码流的 37%（码率比 1.5 ÷ 4 = 37.5%）

和 M2 比：10 秒比 2 秒长，码率控制有时间收敛，实际码率离设定值更近（M2 主码流只有 3.6 Mbps）。

### 6.8 要回答的问题

1. 摄像头的 buffer，为什么在编码器 #1 和 RGA 都返回以后就能还，不用等编码器 #2？
2. 把 `mainCfg.verStride` 改成 0（让编码器自己按 16 对齐算成 1088），主码流画面会怎样？（和 M1 的绿边是同一个原因，方向相反：这次是编码器以为 UV 在更后面）
3. 为什么 M3 不需要 `mpp_buffer_sync_begin/end`，M2 的 `read_nv12_frame` 却需要？

## 7. 晚上的实验（可选，都和编码有关）

| 实验                 | 改哪里                             | 看什么                                   |
|----------------------|------------------------------------|------------------------------------------|
| 码率对画质的影响     | `subCfg.bps` 改成 0.5、1.5、3 Mbps | 文件大小、ffplay 画质                    |
| GOP 对文件大小的影响 | `subCfg.gop` 改成 30、60、300      | I 帧越少文件越小，但拖动进度条时跳得越慢 |
| H.264 vs H.265       | `subCfg.type`                      | 同码率下画质                             |
| 双码流时 CPU 占用    | 摄像头 M3 时 `top`                 | RGA 和编码都是硬件做的，CPU 应该很低     |

## 8. 容易踩的坑

| 坑                                                     | 现象                           | 怎么办                                                                           |
|--------------------------------------------------------|--------------------------------|----------------------------------------------------------------------------------|
| RGA 的 stride 和 buffer 实际排布不一致                 | 颜色错乱、画面错位             | `wrapbuffer_handle` 的 wstride / hstride 填 MPP 用的那组 hor_stride / ver_stride |
| 缩完没把 `releasebuffer_handle`                        | 跑久了 RGA 报错、导入失败      | 每次 `importbuffer_fd` 都要配一次 `releasebuffer_handle`                         |
| 子码流编码器的宽高、stride 没跟着改                    | 编码器报错，或者子码流画面错乱 | `subCfg` 的宽高、stride 必须和 `subPicture` 一样                                 |
| CPU 读 RGA 刚写完的 buffer 没 sync                     | 读到旧数据（偶发）             | CPU 读前 `mpp_buffer_sync_begin`（M1 写文件时要；M2 只给编码器用，不用）         |
| `#include <rga/im2d.hpp>` 报 `memset was not declared` | 编译失败                       | im2d 头文件的宏用了 memset 却没 include，自己先 `#include <cstring>`             |
