# 具体编码 day4_rga 的流程

> 2026-11-03 写。参考代码已经交叉编译通过（`-Wall -Wextra` 0 warning，最高只用到 GLIBC_2.34）。
> 2026-11-06 更新：M1、M2 已经上板跑通（用户手写的版本在 `src/`），第 4.5、5.2 节的输出换成了实测数据。逐段讲解和更多实测见 [doc/1_理解M1的需求和代码.md](doc/1_理解M1的需求和代码.md)、[doc/1_理解M2的需求和代码.md](doc/1_理解M2的需求和代码.md)。

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

```
day4_rga/
├── CMakeLists.txt
├── run_on_board_with_mac.sh     从 day3_cam2h264 复制，把 BIN_NAME 改成 day4_rga
└── src/
    ├── main.cpp                 M1、M2 都在这里（面条代码，先跑通）
    ├── encoder/mpp_encoder.h/.cpp   从 day3_cam2h264/src/encoder/ 原样复制，不用改
    ├── common/log.h、mpp_utils.h    从 day3_cam2h264/src/common/ 原样复制
    └── rga/rga_resizer.h/.cpp   新写：用 RGA 缩放一帧 NV12（第 4 节）
```

现在 `day4_rga/` 里的 `main.cpp`、`CMakeLists.txt` 是 CLion 生成的模板，整个替换掉。

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

顺带的对照实验：子码流码率设成和主码流一样的 4 Mbps 时，子码流 859,490 字节（主码流的 96%），几乎一样大。**分辨率降了、码率不降，子码流就白做了**。详见 [doc/1_理解M2的需求和代码.md](doc/1_理解M2的需求和代码.md) 第 3.6 节

### 5.3 要回答的问题

1. 为什么两路码流要各自写 SPS/PPS？把子码流文件开头的 SPS/PPS 去掉，还能播吗？（SPS 里有宽高、profile，解码器没有它不知道怎么解）
2. 子码流的码率从 1.5 Mbps 改成 0.5 Mbps，文件变多小？ffplay 看画质差多少？
3. 子码流改成 H.265（`subCfg.type = MPP_VIDEO_CodingHEVC`），同样码率，画质和 H.264 比怎么样？

## 6. M3：摄像头双码流（摄像头装回后再做）

和 M2 只差输入：

|        | M2                                  | M3                                                                                                   |
|--------|-------------------------------------|------------------------------------------------------------------------------------------------------|
| 输入   | 每帧从文件读进 `mainPicture.mppBuf` | 摄像头 DQBUF 出来的那块 buffer（Day 3 已经 `mpp_buffer_import` 成 MppBuffer）                        |
| 主码流 | 编 `mainPicture.mppBuf`             | 直接编摄像头的 MppBuffer（Day 3 的零拷贝）                                                           |
| 子码流 | RGA 从 `mainPicture` 缩             | RGA 从摄像头的 MppBuffer 缩，`Nv12Picture` 里填摄像头的 stride（1920 × 1080，不是 1088，Day 3 踩过） |
| 旋转   | 不用                                | 两个编码器都设 `cfg.rotation = 3`；子码流转正后是 720 × 1280                                         |

做法：把 Day 3 的 `main.cpp` 复制过来，在"编码一帧"的地方，按 M2 的写法加上 RGA 缩放和第二个编码器。做的时候再细写。

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
