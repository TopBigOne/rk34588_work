# 具体写 day2_h2642yuv 的流程

> 和 Day 1 一样：**先全部写在 `main.cpp` 的 `main()` 里**（面条代码），从上往下照顺序写，M4 跑通后再整理成 `MppDecoder` 类。
> 每个里程碑都有：要做什么 → 完整代码（照着敲）→ 怎么验证 → 易错点。
> 下面的代码都在 Mac 上用 RK3588 交叉工具链**编译通过**了。⚠️ 板子上的实测结果（`buf_size` 的具体值、fps）等板子连上后补上。

---

## 0. 先放下心理负担

`mpi_dec_test.c` 695 行，但我们要写的只有大约 **250 行**（含参数解析和统计），核心循环不到 80 行。比 Day 1 简单的地方：

- 解码几乎不用配参数（只有 `split_parse` 一个），宽高从码流里读
- Day 1 的 CHECK 宏、goto CLEANUP、fread/fwrite、stride 全部会用

比 Day 1 难的只有一个：**送和取不是一一对应的**。编码是送一帧取一包；解码是送一块（64KB，可能半帧也可能三帧）、取 0～N 帧，第一次取到的还不是图像（info change）。所以主循环要写成"送一包 → 把能取的都取完 → 回去送下一包"。

---

## 1. 整体调用流程

```
初始化（M1）
  mpp_create → mpp_init(MPP_CTX_DEC, AVC)
  mpp_dec_cfg_init → GET_CFG → set "base:split_parse" = 1 → SET_CFG

主循环（M2～M4）
  while (没拿到 EOS 帧 && 帧数没到上限) {
      ① 上一包送完了、文件没读完 → fread 64KB → mpp_packet_init（最后一块 set_eos）
      ② decode_put_packet
           MPP_OK              → mpp_packet_deinit（MPP 已经拷走数据）
           MPP_ERR_BUFFER_FULL → 什么都不做，下一轮重送同一包
      ③ while (decode_get_frame 拿到了帧) {
             info change → 建内存池（buf_size × 24）→ SET_EXT_BUF_GROUP → SET_INFO_CHANGE_READY
             errinfo/discard → 跳过
             正常图像 → 逐行写 width × height
             记下 eos → mpp_frame_deinit
         }
      ④ 包没送进去，或者 EOS 送了还没拿到最后一帧 → usleep(1ms)
  }

收尾
  frame_deinit → packet_deinit → dec_cfg_deinit → mpp_destroy → buffer_group_put → fclose
```

---

## 2. 准备工作

### 2.1 头文件

```cpp
#include <cstdint>            // uint8_t
#include <cstdio>             // fopen / fread / fwrite / printf
#include <rockchip/rk_mpi.h>  // MPP 全部接口（解码的 MppDecCfg 也在里面）
#include <unistd.h>           // usleep
#include <vector>             // 读缓冲区
```

### 2.2 CHECK 宏

和 Day 1 一模一样（M1 代码里有）。⚠️ 还是那条 C++ 规则：**`goto` 不能跳过带初始化的变量声明**，所以 `main()` 里的变量都在第一个 `CHECK` 之前声明。

### 2.3 CMake 和脚本

已经配好：[CMakeLists.txt](CMakeLists.txt) 链接了 `rockchip_mpp`、加了 rpath。CLion 要新建 `RK3588-Debug` profile，见 [guide/01](guide/01_环境准备.md) 第 1 节。

### 2.4 测试素材

```bash
./run_move_file_to_board_via_usb.sh     # aaa.264 → /userdata/av/
```

---

## 3. M1：解码器初始化（约 40 行）

### 步骤

| # | 调用 | 和编码比 |
|---|---|---|
| 1 | `mpp_create(&decoderCtx, &decoderApi)` | 一样 |
| 2 | `mpp_init(decoderCtx, MPP_CTX_DEC, MPP_VIDEO_CodingAVC)` | `MPP_CTX_ENC` → **`MPP_CTX_DEC`** |
| 3 | `mpp_dec_cfg_init(&decoderCfg)` | `mpp_enc_cfg_init` → **`mpp_dec_cfg_init`** |
| 4 | `control(MPP_DEC_GET_CFG, decoderCfg)` | `MPP_ENC_GET_CFG` → `MPP_DEC_GET_CFG` |
| 5 | `mpp_dec_cfg_set_u32(decoderCfg, "base:split_parse", 1)` | 编码设一堆，解码只设这一个；注意是 **`set_u32`**，不是 `set_s32` |
| 6 | `control(MPP_DEC_SET_CFG, decoderCfg)` | 一样 |

### 完整代码（替换 `main.cpp`）

```cpp
#include <cstdio>
#include <rockchip/rk_mpi.h>

// 调用失败就打印是哪一句，然后跳到 CLEANUP 统一释放（和 Day 1 一样）
#define CHECK(expr)                                                            \
    do {                                                                       \
        MPP_RET _r = (expr);                                                   \
        if (_r != MPP_OK) {                                                    \
            printf("[FAIL] %s ret=%d (line %d)\n", #expr, (int) _r, __LINE__); \
            goto CLEANUP;                                                      \
        }                                                                      \
    } while (0)

int main() {
    MppCtx decoderCtx    = nullptr; // 一个解码器实例
    MppApi* decoderApi   = nullptr; // 函数表：decode_put_packet / decode_get_frame / control ...
    MppDecCfg decoderCfg = nullptr; // 解码参数（只用来设 split_parse）
    int exitCode         = -1;

    // 1. 创建实例，初始化成 H.264 解码器
    CHECK(mpp_create(&decoderCtx, &decoderApi));
    CHECK(mpp_init(decoderCtx, MPP_CTX_DEC, MPP_VIDEO_CodingAVC)); // ★ 和编码的区别：MPP_CTX_DEC

    // 2. 打开内部分帧：我们按固定长度读文件，让 MPP 自己切成一帧一帧
    CHECK(mpp_dec_cfg_init(&decoderCfg));
    CHECK(decoderApi->control(decoderCtx, MPP_DEC_GET_CFG, decoderCfg));
    CHECK(mpp_dec_cfg_set_u32(decoderCfg, "base:split_parse", 1));
    CHECK(decoderApi->control(decoderCtx, MPP_DEC_SET_CFG, decoderCfg));

    printf("|            decoder ready\n");
    exitCode = 0;

CLEANUP:
    if (decoderCfg) {
        mpp_dec_cfg_deinit(decoderCfg);
    }
    if (decoderCtx) {
        mpp_destroy(decoderCtx);
    }
    return exitCode;
}
```

### 怎么验证

```bash
./run_on_board_with_mac.sh
```

预期：打印 `|            decoder ready`，没有 `[FAIL]`，退出码 0。

### 易错点

- `mpp_init` 第 2 个参数写成 `MPP_CTX_ENC`（从 Day 1 复制过来忘了改）→ 后面 `MPP_DEC_GET_CFG` 失败。
- `split_parse` 用了 `mpp_dec_cfg_set_s32` → 类型不匹配，返回失败（这个 key 是 u32）。

---

## 4. M2：送码流，拿到 info change（约 70 行）

### 步骤

1. 打开 `aaa.264`，准备一个 64KB 的读缓冲区（`std::vector<uint8_t>`）。
2. 循环：
   - 上一包已经送进去了（`inputPacket == nullptr`）→ `fread` 64KB → `mpp_packet_init(&inputPacket, chunk.data(), readSize)`
   - `decode_put_packet`：成功就 `mpp_packet_deinit`（MPP 已经拷走了数据）；队列满就保留，下一轮重送
   - `decode_get_frame`：**非阻塞**，没有帧时返回 `MPP_OK` + `nullptr`
   - 拿到的帧 `mpp_frame_get_info_change()` 为真 → 打印宽高、stride、`buf_size`，建内存池，交给解码器，告诉它继续
   - 不管拿到什么帧，都要 `mpp_frame_deinit`
3. 拿到 info change 就退出（M2 只验证到这一步）。

### 内存池三步

```cpp
CHECK(mpp_buffer_group_get_internal(&frameGroup, MPP_BUFFER_TYPE_DRM));          // 建一个内部池
CHECK(mpp_buffer_group_limit_config(frameGroup, bufSize, 24));                   // 限制：每块 bufSize，最多 24 块
CHECK(decoderApi->control(decoderCtx, MPP_DEC_SET_EXT_BUF_GROUP, frameGroup));   // 交给解码器
CHECK(decoderApi->control(decoderCtx, MPP_DEC_SET_INFO_CHANGE_READY, nullptr));  // 告诉它：好了，继续解码
```

这就是官方文档的"半内部分配模式"：池子是我们建的、限了量，但每块内存由解码器自己从池里申请（见 [guide/02](guide/02_读源码.md) 第 3 节）。

### 完整代码（在 M1 基础上改，`★ M2` 标出的是新增部分）

```cpp
#include <cstdint>
#include <cstdio>
#include <rockchip/rk_mpi.h>
#include <unistd.h> // ★ M2：usleep
#include <vector>   // ★ M2：读缓冲区

#define CHECK(expr)                                                            \
    do {                                                                       \
        MPP_RET _r = (expr);                                                   \
        if (_r != MPP_OK) {                                                    \
            printf("[FAIL] %s ret=%d (line %d)\n", #expr, (int) _r, __LINE__); \
            goto CLEANUP;                                                      \
        }                                                                      \
    } while (0)

int main() {
    const char* streamInputPath = "/userdata/av/aaa.264"; // ★ M2
    const size_t chunkSize      = 64 * 1024;              // ★ M2：每次读 64KB
    std::vector<uint8_t> chunk(chunkSize);                // ★ M2

    MppCtx decoderCtx         = nullptr;
    MppApi* decoderApi        = nullptr;
    MppDecCfg decoderCfg      = nullptr;
    MppBufferGroup frameGroup = nullptr; // ★ M2：给解码器用的内存池（info change 时建）
    MppPacket inputPacket     = nullptr; // ★ M2：装一块码流
    MppFrame outputFrame      = nullptr; // ★ M2：解码器吐出来的帧（第一次是 info change）
    FILE* streamInputFile     = nullptr; // ★ M2
    bool gotInfoChange        = false;   // ★ M2
    size_t readSize           = 0;       // ★ M2
    int exitCode              = -1;

    CHECK(mpp_create(&decoderCtx, &decoderApi));
    CHECK(mpp_init(decoderCtx, MPP_CTX_DEC, MPP_VIDEO_CodingAVC));
    CHECK(mpp_dec_cfg_init(&decoderCfg));
    CHECK(decoderApi->control(decoderCtx, MPP_DEC_GET_CFG, decoderCfg));
    CHECK(mpp_dec_cfg_set_u32(decoderCfg, "base:split_parse", 1));
    CHECK(decoderApi->control(decoderCtx, MPP_DEC_SET_CFG, decoderCfg));
    printf("|            decoder ready\n");

    // ★ M2：打开码流文件
    streamInputFile = fopen(streamInputPath, "rb");
    if (!streamInputFile) {
        printf("open %s failed\n", streamInputPath);
        goto CLEANUP;
    }

    // ★ M2：一块一块送，直到拿到 info change
    while (!gotInfoChange) {
        // 1. 读一块，包成 packet（上一块送进去了才读新的）
        if (!inputPacket) {
            readSize = fread(chunk.data(), 1, chunkSize, streamInputFile);
            if (readSize == 0) {
                printf("文件读完了还没等到 info change\n");
                goto CLEANUP;
            }
            CHECK(mpp_packet_init(&inputPacket, chunk.data(), readSize));
        }

        // 2. 送进去。队列满（MPP_ERR_BUFFER_FULL）就保留这包，下一轮重送
        if (decoderApi->decode_put_packet(decoderCtx, inputPacket) == MPP_OK) {
            mpp_packet_deinit(&inputPacket); // MPP 已经拷走了数据，可以读下一块了
        }

        // 3. 取一帧看看（非阻塞：没有就是 nullptr）
        CHECK(decoderApi->decode_get_frame(decoderCtx, &outputFrame));
        if (outputFrame && mpp_frame_get_info_change(outputFrame)) {
            const RK_U32 width     = mpp_frame_get_width(outputFrame);
            const RK_U32 height    = mpp_frame_get_height(outputFrame);
            const RK_U32 horStride = mpp_frame_get_hor_stride(outputFrame);
            const RK_U32 verStride = mpp_frame_get_ver_stride(outputFrame);
            const size_t bufSize   = mpp_frame_get_buf_size(outputFrame);
            printf("|            info change: %ux%u stride %ux%u buf_size %zu\n", width, height, horStride, verStride,
                bufSize);

            // 建内存池：最多 24 块，每块 bufSize 字节（半内部分配模式）
            CHECK(mpp_buffer_group_get_internal(&frameGroup, MPP_BUFFER_TYPE_DRM));
            CHECK(mpp_buffer_group_limit_config(frameGroup, bufSize, 24));
            CHECK(decoderApi->control(decoderCtx, MPP_DEC_SET_EXT_BUF_GROUP, frameGroup));
            CHECK(decoderApi->control(decoderCtx, MPP_DEC_SET_INFO_CHANGE_READY, nullptr));
            gotInfoChange = true;
        }
        if (outputFrame) {
            mpp_frame_deinit(&outputFrame); // info change 帧也要释放
        }
        if (!gotInfoChange) {
            usleep(1000); // 解码器还在解析，等 1ms
        }
    }
    exitCode = 0;

CLEANUP:
    if (outputFrame) {
        mpp_frame_deinit(&outputFrame);
    }
    if (inputPacket) {
        mpp_packet_deinit(&inputPacket);
    }
    if (decoderCfg) {
        mpp_dec_cfg_deinit(decoderCfg);
    }
    if (decoderCtx) {
        mpp_destroy(decoderCtx);
    }
    if (frameGroup) {
        mpp_buffer_group_put(frameGroup); // 内存池在解码器销毁之后再释放
    }
    if (streamInputFile) {
        fclose(streamInputFile);
    }
    return exitCode;
}
```

### 怎么验证

```bash
./run_move_file_to_board_via_usb.sh     # 第一次要先传素材
./run_on_board_with_mac.sh
```

预期：

```
|            decoder ready
|            info change: 1920x1080 stride 1920x1088 buf_size ???????
```

- `1920x1080 stride 1920x1088` 是验收标准之一。
- `buf_size` 的实际值记下来，和 `1920 × 1088 × 3 / 2 = 3,133,440` 比一比，看大了多少（板子实测后补到这里）。

### 易错点

- **只 `put` 不 `get`**：送了几包之后队列满，`put_packet` 一直返回 `MPP_ERR_BUFFER_FULL`，死循环。解码器要等你把 info change 取走才继续。
- **拿到 info change 不调 `SET_INFO_CHANGE_READY`**：解码器一直暂停，后面永远拿不到图像。
- **`put_packet` 失败就读下一块**：丢了一包，后面画面花屏或者解码出错。
- **内存池在 `mpp_destroy` 之前 put**：解码器还在用池子里的内存，可能崩溃。顺序是先 `mpp_destroy`，再 `mpp_buffer_group_put`（和 demo 的 `MPP_TEST_OUT` 一致）。

---

## 5. M3：拿到第 1 帧图像，写成 NV12（约 40 行）

### 步骤

在 M2 的基础上：

1. 拿到 info change 之后**不退出**，继续送、继续取。
2. 拿到的帧不是 info change → 检查 `errinfo` / `discard`，都为 0 才是好图像。
3. 好图像 → 按 `width × height` 逐行写到 `/userdata/av/out.nv12`，写完第 1 帧就退出。

### 逐行写一帧（核心代码）

```cpp
// 按 width × height 写出一帧 NV12（跳过 stride 填充），返回是否写成功
static bool write_nv12_frame(FILE* fp, MppFrame frame) {
    const RK_U32 width     = mpp_frame_get_width(frame);       // 1920
    const RK_U32 height    = mpp_frame_get_height(frame);      // 1080
    const RK_U32 horStride = mpp_frame_get_hor_stride(frame);  // 1920
    const RK_U32 verStride = mpp_frame_get_ver_stride(frame);  // 1088
    MppBuffer buffer       = mpp_frame_get_buffer(frame);
    if (!buffer) {
        return false;
    }
    const auto* base = static_cast<const uint8_t*>(mpp_buffer_get_ptr(buffer));
    for (RK_U32 row = 0; row < height; row++) {                       // Y：1080 行
        if (fwrite(base + row * horStride, 1, width, fp) != width) {
            return false;
        }
    }
    const uint8_t* uv = base + horStride * verStride;                 // UV 从第 1088 行开始
    for (RK_U32 row = 0; row < height / 2; row++) {                   // UV：540 行
        if (fwrite(uv + row * horStride, 1, width, fp) != width) {
            return false;
        }
    }
    return true;
}
```

和 Day 1 的 `ReadYUV::read_nv12_rows` 正好反过来：Day 1 是"文件连续 → 缓冲区按 stride"，今天是"缓冲区按 stride → 文件连续"。

不用调 `mpp_buffer_sync_*`：`decode_get_frame` 内部已经调了 `mpp_buffer_sync_ro_begin`（见 [guide/02](guide/02_读源码.md) 第 6.3 节）。

### 循环里取帧那部分改成

```cpp
if (mpp_frame_get_info_change(outputFrame)) {
    ...（M2 的内存池代码，去掉 gotInfoChange = true）
} else {
    const RK_U32 errInfo = mpp_frame_get_errinfo(outputFrame);
    const RK_U32 discard = mpp_frame_get_discard(outputFrame);
    if (!errInfo && !discard && mpp_frame_get_buffer(outputFrame)) {
        if (!write_nv12_frame(yuvOutputFile, outputFrame)) {
            perror("fwrite frame");
            goto CLEANUP;
        }
        gotFirstFrame = true;     // 循环条件改成 while (!gotFirstFrame)
    }
}
mpp_frame_deinit(&outputFrame);
```

另外别忘了：`fopen` 输出文件（`"wb"`）、`CLEANUP` 里 `fclose`、`usleep` 的条件改成 `!gotFirstFrame`。

### 怎么验证

```bash
./run_on_board_with_mac.sh
adb shell "ls -l /userdata/av/out.nv12"          # 应该正好 3110400 字节
adb pull /userdata/av/out.nv12 .
/usr/local/ffmpeg/4.4/bin/ffplay -f rawvideo -pixel_format nv12 -video_size 1920x1080 out.nv12
```

- 文件大小**正好 3,110,400**（= 1920 × 1080 × 3/2）。如果是 3,133,440，说明按 stride 整块写了。
- ffplay 里画面正常、颜色正常。

---

## 6. M4：解完整个文件（或前 N 帧），处理 EOS（约 30 行改动）

### 步骤

1. 文件读不满 64KB → 这是最后一块 → `mpp_packet_set_eos(inputPacket)`。
2. 循环条件改成：**没拿到 EOS 帧** 并且 **帧数没到上限**。
3. 取帧改成内层循环：**把现在能拿的帧全拿完**（拿不到了再回去送下一包）。
4. 每帧都检查 `mpp_frame_get_eos()`。
5. 什么时候 `usleep(1000)`：这包没送进去（队列满），或者 EOS 已经送了但最后一帧还没出来。
6. 帧数上限先写死 60（全部解完 5.6GB）。
7. 内存池：info change 可能不止一次（码流中途换分辨率），第二次时池子已经有了，要先 `mpp_buffer_group_clear` 再重新 `limit_config`。

### 两个 EOS（和 Day 1 一样的道理）

| | 谁说的 | 意思 |
|---|---|---|
| `inputEos` | 我们 → 解码器 | 文件读完了，最后一包带 EOS |
| `outputEos` | 解码器 → 我们 | 最后一帧也给你了 |

循环结束条件是 **`outputEos`**：送完最后一包时，解码器手里还有好几帧没吐出来（参考帧、还在硬件里的帧）。

### 完整代码（在 M3 基础上改）

```cpp
#include <cstdint>
#include <cstdio>
#include <rockchip/rk_mpi.h>
#include <unistd.h> // usleep
#include <vector>

#define CHECK(expr)                                                            \
    do {                                                                       \
        MPP_RET _r = (expr);                                                   \
        if (_r != MPP_OK) {                                                    \
            printf("[FAIL] %s ret=%d (line %d)\n", #expr, (int) _r, __LINE__); \
            goto CLEANUP;                                                      \
        }                                                                      \
    } while (0)

// 按 width × height 写出一帧 NV12（跳过 stride 填充），返回是否写成功
static bool write_nv12_frame(FILE* fp, MppFrame frame) {
    const RK_U32 width     = mpp_frame_get_width(frame);
    const RK_U32 height    = mpp_frame_get_height(frame);
    const RK_U32 horStride = mpp_frame_get_hor_stride(frame);
    const RK_U32 verStride = mpp_frame_get_ver_stride(frame);
    MppBuffer buffer       = mpp_frame_get_buffer(frame);
    if (!buffer) {
        return false;
    }
    const auto* base = static_cast<const uint8_t*>(mpp_buffer_get_ptr(buffer));
    for (RK_U32 row = 0; row < height; row++) {
        if (fwrite(base + row * horStride, 1, width, fp) != width) {
            return false;
        }
    }
    const uint8_t* uv = base + horStride * verStride;
    for (RK_U32 row = 0; row < height / 2; row++) {
        if (fwrite(uv + row * horStride, 1, width, fp) != width) {
            return false;
        }
    }
    return true;
}

int main() {
    const char* streamInputPath = "/userdata/av/aaa.264";
    const char* yuvOutputPath   = "/userdata/av/out.nv12";
    const int maxFrames         = 60; // 全部解完是 5.6GB，先只写前 60 帧（187MB）
    const size_t chunkSize = 64 * 1024;
    std::vector<uint8_t> chunk(chunkSize);

    MppCtx decoderCtx            = nullptr;
    MppApi* decoderApi           = nullptr;
    MppDecCfg decoderCfg         = nullptr;
    MppBufferGroup frameGroup    = nullptr;
    MppPacket inputPacket        = nullptr;
    MppFrame outputFrame         = nullptr;
    FILE* streamInputFile        = nullptr;
    FILE* yuvOutputFile          = nullptr;
    bool inputEos                = false; // 文件读完了，最后一包带了 EOS
    bool outputEos               = false; // 解码器吐出了带 EOS 的帧
    bool reachedLimit            = false; // maxFrames 帧写够了
    size_t readSize              = 0;
    RK_S32 decodedFrameCount     = 0;
    int exitCode                 = -1;

    // ==================== M1：初始化解码器 ====================
    CHECK(mpp_create(&decoderCtx, &decoderApi));
    CHECK(mpp_init(decoderCtx, MPP_CTX_DEC, MPP_VIDEO_CodingAVC));
    CHECK(mpp_dec_cfg_init(&decoderCfg));
    CHECK(decoderApi->control(decoderCtx, MPP_DEC_GET_CFG, decoderCfg));
    CHECK(mpp_dec_cfg_set_u32(decoderCfg, "base:split_parse", 1)); // 内部分帧：我们按 64KB 读，不按帧切
    CHECK(decoderApi->control(decoderCtx, MPP_DEC_SET_CFG, decoderCfg));
    printf("|            decoder ready\n");

    streamInputFile = fopen(streamInputPath, "rb");
    if (!streamInputFile) {
        printf("open %s failed\n", streamInputPath);
        goto CLEANUP;
    }
    yuvOutputFile = fopen(yuvOutputPath, "wb");
    if (!yuvOutputFile) {
        printf("open %s failed\n", yuvOutputPath);
        goto CLEANUP;
    }

    // ==================== M2～M4：送码流、取图像 ====================
    while (!outputEos && !reachedLimit) {
        // 1. 读一块码流，包成 packet（上一块送进去了、文件还没读完，才读新的）
        if (!inputPacket && !inputEos) {
            readSize = fread(chunk.data(), 1, chunkSize, streamInputFile);
            inputEos = readSize < chunkSize; // 读不满 = 文件读完了
            CHECK(mpp_packet_init(&inputPacket, chunk.data(), readSize));
            if (inputEos) {
                mpp_packet_set_eos(inputPacket);
                printf("              input end, send EOS\n");
            }
        }

        // 2. 送进去：队列满了返回 MPP_ERR_BUFFER_FULL，这包留着下一轮重送
        if (inputPacket) {
            MPP_RET ret = decoderApi->decode_put_packet(decoderCtx, inputPacket);
            if (ret == MPP_OK) {
                mpp_packet_deinit(&inputPacket); // MPP 已经拷走了数据，inputPacket 变回 nullptr
            } else if (ret == MPP_ERR_BUFFER_FULL) {
                // 队列满：inputPacket 留着，下一轮重送同一包
            } else {
                printf("[FAIL] decode_put_packet ret=%d\n", ret);
                goto CLEANUP;
            }
        }

        // 3. 把现在能拿的图像都拿出来（非阻塞：没有就返回 MPP_OK + nullptr）
        while (true) {
            CHECK(decoderApi->decode_get_frame(decoderCtx, &outputFrame));
            if (!outputFrame) {
                break;
            }
            if (mpp_frame_get_info_change(outputFrame)) {
                const RK_U32 width     = mpp_frame_get_width(outputFrame);
                const RK_U32 height    = mpp_frame_get_height(outputFrame);
                const RK_U32 horStride = mpp_frame_get_hor_stride(outputFrame);
                const RK_U32 verStride = mpp_frame_get_ver_stride(outputFrame);
                const size_t bufSize   = mpp_frame_get_buf_size(outputFrame);
                printf("|            info change: %ux%u stride %ux%u buf_size %zu\n", width, height, horStride,
                    verStride, bufSize);
                if (!frameGroup) {
                    CHECK(mpp_buffer_group_get_internal(&frameGroup, MPP_BUFFER_TYPE_DRM));
                } else {
                    CHECK(mpp_buffer_group_clear(frameGroup));
                }
                CHECK(mpp_buffer_group_limit_config(frameGroup, bufSize, 24));
                CHECK(decoderApi->control(decoderCtx, MPP_DEC_SET_EXT_BUF_GROUP, frameGroup));
                CHECK(decoderApi->control(decoderCtx, MPP_DEC_SET_INFO_CHANGE_READY, nullptr));
            } else {
                const RK_U32 errInfo = mpp_frame_get_errinfo(outputFrame);
                const RK_U32 discard = mpp_frame_get_discard(outputFrame);
                if (errInfo || discard) {
                    printf("              frame %-4d err %x discard %x, skip\n", decodedFrameCount, errInfo, discard);
                } else if (mpp_frame_get_buffer(outputFrame)) {
                    if (!write_nv12_frame(yuvOutputFile, outputFrame)) {
                        perror("fwrite frame");
                        goto CLEANUP;
                    }
                    decodedFrameCount++;
                    if (decodedFrameCount >= maxFrames) {
                        reachedLimit = true;
                    }
                }
            }
            outputEos = mpp_frame_get_eos(outputFrame);
            mpp_frame_deinit(&outputFrame);
            if (outputEos || reachedLimit) {
                break;
            }
        }

        // 4. 这一包没送进去（队列满），或者 EOS 已经送了但还没拿到最后一帧：等 1ms 再试
        if (inputPacket || (inputEos && !outputEos && !reachedLimit)) {
            usleep(1000);
        }
    }
    printf("decoded %d frames → %s\n", decodedFrameCount, yuvOutputPath);
    exitCode = 0;

CLEANUP:
    if (outputFrame) {
        mpp_frame_deinit(&outputFrame);
    }
    if (inputPacket) {
        mpp_packet_deinit(&inputPacket);
    }
    if (decoderCfg) {
        mpp_dec_cfg_deinit(decoderCfg);
    }
    if (decoderCtx) {
        mpp_destroy(decoderCtx);
    }
    if (frameGroup) {
        mpp_buffer_group_put(frameGroup);
    }
    if (yuvOutputFile) {
        fclose(yuvOutputFile);
    }
    if (streamInputFile) {
        fclose(streamInputFile);
    }
    return exitCode;
}
```

### 怎么验证

```bash
./run_on_board_with_mac.sh
adb shell "ls -l /userdata/av/out.nv12"          # 60 × 3110400 = 186,624,000 字节
adb pull /userdata/av/out.nv12 .
/usr/local/ffmpeg/4.4/bin/ffplay -f rawvideo -pixel_format nv12 -video_size 1920x1080 -framerate 30 out.nv12
```

- 文件大小 = 186,624,000 字节（60 帧）
- ffplay 能播 2 秒，画面连续、不花
- 能不能走到 EOS 正常退出，等 M5 加了"不写文件"参数之后验证（全部写文件会写出 5.6GB）

### 易错点

- 循环条件用了 `inputEos` → 最后几帧丢了（1803 帧只解出 1790 多帧）。
- EOS 包送进去后没 `usleep` 就空转 → CPU 占满（不影响结果，但不好）。
- 只取一帧就回去送 → 帧在输出队列里越积越多，解码器内存池被占满，卡住。

---

## 7. M5 + M6：命令行参数、统计（可选，跑通后再做）

| 参数 | 含义 | 默认 |
|---|---|---|
| `-i` | 输入码流 | `/userdata/av/aaa.264` |
| `-o` | 输出 NV12；**不给就不写文件**（测纯解码速度） | 不写 |
| `-t` | `h264` / `h265` | `h264` |
| `-n` | 最多解多少帧，0 = 全部 | 0 |

统计：解码帧数、错误/丢弃帧数、info change 次数、用时、fps、`put_packet` 因为队列满重试的次数。

### M5 + M6 合起来的完整代码（在 M4 基础上改）

```cpp
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <rockchip/rk_mpi.h>
#include <unistd.h> // usleep
#include <vector>

#define CHECK(expr)                                                            \
    do {                                                                       \
        MPP_RET _r = (expr);                                                   \
        if (_r != MPP_OK) {                                                    \
            printf("[FAIL] %s ret=%d (line %d)\n", #expr, (int) _r, __LINE__); \
            goto CLEANUP;                                                      \
        }                                                                      \
    } while (0)

struct Args {
    const char* streamInputPath = "/userdata/av/aaa.264";
    const char* yuvOutputPath   = nullptr; // nullptr = 不写文件
    MppCodingType type          = MPP_VIDEO_CodingAVC;
    int maxFrames               = 0; // 0 = 全部
};

static bool parse_args(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; i += 2) {
        const char* opt = argv[i];
        const char* val = (i + 1 < argc) ? argv[i + 1] : nullptr;
        if (!val) {
            printf("参数 %s 缺少值\n", opt);
            return false;
        }
        if (!strcmp(opt, "-i")) {
            a.streamInputPath = val;
        } else if (!strcmp(opt, "-o")) {
            a.yuvOutputPath = val;
        } else if (!strcmp(opt, "-n")) {
            a.maxFrames = atoi(val);
        } else if (!strcmp(opt, "-t")) {
            if (!strcmp(val, "h264")) {
                a.type = MPP_VIDEO_CodingAVC;
            } else if (!strcmp(val, "h265")) {
                a.type = MPP_VIDEO_CodingHEVC;
            } else {
                printf("-t 只支持 h264 / h265\n");
                return false;
            }
        } else {
            printf("未知参数 %s\n", opt);
            return false;
        }
    }
    if (a.maxFrames < 0) {
        printf("-n 不能是负数\n");
        return false;
    }
    return true;
}

// 按 width × height 写出一帧 NV12（跳过 stride 填充），返回是否写成功
static bool write_nv12_frame(FILE* fp, MppFrame frame) {
    const RK_U32 width     = mpp_frame_get_width(frame);
    const RK_U32 height    = mpp_frame_get_height(frame);
    const RK_U32 horStride = mpp_frame_get_hor_stride(frame);
    const RK_U32 verStride = mpp_frame_get_ver_stride(frame);
    MppBuffer buffer       = mpp_frame_get_buffer(frame);
    if (!buffer) {
        return false;
    }
    const auto* base = static_cast<const uint8_t*>(mpp_buffer_get_ptr(buffer));
    for (RK_U32 row = 0; row < height; row++) {
        if (fwrite(base + row * horStride, 1, width, fp) != width) {
            return false;
        }
    }
    const uint8_t* uv = base + horStride * verStride;
    for (RK_U32 row = 0; row < height / 2; row++) {
        if (fwrite(uv + row * horStride, 1, width, fp) != width) {
            return false;
        }
    }
    return true;
}

int main(int argc, char** argv) {
    Args a;
    if (!parse_args(argc, argv, a)) {
        printf("usage: %s -i in.h264 [-o out.nv12] [-t h264|h265] [-n 帧数]\n", argv[0]);
        return -1;
    }

    const size_t chunkSize = 64 * 1024;
    std::vector<uint8_t> chunk(chunkSize);

    MppCtx decoderCtx            = nullptr;
    MppApi* decoderApi           = nullptr;
    MppDecCfg decoderCfg         = nullptr;
    MppBufferGroup frameGroup    = nullptr;
    MppPacket inputPacket        = nullptr;
    MppFrame outputFrame         = nullptr;
    FILE* streamInputFile        = nullptr;
    FILE* yuvOutputFile          = nullptr;
    bool inputEos                = false; // 文件读完了，最后一包带了 EOS
    bool outputEos               = false; // 解码器吐出了带 EOS 的帧
    bool reachedLimit            = false; // -n 帧数到了
    size_t readSize              = 0;
    RK_S32 decodedFrameCount     = 0;
    RK_S32 errorFrameCount       = 0;
    RK_S32 infoChangeCount       = 0;
    long putRetryCount           = 0;
    std::chrono::steady_clock::time_point startTime;
    double seconds = 0;
    int exitCode   = -1;

    // ==================== M1：初始化解码器 ====================
    CHECK(mpp_create(&decoderCtx, &decoderApi));
    CHECK(mpp_init(decoderCtx, MPP_CTX_DEC, a.type));
    CHECK(mpp_dec_cfg_init(&decoderCfg));
    CHECK(decoderApi->control(decoderCtx, MPP_DEC_GET_CFG, decoderCfg));
    CHECK(mpp_dec_cfg_set_u32(decoderCfg, "base:split_parse", 1)); // 内部分帧：我们按 64KB 读，不按帧切
    CHECK(decoderApi->control(decoderCtx, MPP_DEC_SET_CFG, decoderCfg));
    printf("|            decoder ready (%s)\n", a.type == MPP_VIDEO_CodingAVC ? "h264" : "h265");

    streamInputFile = fopen(a.streamInputPath, "rb");
    if (!streamInputFile) {
        printf("open %s failed\n", a.streamInputPath);
        goto CLEANUP;
    }
    if (a.yuvOutputPath) {
        yuvOutputFile = fopen(a.yuvOutputPath, "wb");
        if (!yuvOutputFile) {
            printf("open %s failed\n", a.yuvOutputPath);
            goto CLEANUP;
        }
    }

    // ==================== M2～M4：送码流、取图像 ====================
    startTime = std::chrono::steady_clock::now();
    while (!outputEos && !reachedLimit) {
        // 1. 读一块码流，包成 packet（上一块送进去了、文件还没读完，才读新的）
        if (!inputPacket && !inputEos) {
            readSize = fread(chunk.data(), 1, chunkSize, streamInputFile);
            inputEos = readSize < chunkSize; // 读不满 = 文件读完了
            CHECK(mpp_packet_init(&inputPacket, chunk.data(), readSize));
            if (inputEos) {
                mpp_packet_set_eos(inputPacket);
                printf("              input end, send EOS\n");
            }
        }

        // 2. 送进去：队列满了返回 MPP_ERR_BUFFER_FULL，这包留着下一轮重送
        if (inputPacket) {
            MPP_RET ret = decoderApi->decode_put_packet(decoderCtx, inputPacket);
            if (ret == MPP_OK) {
                mpp_packet_deinit(&inputPacket); // MPP 已经拷走了数据，inputPacket 变回 nullptr
            } else if (ret == MPP_ERR_BUFFER_FULL) {
                putRetryCount++;
            } else {
                printf("[FAIL] decode_put_packet ret=%d\n", ret);
                goto CLEANUP;
            }
        }

        // 3. 把现在能拿的图像都拿出来（非阻塞：没有就返回 MPP_OK + nullptr）
        while (true) {
            CHECK(decoderApi->decode_get_frame(decoderCtx, &outputFrame));
            if (!outputFrame) {
                break;
            }
            if (mpp_frame_get_info_change(outputFrame)) {
                const RK_U32 width     = mpp_frame_get_width(outputFrame);
                const RK_U32 height    = mpp_frame_get_height(outputFrame);
                const RK_U32 horStride = mpp_frame_get_hor_stride(outputFrame);
                const RK_U32 verStride = mpp_frame_get_ver_stride(outputFrame);
                const size_t bufSize   = mpp_frame_get_buf_size(outputFrame);
                infoChangeCount++;
                printf("|            info change: %ux%u stride %ux%u buf_size %zu\n", width, height, horStride,
                    verStride, bufSize);
                if (!frameGroup) {
                    CHECK(mpp_buffer_group_get_internal(&frameGroup, MPP_BUFFER_TYPE_DRM));
                } else {
                    CHECK(mpp_buffer_group_clear(frameGroup));
                }
                CHECK(mpp_buffer_group_limit_config(frameGroup, bufSize, 24));
                CHECK(decoderApi->control(decoderCtx, MPP_DEC_SET_EXT_BUF_GROUP, frameGroup));
                CHECK(decoderApi->control(decoderCtx, MPP_DEC_SET_INFO_CHANGE_READY, nullptr));
            } else {
                const RK_U32 errInfo = mpp_frame_get_errinfo(outputFrame);
                const RK_U32 discard = mpp_frame_get_discard(outputFrame);
                if (errInfo || discard) {
                    errorFrameCount++;
                    printf("              frame %-4d err %x discard %x, skip\n", decodedFrameCount, errInfo, discard);
                } else if (mpp_frame_get_buffer(outputFrame)) {
                    if (yuvOutputFile && !write_nv12_frame(yuvOutputFile, outputFrame)) {
                        perror("fwrite frame");
                        goto CLEANUP;
                    }
                    decodedFrameCount++;
                    if (a.maxFrames > 0 && decodedFrameCount >= a.maxFrames) {
                        reachedLimit = true;
                    }
                }
            }
            outputEos = mpp_frame_get_eos(outputFrame);
            mpp_frame_deinit(&outputFrame);
            if (outputEos || reachedLimit) {
                break;
            }
        }

        // 4. 这一包没送进去（队列满），或者 EOS 已经送了但还没拿到最后一帧：等 1ms 再试
        if (inputPacket || (inputEos && !outputEos && !reachedLimit)) {
            usleep(1000);
        }
    }
    seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count();

    printf("\n===== summary =====\n");
    printf("input     : %s\n", a.streamInputPath);
    printf("output    : %s\n", a.yuvOutputPath ? a.yuvOutputPath : "(不写文件)");
    printf("frames    : %d (error/discard: %d, info change: %d)\n", decodedFrameCount, errorFrameCount,
        infoChangeCount);
    printf("time      : %.2f s, %.1f fps\n", seconds, seconds > 0 ? decodedFrameCount / seconds : 0.0);
    printf("put retry : %ld 次（队列满）\n", putRetryCount);
    exitCode = 0;

CLEANUP:
    if (outputFrame) {
        mpp_frame_deinit(&outputFrame);
    }
    if (inputPacket) {
        mpp_packet_deinit(&inputPacket);
    }
    if (decoderCfg) {
        mpp_dec_cfg_deinit(decoderCfg);
    }
    if (decoderCtx) {
        mpp_destroy(decoderCtx);
    }
    if (frameGroup) {
        mpp_buffer_group_put(frameGroup);
    }
    if (yuvOutputFile) {
        fclose(yuvOutputFile);
    }
    if (streamInputFile) {
        fclose(streamInputFile);
    }
    return exitCode;
}
```

### 怎么验证

```bash
./run_on_board_with_mac.sh -n 60 -o /userdata/av/out.nv12      # 和 M4 一样，60 帧写文件
./run_on_board_with_mac.sh                                     # 不写文件，解完全部 1803 帧，看 fps，验证走到 EOS
./run_on_board_with_mac.sh -n 300                              # 300 帧测速，和 mpi_dec_test -n 300 对比
./run_on_board_with_mac.sh -t h265 -i /userdata/av/out.h265 -o /userdata/av/dec265.nv12   # 解 Day 1 编的 H.265
./run_on_board_with_mac.sh -t vp8                              # 参数错误 → 打印用法，退出码 -1
```

预期：解码 fps 明显高于 30（RK3588 解 1080p H.264 应该能到几百 fps），不限帧数时解出 1803 帧、打印 `input end, send EOS` 后正常退出，`error/discard: 0`，`info change: 1`。

---

## 8. 跑通后：整理成 `MppDecoder` 类

和 Day 1 一样拆到 `src/` 下，复用 Day 1 的 `common/mpp_utils.h`（`MPP_CHECK`、RAII 句柄）：

| `main()` 里的代码段 | 移到哪里 |
|---|---|
| M1 的 create / init / split_parse | `MppDecoder::init(MppCodingType)` |
| `decode_put_packet` + 队列满的判断 | `MppDecoder::feed(...)` → 返回"送进去了 / 队列满" |
| `decode_get_frame` + info change 处理 | `MppDecoder::get_frame(...)`：info change 在类里面处理掉，外面只拿到真正的图像 |
| 释放 cfg / ctx / 内存池 | `~MppDecoder()` |
| 按 64KB 读文件 | `source/read_stream.*`（对应 Day 1 的 `ReadYUV`） |
| 逐行写 NV12 | `sink/write_yuv.*`（对应 Day 1 的 `WriteStream`） |
| 主循环 | `pipeline/decode_pipeline.*` |

整理完再跑一遍，输出文件应该和整理前**完全一样**（`md5sum` 对比）。到时候再写详细的拆分方案。

---

## 9. 每一步的检查清单

| 里程碑 | 检查 |
|---|---|
| M1 | 打印 `decoder ready`，退出码 0 |
| M2 | 打印 `info change: 1920x1080 stride 1920x1088 buf_size ...` |
| M3 | `out.nv12` 正好 3,110,400 字节；ffplay 看到一帧正常画面 |
| M4 | 60 帧 186,624,000 字节，ffplay 播放连续 |
| M5 | `-o` / `-n` / `-t` 都生效；参数错误打印用法；不限帧数能走到 EOS 正常退出 |
| M6 | 汇总里 fps 远高于 30；error/discard 为 0；info change 为 1 |
