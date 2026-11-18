# 具体写 day1_yuv2h264 的流程

> 目标：把一个 NV12 文件（1920×1080）用 RK3588 的硬件编码器编成 H.264 文件，在 Mac 上用 VLC 能播放。
>
> 本文的行号都对应 `demo_code_mmp/test/mpi_enc_test.c`（就是 `rk_code/external/mpp/test/mpi_enc_test.c`）。

---

## 0. 先放下心理负担

`mpi_enc_test.c` 有 1000 多行，是因为它是官方的**全功能测试工具**。我们只要它的**一条主线**，大约 150～200 行。

| `mpi_enc_test.c` 里的部分 | 今天要不要 |
|---|---|
| 命令行解析、几十个参数 | ❌ 先写死，M5 再加 |
| `enc_test_multi` 多线程、多实例 | ❌ |
| RGB / 422 / FBC 等格式 | ❌ 只支持 NV12 |
| H.265 / JPEG / VP8 分支 | ❌ 先只做 H.264，M5 再加 H.265 |
| OSD、ROI、user data、运动信息、slice 分片 | ❌ |
| FIXQP、各种 qp 参数的计算 | ❌ 先写死 CBR 4Mbps |
| **create → init → 设参数 → 取头 → 读帧 → put_frame → get_packet → 释放** | ✅ **只有这条** |

**写法原则：**
1. **第一遍不写类**，全部写在 `main.cpp` 的 `main()` 里，从上往下照着顺序写（"面条代码"）。M4 跑通后再整理进 `MppEncoder` 类。
2. **每次只写一个里程碑**，上板跑通、看到预期输出，再写下一个。
3. **每个 MPP 调用都检查返回值并打印**，出错一眼就知道卡在哪一步。
4. **每完成一个里程碑就 commit 一次**，例如 `day1: M1 编码器初始化`。

---

## 1. 整体调用流程（先在脑子里有这张图）

```
                     ┌──────────────────────────────────────────┐
  M1 初始化           │ mpp_create(&ctx, &mpi)                   │
                     │ mpi->control(MPP_SET_OUTPUT_TIMEOUT)     │  get_packet 阻塞等待
                     │ mpp_init(ctx, MPP_CTX_ENC, AVC)          │
                     │ mpp_enc_cfg_init(&cfg)                   │
                     │ mpi->control(MPP_ENC_GET_CFG, cfg)       │  先拿默认值
                     │ mpp_enc_cfg_set_s32(cfg, "...", ...) × N │  再改
                     │ mpi->control(MPP_ENC_SET_CFG, cfg)       │  真正生效
                     │ mpi->control(MPP_ENC_SET_HEADER_MODE)    │  每个 IDR 带 SPS/PPS
                     └──────────────────────────────────────────┘
                     ┌──────────────────────────────────────────┐
  M2 取头             │ mpp_buffer_group_get_internal(DRM)       │  硬件能访问的内存池
                     │ mpp_buffer_get(&pkt_buf)                 │
                     │ mpp_packet_init_with_buffer(&pkt, pkt_buf)│
                     │ mpi->control(MPP_ENC_GET_HDR_SYNC, pkt)  │
                     │ fwrite(SPS/PPS) → mpp_packet_deinit      │
                     └──────────────────────────────────────────┘
                     ┌──────────────────────────────────────────┐
  M3/M4 编码循环      │ mpp_buffer_get(&frm_buf)  （只申请一次）  │
  while (!pkt_eos)   │ ┌ sync_begin → 逐行读一帧 → sync_end     │
                     │ │ mpp_frame_init + set 宽高/stride/格式   │
                     │ │ mpp_frame_set_buffer / set_eos          │
                     │ │ mpi->encode_put_frame(ctx, frame)       │
                     │ │ mpp_frame_deinit                        │
                     │ │ mpi->encode_get_packet(ctx, &packet)    │
                     │ └ fwrite → 检查 eos → mpp_packet_deinit   │
                     └──────────────────────────────────────────┘
                     ┌──────────────────────────────────────────┐
  释放（倒着来）       │ mpi->reset → mpp_destroy                 │
                     │ mpp_enc_cfg_deinit                       │
                     │ mpp_buffer_put(frm_buf / pkt_buf)        │
                     │ mpp_buffer_group_put → fclose            │
                     └──────────────────────────────────────────┘
```

---

## 2. 准备工作（写 M1 之前）

### 2.1 头文件
```cpp
#include <cstdio>
#include <cstdint>
#include <rockchip/rk_mpi.h>   // 找不到就改成 "rk_mpi.h"，以板子 sysroot 里的实际路径为准
```
`rk_mpi.h` 会间接包含 `rk_venc_cfg.h`、`rk_venc_cmd.h` 等，`mpp_enc_cfg_*` 和各种 `MPP_ENC_*` 命令都在里面。缺什么类型再补对应的头文件（`mpp_buffer.h`、`mpp_frame.h`、`mpp_packet.h`、`mpp_meta.h`）。

### 2.2 自己定义对齐宏（重要）
`MPP_ALIGN` 定义在 `osal/inc/mpp_common.h`，是 MPP **内部头文件，不对外提供**，自己的程序里用不了。照抄一份：
```cpp
#define ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))
```

### 2.3 检查返回值的小宏（可选，但很省事）
```cpp
#define CHECK(expr) do { \
    MPP_RET _r = (expr); \
    if (_r != MPP_OK) { \
        printf("[FAIL] %s ret=%d (line %d)\n", #expr, (int)_r, __LINE__); \
        goto CLEANUP; \
    } \
} while (0)
```
用 `goto CLEANUP` 统一跳到末尾释放资源，和 `mpi_enc_test.c` 里的 `goto RET` 是同一个思路。

> ⚠️ **C++ 的 goto 规则**：`goto` 不能跳过 `main()` 里**带初始化的变量声明**，否则编译报错 `jump to label 'CLEANUP' crosses initialization`。所以下面的代码都把变量**集中声明在 `main()` 开头**、第一个 `CHECK` 之前。`while` / `if` 大括号里面声明的变量不受影响。

### 2.4 CMake
现在的 `CMakeLists.txt` 只在 aarch64 交叉编译时才链接 `rockchip_mpp`，所以**必须用 rk3588 工具链编译**，Mac 本地 build 会报找不到头文件，这是正常的。

### 2.5 测试素材
板子上要有 `/userdata/av/in_1080p_60f.nv12`（60 帧，见 readme 0.2）。一帧 NV12 = 1920 × 1080 × 3 / 2 = **3,110,400 字节**，60 帧约 178MB，可以用 `ls -l` 检查文件大小对不对。

---

## 3. M1：编码器初始化（约 40 行）

**目标**：运行后打印 `encoder ready: 1920x1080 stride 1920x1088`，没有报错。

### 步骤
| # | 调用 | 对照行号 | 说明 |
|:---:|---|:---:|---|
| 1 | 算 stride | 160～161 | `hor_stride = ALIGN(1920, 16) = 1920`，`ver_stride = ALIGN(1080, 16) = 1088` |
| 2 | `mpp_create(&ctx, &mpi)` | 1010 | 创建实例，拿到 `ctx`（句柄）和 `mpi`（函数表） |
| 3 | `mpi->control(ctx, MPP_SET_OUTPUT_TIMEOUT, &timeout)` | 967、1020 | `MppPollType timeout = MPP_POLL_BLOCK;`，**要在 `mpp_init` 之前** |
| 4 | `mpp_init(ctx, MPP_CTX_ENC, MPP_VIDEO_CodingAVC)` | 1027 | 初始化为 H.264 编码器 |
| 5 | `mpp_enc_cfg_init(&cfg)` | 1034 | 创建配置对象 |
| 6 | `mpi->control(ctx, MPP_ENC_GET_CFG, cfg)` | 1040 | **先拿默认配置**，再在上面改 |
| 7 | 设参数（见下表） | 334～554 | |
| 8 | `mpi->control(ctx, MPP_ENC_SET_CFG, cfg)` | 570 | **参数到这一步才真正生效** |
| 9 | `mpi->control(ctx, MPP_ENC_SET_HEADER_MODE, &header_mode)` | 592～595 | `MppEncHeaderMode header_mode = MPP_ENC_HEADER_MODE_EACH_IDR;` |
| 10 | 打印 ready，然后释放 | 1079、1084 | `mpp_enc_cfg_deinit(cfg)`、`mpp_destroy(ctx)` |

### 要设置的参数（照着 `test_mpp_enc_cfg_setup` 抄）
| 参数 | 值 | 对照行号 |
|---|---|:---:|
| `prep:width` / `prep:height` | 1920 / 1080 | 334～335 |
| `prep:hor_stride` / `prep:ver_stride` | 1920 / 1088 | 336～337 |
| `prep:format` | `MPP_FMT_YUV420SP` | 338 |
| `rc:mode` | `MPP_ENC_RC_MODE_CBR` | 341 |
| `rc:fps_in_flex` / `rc:fps_in_num` / `rc:fps_in_denom` | 0 / 30 / 1 | 344～346 |
| `rc:fps_out_flex` / `rc:fps_out_num` / `rc:fps_out_denom` | 0 / 30 / 1 | 347～349 |
| `rc:bps_target` | 4000000 | 357 |
| `rc:bps_max` / `rc:bps_min` | bps × 17/16 / bps × 15/16（CBR 的算法） | 366～367 |
| `codec:type` | `MPP_VIDEO_CodingAVC` | 452 |
| `h264:profile` / `h264:level` | 100 / 40 | 502、505 |
| `h264:cabac_en` / `h264:cabac_idc` | 1 / 0 | 506～507 |
| `rc:gop` | 60（fps × 2） | 554 |

### 完整代码（照着敲，替换 `main.cpp`）
```cpp
#include <cstdio>
#include <cstdint>
#include <rockchip/rk_mpi.h>   // 找不到就改成 "rk_mpi.h"

// MPP_ALIGN 在 MPP 内部头文件里，用不了，自己定义一个
#define ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))

// 调用失败就打印是哪一句，然后跳到 CLEANUP 统一释放
#define CHECK(expr) do { \
    MPP_RET _r = (expr); \
    if (_r != MPP_OK) { \
        printf("[FAIL] %s ret=%d (line %d)\n", #expr, (int)_r, __LINE__); \
        goto CLEANUP; \
    } \
} while (0)

int main() {
    // ---------- 参数（先写死） ----------
    const int width      = 1920;
    const int height     = 1080;
    const int hor_stride = ALIGN(width, 16);    // 1920
    const int ver_stride = ALIGN(height, 16);   // 1088
    const int fps        = 30;
    const int bps        = 4 * 1000 * 1000;     // 4Mbps
    const int gop        = fps * 2;             // 60 帧一个 I 帧

    // ---------- 所有变量都在这里声明（goto 规则） ----------
    MppCtx    ctx = nullptr;                    // 编码器句柄
    MppApi*   mpi = nullptr;                    // 函数表：mpi->control / encode_put_frame ...
    MppEncCfg cfg = nullptr;                    // 编码参数
    MppPollType      timeout     = MPP_POLL_BLOCK;
    MppEncHeaderMode header_mode = MPP_ENC_HEADER_MODE_EACH_IDR;
    int ret_code = -1;

    // ---------- 1. 创建、初始化 ----------
    CHECK(mpp_create(&ctx, &mpi));
    CHECK(mpi->control(ctx, MPP_SET_OUTPUT_TIMEOUT, &timeout));   // 让 get_packet 阻塞等结果
    CHECK(mpp_init(ctx, MPP_CTX_ENC, MPP_VIDEO_CodingAVC));       // H.264 编码器

    // ---------- 2. 先拿默认配置，再改 ----------
    CHECK(mpp_enc_cfg_init(&cfg));
    CHECK(mpi->control(ctx, MPP_ENC_GET_CFG, cfg));

    // 输入图像的描述（必须和内存里的排布一致）
    mpp_enc_cfg_set_s32(cfg, "prep:width",      width);
    mpp_enc_cfg_set_s32(cfg, "prep:height",     height);
    mpp_enc_cfg_set_s32(cfg, "prep:hor_stride", hor_stride);
    mpp_enc_cfg_set_s32(cfg, "prep:ver_stride", ver_stride);
    mpp_enc_cfg_set_s32(cfg, "prep:format",     MPP_FMT_YUV420SP);   // NV12

    // 码率控制：CBR
    mpp_enc_cfg_set_s32(cfg, "rc:mode", MPP_ENC_RC_MODE_CBR);

    // 帧率：输入 30/1，输出 30/1（flex = 0 表示固定帧率）
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_flex",   0);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_num",    fps);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_denom",  1);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_flex",  0);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_num",   fps);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_denom", 1);

    // 码率：CBR 的上下限很窄（±1/16）
    mpp_enc_cfg_set_s32(cfg, "rc:bps_target", bps);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_max",    bps * 17 / 16);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_min",    bps * 15 / 16);

    // GOP
    mpp_enc_cfg_set_s32(cfg, "rc:gop", gop);

    // H.264 专属参数
    mpp_enc_cfg_set_s32(cfg, "codec:type",    MPP_VIDEO_CodingAVC);
    mpp_enc_cfg_set_s32(cfg, "h264:profile",  100);   // High Profile
    mpp_enc_cfg_set_s32(cfg, "h264:level",    40);    // Level 4.0，够 1080p@30fps
    mpp_enc_cfg_set_s32(cfg, "h264:cabac_en", 1);     // 开 CABAC（Main 及以上才能开）
    mpp_enc_cfg_set_s32(cfg, "h264:cabac_idc", 0);

    // ---------- 3. 参数真正生效 ----------
    CHECK(mpi->control(ctx, MPP_ENC_SET_CFG, cfg));
    CHECK(mpi->control(ctx, MPP_ENC_SET_HEADER_MODE, &header_mode));   // 每个 IDR 前带 SPS/PPS

    printf("encoder ready: %dx%d stride %dx%d\n", width, height, hor_stride, ver_stride);
    ret_code = 0;

CLEANUP:
    if (cfg) mpp_enc_cfg_deinit(cfg);
    if (ctx) mpp_destroy(ctx);
    return ret_code;
}
```

**运行**：Mac 上执行 `./run_on_board_with_mac.sh`（这一步不需要参数），应该看到：
```
encoder ready: 1920x1080 stride 1920x1088
```
如果看到 `[FAIL] ...`，它会告诉你是哪一句、返回值是多少。

### 易错点
- `MPP_SET_OUTPUT_TIMEOUT` 放在 `mpp_init` 之后可能不生效，按源码顺序放在前面。
- 设了参数但忘了 `SET_CFG`，参数等于没设。

---

## 4. M2：取 SPS/PPS 写进文件（约 20 行）

**目标**：生成 `out.h264`，在 Mac 上 `xxd out.h264 | head -3` 能看到 `00 00 00 01 67`（SPS）……`00 00 00 01 68`（PPS）。

### 步骤
| # | 调用 | 对照行号 | 说明 |
|:---:|---|:---:|---|
| 1 | `fopen("out.h264", "wb")` | | 输出文件 |
| 2 | `mpp_buffer_group_get_internal(&buf_grp, MPP_BUFFER_TYPE_DRM \| MPP_BUFFER_FLAGS_CACHABLE)` | 982 | 创建 DRM 内存池（硬件能访问的内存） |
| 3 | 算 `frame_size = ALIGN(hor_stride, 64) * ALIGN(ver_stride, 64) * 3 / 2` | 225 | 1920 × 1088 × 3/2 = 3,133,440 |
| 4 | `mpp_buffer_get(buf_grp, &pkt_buf, frame_size)` | 996 | 输出码流缓冲区，**一帧码流不会比原图还大**，所以用 frame_size 足够 |
| 5 | `mpp_packet_init_with_buffer(&packet, pkt_buf)` | 645 | 把 pkt_buf 包装成 packet |
| 6 | `mpp_packet_set_length(packet, 0)` | 647 | **源码注释特别强调：一定要清零** |
| 7 | `mpi->control(ctx, MPP_ENC_GET_HDR_SYNC, packet)` | 650 | 编码器把 SPS/PPS 写进 packet |
| 8 | `fwrite(mpp_packet_get_pos(packet), 1, mpp_packet_get_length(packet), fp_out)` | 656～660 | 写到文件开头 |
| 9 | `mpp_packet_deinit(&packet)` | 663 | |

### 完整代码（在 M1 基础上改，`★ M2` 标出的是新增部分）
```cpp
#include <cstdio>
#include <cstdint>
#include <rockchip/rk_mpi.h>

#define ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))

#define CHECK(expr) do { \
    MPP_RET _r = (expr); \
    if (_r != MPP_OK) { \
        printf("[FAIL] %s ret=%d (line %d)\n", #expr, (int)_r, __LINE__); \
        goto CLEANUP; \
    } \
} while (0)

int main() {
    // ---------- 参数（先写死） ----------
    const int width      = 1920;
    const int height     = 1080;
    const int hor_stride = ALIGN(width, 16);    // 1920
    const int ver_stride = ALIGN(height, 16);   // 1088
    const int fps        = 30;
    const int bps        = 4 * 1000 * 1000;
    const int gop        = fps * 2;
    const char* out_path = "/userdata/av/out.h264";                                  // ★ M2
    // 缓冲区大小：stride 再按 64 对齐，NV12 是 ×3/2
    const size_t frame_size = ALIGN(hor_stride, 64) * ALIGN(ver_stride, 64) * 3 / 2;  // ★ M2

    // ---------- 所有变量都在这里声明 ----------
    MppCtx    ctx = nullptr;
    MppApi*   mpi = nullptr;
    MppEncCfg cfg = nullptr;
    MppPollType      timeout     = MPP_POLL_BLOCK;
    MppEncHeaderMode header_mode = MPP_ENC_HEADER_MODE_EACH_IDR;
    FILE*          fp_out  = nullptr;   // ★ M2 输出文件
    MppBufferGroup buf_grp = nullptr;   // ★ M2 DRM 内存池
    MppBuffer      pkt_buf = nullptr;   // ★ M2 装码流的缓冲区
    MppPacket      packet  = nullptr;   // ★ M2 码流包
    int ret_code = -1;

    // ---------- 1. 创建、初始化 ----------
    CHECK(mpp_create(&ctx, &mpi));
    CHECK(mpi->control(ctx, MPP_SET_OUTPUT_TIMEOUT, &timeout));
    CHECK(mpp_init(ctx, MPP_CTX_ENC, MPP_VIDEO_CodingAVC));

    // ---------- 2. 配置参数 ----------
    CHECK(mpp_enc_cfg_init(&cfg));
    CHECK(mpi->control(ctx, MPP_ENC_GET_CFG, cfg));

    mpp_enc_cfg_set_s32(cfg, "prep:width",      width);
    mpp_enc_cfg_set_s32(cfg, "prep:height",     height);
    mpp_enc_cfg_set_s32(cfg, "prep:hor_stride", hor_stride);
    mpp_enc_cfg_set_s32(cfg, "prep:ver_stride", ver_stride);
    mpp_enc_cfg_set_s32(cfg, "prep:format",     MPP_FMT_YUV420SP);

    mpp_enc_cfg_set_s32(cfg, "rc:mode", MPP_ENC_RC_MODE_CBR);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_flex",   0);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_num",    fps);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_denom",  1);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_flex",  0);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_num",   fps);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_denom", 1);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_target", bps);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_max",    bps * 17 / 16);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_min",    bps * 15 / 16);
    mpp_enc_cfg_set_s32(cfg, "rc:gop", gop);

    mpp_enc_cfg_set_s32(cfg, "codec:type",    MPP_VIDEO_CodingAVC);
    mpp_enc_cfg_set_s32(cfg, "h264:profile",  100);
    mpp_enc_cfg_set_s32(cfg, "h264:level",    40);
    mpp_enc_cfg_set_s32(cfg, "h264:cabac_en", 1);
    mpp_enc_cfg_set_s32(cfg, "h264:cabac_idc", 0);

    CHECK(mpi->control(ctx, MPP_ENC_SET_CFG, cfg));
    CHECK(mpi->control(ctx, MPP_ENC_SET_HEADER_MODE, &header_mode));
    printf("encoder ready: %dx%d stride %dx%d\n", width, height, hor_stride, ver_stride);

    // ---------- ★ M2：3. 打开输出文件 ----------
    fp_out = fopen(out_path, "wb");
    if (!fp_out) {
        printf("open %s failed\n", out_path);
        goto CLEANUP;
    }

    // ---------- ★ M2：4. 申请硬件能访问的内存 ----------
    // DRM：硬件能直接访问的内存；CACHABLE：CPU 读写走缓存（快），但写完要 sync_end
    CHECK(mpp_buffer_group_get_internal(&buf_grp, MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE));
    CHECK(mpp_buffer_get(buf_grp, &pkt_buf, frame_size));

    // ---------- ★ M2：5. 取 SPS/PPS，写到文件开头 ----------
    CHECK(mpp_packet_init_with_buffer(&packet, pkt_buf));   // 用 pkt_buf 包一个 packet
    mpp_packet_set_length(packet, 0);                       // 源码注释：一定要先清零！
    CHECK(mpi->control(ctx, MPP_ENC_GET_HDR_SYNC, packet)); // 编码器把 SPS/PPS 写进 packet

    fwrite(mpp_packet_get_pos(packet), 1, mpp_packet_get_length(packet), fp_out);
    printf("header: %zu bytes\n", mpp_packet_get_length(packet));
    mpp_packet_deinit(&packet);                             // deinit 后 packet 会被置成 NULL

    ret_code = 0;

CLEANUP:
    // 倒着释放：先 packet，再编码器，最后内存池和文件
    if (packet)  mpp_packet_deinit(&packet);      // ★ M2
    if (ctx)     mpp_destroy(ctx);
    if (cfg)     mpp_enc_cfg_deinit(cfg);
    if (pkt_buf) mpp_buffer_put(pkt_buf);         // ★ M2
    if (buf_grp) mpp_buffer_group_put(buf_grp);   // ★ M2
    if (fp_out)  fclose(fp_out);                  // ★ M2
    return ret_code;
}
```

**运行和验证**：
```bash
./run_on_board_with_mac.sh
# 应该看到：
#   encoder ready: 1920x1080 stride 1920x1088
#   header: xx bytes        ← SPS+PPS 很小，几十个字节左右

# 拉回 Mac 看前几个字节
scp root@$IP:/userdata/av/out.h264 /tmp/ && xxd /tmp/out.h264 | head -3
# 应该能找到 00 00 00 01 67 ...（SPS）和 00 00 00 01 68 ...（PPS）
```
`67` 和 `68` 是 NAL 头：低 5 位 `0x67 & 0x1F = 7` 是 SPS，`0x68 & 0x1F = 8` 是 PPS。

### 易错点
- 第 2 步在 M1 的 `SET_CFG` 之后、`GET_HDR_SYNC` 之前都可以，但 **`GET_HDR_SYNC` 一定要在 `SET_CFG` 之后**，否则拿到的是默认参数的头。
- 释放时多了 `mpp_buffer_put(pkt_buf)` 和 `mpp_buffer_group_put(buf_grp)`（1094、1109 行）。

---

## 5. M3：编码 1 帧（约 50 行，**今天的难点**）

**目标**：`ffprobe -show_frames out_1f.h264` 能看到 1 帧，`pict_type=I`。

### 步骤
| # | 调用 | 对照行号 | 说明 |
|:---:|---|:---:|---|
| 1 | `fopen("in_1080p_60f.nv12", "rb")` | | 输入文件 |
| 2 | `mpp_buffer_get(buf_grp, &frm_buf, frame_size)` | 989 | 输入图像缓冲区，**必须从 DRM 池申请，不能 malloc** |
| 3 | `uint8_t* dst = (uint8_t*)mpp_buffer_get_ptr(frm_buf)` | 671 | 拿到硬件缓冲区的 CPU 地址 |
| 4 | `mpp_buffer_sync_begin(frm_buf)` | 677 | CPU 开始写之前 |
| 5 | ⭐ **逐行读一帧**（见下面代码） | `utils.c` 544～564 | 照抄 `read_image` 的 NV12 分支 |
| 6 | ⭐ `mpp_buffer_sync_end(frm_buf)` | 696 | **CPU 写完刷缓存，硬件才能看到新数据** |
| 7 | `mpp_frame_init(&frame)` | 721 | |
| 8 | `mpp_frame_set_width/height/hor_stride/ver_stride/fmt` | 728～734 | **必须和 `prep:*` 参数一致** |
| 9 | `mpp_frame_set_eos(frame, 0)` | 735 | 不是最后一帧 |
| 10 | `mpp_frame_set_buffer(frame, frm_buf)` | 742 | 把缓冲区挂到 frame 上 |
| 11 | `mpi->encode_put_frame(ctx, frame)` | 843 | 送进编码器（阻塞，等硬件读完） |
| 12 | `mpp_frame_deinit(&frame)` | 850 | put 完就可以释放 frame（缓冲区还在） |
| 13 | `mpi->encode_get_packet(ctx, &packet)` | 854 | 取出码流（阻塞，因为设了 `MPP_POLL_BLOCK`） |
| 14 | `fwrite(pos, 1, length, fp_out)` | 864～877 | 写到 SPS/PPS 后面 |
| 15 | `mpp_packet_deinit(&packet)` | 926 | |

### 逐行读一帧（核心代码）
```cpp
// Y：height 行，每行从文件读 width 字节，写到 row * hor_stride 的位置
for (int row = 0; row < height; row++) {
    if (fread(dst + row * hor_stride, 1, width, fp_in) != (size_t)width) { /* 读完了 */ }
}
// UV：从 hor_stride * ver_stride 开始（第 1088 行，不是 1080 行）
uint8_t* dst_uv = dst + hor_stride * ver_stride;
for (int row = 0; row < height / 2; row++) {
    if (fread(dst_uv + row * hor_stride, 1, width, fp_in) != (size_t)width) { /* 读完了 */ }
}
```

### 关于输出 packet（两种写法）
- **简单写法**：不给 frame 设置输出 packet，直接 `encode_get_packet` 拿编码器给的 packet。下面的代码用这种。编码器发现你没给 packet 时会自己分配（MPP 源码 `mpp/codec/mpp_enc_impl.cpp` 1400～1443 行）。
- **源码写法**（745～747 行）：每帧用 `pkt_buf` 包装一个 packet，`set_length(0)`，再 `mpp_meta_set_packet(meta, KEY_OUTPUT_PACKET, packet)` 挂到 frame 的 meta 上，编码器就会把码流写进你的 `pkt_buf`。如果简单写法出问题，改成这种。

### 完整代码（在 M2 基础上改，`★ M3` 标出的是新增部分）
```cpp
#include <cstdio>
#include <cstdint>
#include <rockchip/rk_mpi.h>

#define ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))

#define CHECK(expr) do { \
    MPP_RET _r = (expr); \
    if (_r != MPP_OK) { \
        printf("[FAIL] %s ret=%d (line %d)\n", #expr, (int)_r, __LINE__); \
        goto CLEANUP; \
    } \
} while (0)

// ★ M3：按 stride 逐行读一帧 NV12（照抄 utils.c read_image 的 NV12 分支）
// 读满一帧返回 true；读到文件末尾返回 false
static bool read_nv12_frame(FILE* fp, uint8_t* dst,
                            int width, int height, int hor_stride, int ver_stride) {
    // Y：height 行，每行从文件读 width 字节，写到 row * hor_stride 的位置
    for (int row = 0; row < height; row++) {
        if (fread(dst + row * hor_stride, 1, width, fp) != (size_t)width)
            return false;
    }
    // UV：从第 ver_stride 行开始（1088，不是 1080）
    //     只有 height/2 行，但每行还是 width 字节（U、V 交错：UVUV...）
    uint8_t* dst_uv = dst + hor_stride * ver_stride;
    for (int row = 0; row < height / 2; row++) {
        if (fread(dst_uv + row * hor_stride, 1, width, fp) != (size_t)width)
            return false;
    }
    return true;
}

int main() {
    // ---------- 参数（先写死） ----------
    const int width      = 1920;
    const int height     = 1080;
    const int hor_stride = ALIGN(width, 16);
    const int ver_stride = ALIGN(height, 16);
    const int fps        = 30;
    const int bps        = 4 * 1000 * 1000;
    const int gop        = fps * 2;
    const char* in_path  = "/userdata/av/in_1080p_60f.nv12";   // ★ M3
    const char* out_path = "/userdata/av/out_1f.h264";         // ★ M3 改个名字，只有 1 帧
    const size_t frame_size = ALIGN(hor_stride, 64) * ALIGN(ver_stride, 64) * 3 / 2;

    // ---------- 所有变量都在这里声明 ----------
    MppCtx    ctx = nullptr;
    MppApi*   mpi = nullptr;
    MppEncCfg cfg = nullptr;
    MppPollType      timeout     = MPP_POLL_BLOCK;
    MppEncHeaderMode header_mode = MPP_ENC_HEADER_MODE_EACH_IDR;
    FILE*          fp_in   = nullptr;   // ★ M3 输入文件
    FILE*          fp_out  = nullptr;
    MppBufferGroup buf_grp = nullptr;
    MppBuffer      frm_buf = nullptr;   // ★ M3 装一帧原始图像的缓冲区
    MppBuffer      pkt_buf = nullptr;
    MppFrame       frame   = nullptr;   // ★ M3 描述一帧图像（宽高、stride、格式、缓冲区）
    MppPacket      packet  = nullptr;
    uint8_t*       dst     = nullptr;   // ★ M3 frm_buf 的 CPU 地址
    size_t         len     = 0;         // ★ M3
    int ret_code = -1;

    // ---------- 1～2. 创建、初始化、配置参数（和 M2 一样） ----------
    CHECK(mpp_create(&ctx, &mpi));
    CHECK(mpi->control(ctx, MPP_SET_OUTPUT_TIMEOUT, &timeout));
    CHECK(mpp_init(ctx, MPP_CTX_ENC, MPP_VIDEO_CodingAVC));

    CHECK(mpp_enc_cfg_init(&cfg));
    CHECK(mpi->control(ctx, MPP_ENC_GET_CFG, cfg));

    mpp_enc_cfg_set_s32(cfg, "prep:width",      width);
    mpp_enc_cfg_set_s32(cfg, "prep:height",     height);
    mpp_enc_cfg_set_s32(cfg, "prep:hor_stride", hor_stride);
    mpp_enc_cfg_set_s32(cfg, "prep:ver_stride", ver_stride);
    mpp_enc_cfg_set_s32(cfg, "prep:format",     MPP_FMT_YUV420SP);

    mpp_enc_cfg_set_s32(cfg, "rc:mode", MPP_ENC_RC_MODE_CBR);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_flex",   0);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_num",    fps);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_denom",  1);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_flex",  0);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_num",   fps);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_denom", 1);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_target", bps);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_max",    bps * 17 / 16);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_min",    bps * 15 / 16);
    mpp_enc_cfg_set_s32(cfg, "rc:gop", gop);

    mpp_enc_cfg_set_s32(cfg, "codec:type",    MPP_VIDEO_CodingAVC);
    mpp_enc_cfg_set_s32(cfg, "h264:profile",  100);
    mpp_enc_cfg_set_s32(cfg, "h264:level",    40);
    mpp_enc_cfg_set_s32(cfg, "h264:cabac_en", 1);
    mpp_enc_cfg_set_s32(cfg, "h264:cabac_idc", 0);

    CHECK(mpi->control(ctx, MPP_ENC_SET_CFG, cfg));
    CHECK(mpi->control(ctx, MPP_ENC_SET_HEADER_MODE, &header_mode));
    printf("encoder ready: %dx%d stride %dx%d\n", width, height, hor_stride, ver_stride);

    // ---------- 3. 打开输入、输出文件 ----------
    fp_in = fopen(in_path, "rb");                                   // ★ M3
    if (!fp_in) {
        printf("open %s failed\n", in_path);
        goto CLEANUP;
    }
    fp_out = fopen(out_path, "wb");
    if (!fp_out) {
        printf("open %s failed\n", out_path);
        goto CLEANUP;
    }

    // ---------- 4. 申请内存 ----------
    CHECK(mpp_buffer_group_get_internal(&buf_grp, MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE));
    CHECK(mpp_buffer_get(buf_grp, &frm_buf, frame_size));           // ★ M3 输入图像，不能用 malloc
    CHECK(mpp_buffer_get(buf_grp, &pkt_buf, frame_size));

    // ---------- 5. 取 SPS/PPS ----------
    CHECK(mpp_packet_init_with_buffer(&packet, pkt_buf));
    mpp_packet_set_length(packet, 0);
    CHECK(mpi->control(ctx, MPP_ENC_GET_HDR_SYNC, packet));
    fwrite(mpp_packet_get_pos(packet), 1, mpp_packet_get_length(packet), fp_out);
    printf("header: %zu bytes\n", mpp_packet_get_length(packet));
    mpp_packet_deinit(&packet);

    // ---------- ★ M3：6. 读一帧到硬件缓冲区 ----------
    dst = (uint8_t*)mpp_buffer_get_ptr(frm_buf);    // 硬件缓冲区的 CPU 地址
    mpp_buffer_sync_begin(frm_buf);                 // CPU 开始写
    if (!read_nv12_frame(fp_in, dst, width, height, hor_stride, ver_stride)) {
        printf("read frame failed, 输入文件太小？\n");
        goto CLEANUP;
    }
    mpp_buffer_sync_end(frm_buf);                   // ⭐ CPU 写完，刷缓存，硬件才能看到

    // ---------- ★ M3：7. 包装成 MppFrame ----------
    CHECK(mpp_frame_init(&frame));
    mpp_frame_set_width(frame, width);              // 这 5 个必须和 prep:* 一致
    mpp_frame_set_height(frame, height);
    mpp_frame_set_hor_stride(frame, hor_stride);
    mpp_frame_set_ver_stride(frame, ver_stride);
    mpp_frame_set_fmt(frame, MPP_FMT_YUV420SP);
    mpp_frame_set_eos(frame, 0);                    // 不是最后一帧
    mpp_frame_set_buffer(frame, frm_buf);           // 把图像数据挂上去

    // ---------- ★ M3：8. 送进去，取出来 ----------
    CHECK(mpi->encode_put_frame(ctx, frame));       // 阻塞，等硬件读完
    mpp_frame_deinit(&frame);                       // frame 用完就放，frm_buf 还在

    CHECK(mpi->encode_get_packet(ctx, &packet));    // 阻塞，等编码完成
    if (packet) {
        len = mpp_packet_get_length(packet);
        fwrite(mpp_packet_get_pos(packet), 1, len, fp_out);
        printf("frame 0 size %zu bytes\n", len);
        mpp_packet_deinit(&packet);
    }

    ret_code = 0;

CLEANUP:
    if (frame)   mpp_frame_deinit(&frame);        // ★ M3
    if (packet)  mpp_packet_deinit(&packet);
    if (ctx)     mpp_destroy(ctx);
    if (cfg)     mpp_enc_cfg_deinit(cfg);
    if (frm_buf) mpp_buffer_put(frm_buf);         // ★ M3
    if (pkt_buf) mpp_buffer_put(pkt_buf);
    if (buf_grp) mpp_buffer_group_put(buf_grp);
    if (fp_in)   fclose(fp_in);                   // ★ M3
    if (fp_out)  fclose(fp_out);
    return ret_code;
}
```

**运行和验证**：
```bash
./run_on_board_with_mac.sh
# 应该看到：
#   encoder ready: 1920x1080 stride 1920x1088
#   header: xx bytes
#   frame 0 size xxxxx bytes      ← 第一帧是 I 帧，比较大，几十 KB 到一两百 KB

scp root@$IP:/userdata/av/out_1f.h264 /tmp/
/usr/local/ffmpeg/4.4/bin/ffprobe -v error -show_frames /tmp/out_1f.h264 | grep -E 'pict_type|width|height'
# 应该看到 pict_type=I，width=1920，height=1080
```
用 ffmpeg 把这一帧转成图片看看画面对不对：
```bash
/usr/local/ffmpeg/4.4/bin/ffmpeg -i /tmp/out_1f.h264 -frames:v 1 /tmp/out_1f.png && open /tmp/out_1f.png
```

### 易错点（对应 readme 第 5 节）
| 现象 | 原因 |
|---|---|
| 画面斜着错位 | 整块 `fread`，没有按 `hor_stride` 逐行写 |
| 底部绿条、颜色不对 | UV 起始地址写成了 `width * height` |
| 花屏、残影 | 忘了 `mpp_buffer_sync_end` |
| `encode_put_frame` 报错 | 缓冲区是 `malloc` 的 |

---

## 6. M4：编完 60 帧（约 20 行）

**目标**：Mac 上 VLC 能播 2 秒画面，没有花屏、没有绿条；程序正常退出。

把 M3 的第 3～15 步套进循环，关键是**怎么结束**：

```
frm_buf 只在循环外申请一次
pkt_eos = false
while (!pkt_eos) {
    sync_begin → 逐行读一帧
    读失败（读到文件末尾）→ frm_eos = true
    sync_end

    frame_init + set 宽高/stride/格式
    set_eos(frame, frm_eos)
    if (frm_eos) set_buffer(frame, NULL)     // 736～737 行：最后送一个空帧，只带 EOS 标志
    else         set_buffer(frame, frm_buf)
    encode_put_frame → frame_deinit

    encode_get_packet
    fwrite(packet)
    pkt_eos = mpp_packet_get_eos(packet)     // 873 行：编码器回给你 EOS，才算真的结束
    packet_deinit
    frame_count++
}
```

### 完整代码（在 M3 基础上改，`★ M4` 标出的是改动部分）
`read_nv12_frame` 和 M3 完全一样；配置参数那一段也和 M3 一样，下面照样完整写出来，方便你直接替换。
```cpp
#include <cstdio>
#include <cstdint>
#include <rockchip/rk_mpi.h>

#define ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))

#define CHECK(expr) do { \
    MPP_RET _r = (expr); \
    if (_r != MPP_OK) { \
        printf("[FAIL] %s ret=%d (line %d)\n", #expr, (int)_r, __LINE__); \
        goto CLEANUP; \
    } \
} while (0)

// 按 stride 逐行读一帧 NV12；读满一帧返回 true，读到文件末尾返回 false
static bool read_nv12_frame(FILE* fp, uint8_t* dst,
                            int width, int height, int hor_stride, int ver_stride) {
    for (int row = 0; row < height; row++) {
        if (fread(dst + row * hor_stride, 1, width, fp) != (size_t)width)
            return false;
    }
    uint8_t* dst_uv = dst + hor_stride * ver_stride;
    for (int row = 0; row < height / 2; row++) {
        if (fread(dst_uv + row * hor_stride, 1, width, fp) != (size_t)width)
            return false;
    }
    return true;
}

int main() {
    // ---------- 参数（先写死） ----------
    const int width      = 1920;
    const int height     = 1080;
    const int hor_stride = ALIGN(width, 16);
    const int ver_stride = ALIGN(height, 16);
    const int fps        = 30;
    const int bps        = 4 * 1000 * 1000;
    const int gop        = fps * 2;
    const char* in_path  = "/userdata/av/in_1080p_60f.nv12";
    const char* out_path = "/userdata/av/out.h264";            // ★ M4 改回 out.h264
    const size_t frame_size = ALIGN(hor_stride, 64) * ALIGN(ver_stride, 64) * 3 / 2;

    // ---------- 所有变量都在这里声明 ----------
    MppCtx    ctx = nullptr;
    MppApi*   mpi = nullptr;
    MppEncCfg cfg = nullptr;
    MppPollType      timeout     = MPP_POLL_BLOCK;
    MppEncHeaderMode header_mode = MPP_ENC_HEADER_MODE_EACH_IDR;
    FILE*          fp_in   = nullptr;
    FILE*          fp_out  = nullptr;
    MppBufferGroup buf_grp = nullptr;
    MppBuffer      frm_buf = nullptr;
    MppBuffer      pkt_buf = nullptr;
    MppFrame       frame   = nullptr;
    MppPacket      packet  = nullptr;
    uint8_t*       dst     = nullptr;
    size_t         len     = 0;
    bool   frm_eos     = false;    // ★ M4 输入读完了（我们告诉编码器）
    bool   pkt_eos     = false;    // ★ M4 输出取完了（编码器告诉我们）
    int    frame_count = 0;        // ★ M4
    size_t stream_size = 0;        // ★ M4 码流总字节数
    int ret_code = -1;

    // ---------- 1～2. 创建、初始化、配置参数（和 M3 一样） ----------
    CHECK(mpp_create(&ctx, &mpi));
    CHECK(mpi->control(ctx, MPP_SET_OUTPUT_TIMEOUT, &timeout));
    CHECK(mpp_init(ctx, MPP_CTX_ENC, MPP_VIDEO_CodingAVC));

    CHECK(mpp_enc_cfg_init(&cfg));
    CHECK(mpi->control(ctx, MPP_ENC_GET_CFG, cfg));

    mpp_enc_cfg_set_s32(cfg, "prep:width",      width);
    mpp_enc_cfg_set_s32(cfg, "prep:height",     height);
    mpp_enc_cfg_set_s32(cfg, "prep:hor_stride", hor_stride);
    mpp_enc_cfg_set_s32(cfg, "prep:ver_stride", ver_stride);
    mpp_enc_cfg_set_s32(cfg, "prep:format",     MPP_FMT_YUV420SP);

    mpp_enc_cfg_set_s32(cfg, "rc:mode", MPP_ENC_RC_MODE_CBR);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_flex",   0);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_num",    fps);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_denom",  1);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_flex",  0);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_num",   fps);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_denom", 1);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_target", bps);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_max",    bps * 17 / 16);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_min",    bps * 15 / 16);
    mpp_enc_cfg_set_s32(cfg, "rc:gop", gop);

    mpp_enc_cfg_set_s32(cfg, "codec:type",    MPP_VIDEO_CodingAVC);
    mpp_enc_cfg_set_s32(cfg, "h264:profile",  100);
    mpp_enc_cfg_set_s32(cfg, "h264:level",    40);
    mpp_enc_cfg_set_s32(cfg, "h264:cabac_en", 1);
    mpp_enc_cfg_set_s32(cfg, "h264:cabac_idc", 0);

    CHECK(mpi->control(ctx, MPP_ENC_SET_CFG, cfg));
    CHECK(mpi->control(ctx, MPP_ENC_SET_HEADER_MODE, &header_mode));
    printf("encoder ready: %dx%d stride %dx%d\n", width, height, hor_stride, ver_stride);

    // ---------- 3. 打开文件 ----------
    fp_in = fopen(in_path, "rb");
    if (!fp_in) {
        printf("open %s failed\n", in_path);
        goto CLEANUP;
    }
    fp_out = fopen(out_path, "wb");
    if (!fp_out) {
        printf("open %s failed\n", out_path);
        goto CLEANUP;
    }

    // ---------- 4. 申请内存（只申请一次，循环里反复用） ----------
    CHECK(mpp_buffer_group_get_internal(&buf_grp, MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE));
    CHECK(mpp_buffer_get(buf_grp, &frm_buf, frame_size));
    CHECK(mpp_buffer_get(buf_grp, &pkt_buf, frame_size));
    dst = (uint8_t*)mpp_buffer_get_ptr(frm_buf);

    // ---------- 5. 取 SPS/PPS ----------
    CHECK(mpp_packet_init_with_buffer(&packet, pkt_buf));
    mpp_packet_set_length(packet, 0);
    CHECK(mpi->control(ctx, MPP_ENC_GET_HDR_SYNC, packet));
    fwrite(mpp_packet_get_pos(packet), 1, mpp_packet_get_length(packet), fp_out);
    stream_size += mpp_packet_get_length(packet);
    mpp_packet_deinit(&packet);

    // ---------- ★ M4：6. 编码循环，直到编码器告诉我们"最后一个包" ----------
    while (!pkt_eos) {
        // 6.1 读一帧；读不到了就标记 frm_eos
        if (!frm_eos) {
            mpp_buffer_sync_begin(frm_buf);
            frm_eos = !read_nv12_frame(fp_in, dst, width, height, hor_stride, ver_stride);
            mpp_buffer_sync_end(frm_buf);
            if (frm_eos)
                printf("input end, send EOS\n");
        }

        // 6.2 包装成 MppFrame
        CHECK(mpp_frame_init(&frame));
        mpp_frame_set_width(frame, width);
        mpp_frame_set_height(frame, height);
        mpp_frame_set_hor_stride(frame, hor_stride);
        mpp_frame_set_ver_stride(frame, ver_stride);
        mpp_frame_set_fmt(frame, MPP_FMT_YUV420SP);
        mpp_frame_set_eos(frame, frm_eos);
        // 最后一次送一个空帧：没有图像，只带 EOS 标志（源码 736～737 行）
        mpp_frame_set_buffer(frame, frm_eos ? nullptr : frm_buf);

        // 6.3 送进去
        CHECK(mpi->encode_put_frame(ctx, frame));
        mpp_frame_deinit(&frame);

        // 6.4 取出来
        CHECK(mpi->encode_get_packet(ctx, &packet));
        if (packet) {
            len     = mpp_packet_get_length(packet);
            pkt_eos = mpp_packet_get_eos(packet);   // 编码器说"这是最后一个"，循环就结束

            if (len > 0) {                          // EOS 空帧对应的包可能是空的，不算一帧
                fwrite(mpp_packet_get_pos(packet), 1, len, fp_out);
                stream_size += len;
                printf("frame %-3d size %zu bytes\n", frame_count, len);
                frame_count++;
            }
            mpp_packet_deinit(&packet);
        }
    }

    printf("done: %d frames, %zu bytes -> %s\n", frame_count, stream_size, out_path);
    ret_code = 0;

CLEANUP:
    if (frame)   mpp_frame_deinit(&frame);
    if (packet)  mpp_packet_deinit(&packet);
    if (ctx)     mpp_destroy(ctx);
    if (cfg)     mpp_enc_cfg_deinit(cfg);
    if (frm_buf) mpp_buffer_put(frm_buf);
    if (pkt_buf) mpp_buffer_put(pkt_buf);
    if (buf_grp) mpp_buffer_group_put(buf_grp);
    if (fp_in)   fclose(fp_in);
    if (fp_out)  fclose(fp_out);
    return ret_code;
}
```

**运行和验证**：
```bash
./run_on_board_with_mac.sh
# 应该看到：
#   frame 0   size 1xxxxx bytes    ← I 帧大
#   frame 1   size xxxxx bytes     ← P 帧小很多
#   ...
#   frame 59  size xxxxx bytes
#   input end, send EOS
#   done: 60 frames, xxxxxxx bytes -> /userdata/av/out.h264

scp root@$IP:/userdata/av/out.h264 /Users/dev/Documents/AV/rk_test_data/
open -a VLC /Users/dev/Documents/AV/rk_test_data/out.h264
/usr/local/ffmpeg/4.4/bin/ffprobe -v error -show_frames /Users/dev/Documents/AV/rk_test_data/out.h264 | grep pict_type | sort | uniq -c
# 60 帧、gop=60 → 应该是 1 个 I 帧，其余是 P 帧
```

**两个 eos 的区别**（这是最容易绕晕的地方）：
| 变量 | 谁设置 | 意思 |
|---|---|---|
| `frm_eos` | 我们 → 编码器 | "我没有图像要送了" |
| `pkt_eos` | 编码器 → 我们 | "我手里的码流也全给你了" |

只有等到 `pkt_eos`，才说明编码器里不再有没取出来的数据，这时才能安全退出。

### 易错点
- **程序最后卡住**：最后没有送 EOS 帧，或者没把 packet 取到 `mpp_packet_get_eos` 为真。
- **内存一直涨**：每个 frame、packet 用完都要 deinit。
- 源码里 `get_packet` 外面还有一层 `do { ... } while (!eoi)`（854～937 行），是给 slice 分片输出用的。我们一帧只出一个 packet，**不需要这层**。

---

## 7. M5：命令行参数（可选，跑通后再做）

用 `getopt` 解析：`-i 输入 -o 输出 -w 宽 -h 高 -t h264|h265 -rc cbr|vbr|avbr -bps 码率 -g gop`

要改的地方：
- `-t h265`：`mpp_init` 和 `codec:type` 改成 `MPP_VIDEO_CodingHEVC`，H.264 专属的 `h264:*` 参数不要设（对照 454 行的 AVC 分支和 517 行的 HEVC 分支）。
- `-rc vbr` / `avbr`：`rc:mode` 改掉，`bps_min` 的算法不一样（对照 373～380 行）。

**验证**：`-t h265` 输出的 `out.h265` 也能播放。

---

## 8. M6：统计（可选）

| 统计项 | 怎么算 |
|---|---|
| 总帧数 | 每取到一个 packet，`frame_count++` |
| 总用时 / 平均 fps | 循环前后各取一次时间（`std::chrono::steady_clock`），`frame_count / 秒数` |
| 实际码率 | 累加每个 packet 的 `length` 得到 `stream_size`，`stream_size * 8 / 秒数`（按 30fps 算播放时长更准：`frame_count / 30.0`） |
| I 帧数量 | `mpp_packet_get_meta(packet)` 后用 `mpp_meta_get_s32(meta, KEY_OUTPUT_INTRA, &is_intra)`，为 1 就是 I 帧 |

**验证**：程序结束时打印一行汇总，再用 ffprobe 对一下 I 帧数是否一致。

### M5 + M6 合起来的完整代码（在 M4 基础上改）
改动点：
- `★ M5`：写死的参数换成 `Args` 结构体 + `parse_args()`；`-t h265` 时换编码类型、跳过 `h264:*` 参数；`-rc` 决定 `bps_min` 的算法。
- `★ M6`：计时、统计 I 帧、最后打印汇总。

```cpp
#include <cstdio>
#include <cstdint>
#include <cstdlib>    // ★ M5 atoi
#include <cstring>    // ★ M5 strcmp
#include <chrono>     // ★ M6 计时
#include <rockchip/rk_mpi.h>

#define ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))

#define CHECK(expr) do { \
    MPP_RET _r = (expr); \
    if (_r != MPP_OK) { \
        printf("[FAIL] %s ret=%d (line %d)\n", #expr, (int)_r, __LINE__); \
        goto CLEANUP; \
    } \
} while (0)

// ★ M5：命令行参数，带默认值
struct Args {
    const char*   in_path  = "/userdata/av/in_1080p_60f.nv12";
    const char*   out_path = "/userdata/av/out.h264";
    int           width    = 1920;
    int           height   = 1080;
    MppCodingType type     = MPP_VIDEO_CodingAVC;
    MppEncRcMode  rc_mode  = MPP_ENC_RC_MODE_CBR;
    int           bps      = 4 * 1000 * 1000;
    int           fps      = 30;
    int           gop      = 0;      // 0 表示用默认值 fps * 2
};

static void usage(const char* prog) {
    printf("usage: %s -i in.nv12 -o out.h264 [-w 1920] [-h 1080] [-t h264|h265]\n"
           "          [-rc cbr|vbr|avbr] [-bps 4000000] [-fps 30] [-g 60]\n", prog);
}

// ★ M5：每个参数都是 "-名字 值" 成对出现
// 不用 getopt，因为 -rc、-bps 是多个字母，getopt 只认单字母
static bool parse_args(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; i += 2) {
        const char* opt = argv[i];
        const char* val = (i + 1 < argc) ? argv[i + 1] : nullptr;
        if (!val) {
            printf("参数 %s 缺少值\n", opt);
            return false;
        }

        if      (!strcmp(opt, "-i"))   a.in_path  = val;
        else if (!strcmp(opt, "-o"))   a.out_path = val;
        else if (!strcmp(opt, "-w"))   a.width    = atoi(val);
        else if (!strcmp(opt, "-h"))   a.height   = atoi(val);
        else if (!strcmp(opt, "-bps")) a.bps      = atoi(val);
        else if (!strcmp(opt, "-fps")) a.fps      = atoi(val);
        else if (!strcmp(opt, "-g"))   a.gop      = atoi(val);
        else if (!strcmp(opt, "-t")) {
            if      (!strcmp(val, "h264")) a.type = MPP_VIDEO_CodingAVC;
            else if (!strcmp(val, "h265")) a.type = MPP_VIDEO_CodingHEVC;
            else { printf("-t 只支持 h264 / h265\n"); return false; }
        } else if (!strcmp(opt, "-rc")) {
            if      (!strcmp(val, "cbr"))  a.rc_mode = MPP_ENC_RC_MODE_CBR;
            else if (!strcmp(val, "vbr"))  a.rc_mode = MPP_ENC_RC_MODE_VBR;
            else if (!strcmp(val, "avbr")) a.rc_mode = MPP_ENC_RC_MODE_AVBR;
            else { printf("-rc 只支持 cbr / vbr / avbr\n"); return false; }
        } else {
            printf("未知参数 %s\n", opt);
            return false;
        }
    }
    if (a.gop == 0)
        a.gop = a.fps * 2;
    return true;
}

static bool read_nv12_frame(FILE* fp, uint8_t* dst,
                            int width, int height, int hor_stride, int ver_stride) {
    for (int row = 0; row < height; row++) {
        if (fread(dst + row * hor_stride, 1, width, fp) != (size_t)width)
            return false;
    }
    uint8_t* dst_uv = dst + hor_stride * ver_stride;
    for (int row = 0; row < height / 2; row++) {
        if (fread(dst_uv + row * hor_stride, 1, width, fp) != (size_t)width)
            return false;
    }
    return true;
}

int main(int argc, char** argv) {
    // ---------- ★ M5：解析参数（在第一个 CHECK 之前，所以可以直接 return） ----------
    Args a;
    if (!parse_args(argc, argv, a)) {
        usage(argv[0]);
        return -1;
    }

    const int hor_stride = ALIGN(a.width, 16);
    const int ver_stride = ALIGN(a.height, 16);
    const size_t frame_size = ALIGN(hor_stride, 64) * ALIGN(ver_stride, 64) * 3 / 2;
    // ★ M5：CBR 上下限窄（±1/16）；VBR/AVBR 下限放宽到 1/16（源码 364～375 行）
    const int bps_min = (a.rc_mode == MPP_ENC_RC_MODE_CBR) ? a.bps * 15 / 16 : a.bps / 16;
    const char* type_name = (a.type == MPP_VIDEO_CodingAVC) ? "h264" : "h265";

    // ---------- 所有变量都在这里声明 ----------
    MppCtx    ctx = nullptr;
    MppApi*   mpi = nullptr;
    MppEncCfg cfg = nullptr;
    MppPollType      timeout     = MPP_POLL_BLOCK;
    MppEncHeaderMode header_mode = MPP_ENC_HEADER_MODE_EACH_IDR;
    FILE*          fp_in   = nullptr;
    FILE*          fp_out  = nullptr;
    MppBufferGroup buf_grp = nullptr;
    MppBuffer      frm_buf = nullptr;
    MppBuffer      pkt_buf = nullptr;
    MppFrame       frame   = nullptr;
    MppPacket      packet  = nullptr;
    uint8_t*       dst     = nullptr;
    size_t         len     = 0;
    bool   frm_eos     = false;
    bool   pkt_eos     = false;
    int    frame_count = 0;
    int    i_count     = 0;                             // ★ M6 I 帧数量
    size_t stream_size = 0;
    std::chrono::steady_clock::time_point t_start;      // ★ M6
    double elapsed = 0;                                 // ★ M6 编码用时（秒）
    double play_sec = 0;                                // ★ M6 视频时长（秒）
    int ret_code = -1;

    printf("config: %s %dx%d rc=%d bps=%d fps=%d gop=%d\n",
           type_name, a.width, a.height, (int)a.rc_mode, a.bps, a.fps, a.gop);

    // ---------- 1. 创建、初始化 ----------
    CHECK(mpp_create(&ctx, &mpi));
    CHECK(mpi->control(ctx, MPP_SET_OUTPUT_TIMEOUT, &timeout));
    CHECK(mpp_init(ctx, MPP_CTX_ENC, a.type));                  // ★ M5 h264 / h265

    // ---------- 2. 配置参数 ----------
    CHECK(mpp_enc_cfg_init(&cfg));
    CHECK(mpi->control(ctx, MPP_ENC_GET_CFG, cfg));

    mpp_enc_cfg_set_s32(cfg, "prep:width",      a.width);
    mpp_enc_cfg_set_s32(cfg, "prep:height",     a.height);
    mpp_enc_cfg_set_s32(cfg, "prep:hor_stride", hor_stride);
    mpp_enc_cfg_set_s32(cfg, "prep:ver_stride", ver_stride);
    mpp_enc_cfg_set_s32(cfg, "prep:format",     MPP_FMT_YUV420SP);

    mpp_enc_cfg_set_s32(cfg, "rc:mode", a.rc_mode);             // ★ M5
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_flex",   0);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_num",    a.fps);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_denom",  1);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_flex",  0);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_num",   a.fps);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_denom", 1);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_target", a.bps);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_max",    a.bps * 17 / 16);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_min",    bps_min);         // ★ M5
    mpp_enc_cfg_set_s32(cfg, "rc:gop", a.gop);

    mpp_enc_cfg_set_s32(cfg, "codec:type", a.type);             // ★ M5
    if (a.type == MPP_VIDEO_CodingAVC) {                        // ★ M5 h264:* 只有 H.264 才设
        mpp_enc_cfg_set_s32(cfg, "h264:profile",  100);
        mpp_enc_cfg_set_s32(cfg, "h264:level",    40);
        mpp_enc_cfg_set_s32(cfg, "h264:cabac_en", 1);
        mpp_enc_cfg_set_s32(cfg, "h264:cabac_idc", 0);
    }

    CHECK(mpi->control(ctx, MPP_ENC_SET_CFG, cfg));
    CHECK(mpi->control(ctx, MPP_ENC_SET_HEADER_MODE, &header_mode));
    printf("encoder ready: %dx%d stride %dx%d\n", a.width, a.height, hor_stride, ver_stride);

    // ---------- 3. 打开文件 ----------
    fp_in = fopen(a.in_path, "rb");
    if (!fp_in) {
        printf("open %s failed\n", a.in_path);
        goto CLEANUP;
    }
    fp_out = fopen(a.out_path, "wb");
    if (!fp_out) {
        printf("open %s failed\n", a.out_path);
        goto CLEANUP;
    }

    // ---------- 4. 申请内存 ----------
    CHECK(mpp_buffer_group_get_internal(&buf_grp, MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE));
    CHECK(mpp_buffer_get(buf_grp, &frm_buf, frame_size));
    CHECK(mpp_buffer_get(buf_grp, &pkt_buf, frame_size));
    dst = (uint8_t*)mpp_buffer_get_ptr(frm_buf);

    // ---------- 5. 取头（H.264：SPS/PPS；H.265：VPS/SPS/PPS） ----------
    CHECK(mpp_packet_init_with_buffer(&packet, pkt_buf));
    mpp_packet_set_length(packet, 0);
    CHECK(mpi->control(ctx, MPP_ENC_GET_HDR_SYNC, packet));
    fwrite(mpp_packet_get_pos(packet), 1, mpp_packet_get_length(packet), fp_out);
    stream_size += mpp_packet_get_length(packet);
    mpp_packet_deinit(&packet);

    // ---------- 6. 编码循环 ----------
    t_start = std::chrono::steady_clock::now();                 // ★ M6 开始计时
    while (!pkt_eos) {
        if (!frm_eos) {
            mpp_buffer_sync_begin(frm_buf);
            frm_eos = !read_nv12_frame(fp_in, dst, a.width, a.height, hor_stride, ver_stride);
            mpp_buffer_sync_end(frm_buf);
        }

        CHECK(mpp_frame_init(&frame));
        mpp_frame_set_width(frame, a.width);
        mpp_frame_set_height(frame, a.height);
        mpp_frame_set_hor_stride(frame, hor_stride);
        mpp_frame_set_ver_stride(frame, ver_stride);
        mpp_frame_set_fmt(frame, MPP_FMT_YUV420SP);
        mpp_frame_set_eos(frame, frm_eos);
        mpp_frame_set_buffer(frame, frm_eos ? nullptr : frm_buf);

        CHECK(mpi->encode_put_frame(ctx, frame));
        mpp_frame_deinit(&frame);

        CHECK(mpi->encode_get_packet(ctx, &packet));
        if (packet) {
            len     = mpp_packet_get_length(packet);
            pkt_eos = mpp_packet_get_eos(packet);

            if (len > 0) {
                // ★ M6：从 packet 的 meta 里看是不是 I 帧
                RK_S32 is_intra = 0;
                if (mpp_packet_has_meta(packet))
                    mpp_meta_get_s32(mpp_packet_get_meta(packet), KEY_OUTPUT_INTRA, &is_intra);
                if (is_intra)
                    i_count++;

                fwrite(mpp_packet_get_pos(packet), 1, len, fp_out);
                stream_size += len;
                printf("frame %-4d %c size %zu\n", frame_count, is_intra ? 'I' : 'P', len);
                frame_count++;
            }
            mpp_packet_deinit(&packet);
        }
    }

    // ---------- ★ M6：7. 统计汇总 ----------
    elapsed  = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count();
    play_sec = (double)frame_count / a.fps;
    printf("\n===== summary =====\n");
    printf("output    : %s (%s)\n", a.out_path, type_name);
    printf("frames    : %d (I frames: %d)\n", frame_count, i_count);
    printf("time      : %.2f s, %.1f fps\n", elapsed, elapsed > 0 ? frame_count / elapsed : 0.0);
    printf("size      : %zu bytes\n", stream_size);
    printf("bitrate   : %.2f Mbps (target %.2f Mbps)\n",
           play_sec > 0 ? stream_size * 8 / play_sec / 1e6 : 0.0, a.bps / 1e6);
    ret_code = 0;

CLEANUP:
    if (frame)   mpp_frame_deinit(&frame);
    if (packet)  mpp_packet_deinit(&packet);
    if (ctx)     mpp_destroy(ctx);
    if (cfg)     mpp_enc_cfg_deinit(cfg);
    if (frm_buf) mpp_buffer_put(frm_buf);
    if (pkt_buf) mpp_buffer_put(pkt_buf);
    if (buf_grp) mpp_buffer_group_put(buf_grp);
    if (fp_in)   fclose(fp_in);
    if (fp_out)  fclose(fp_out);
    return ret_code;
}
```

**运行和验证**：
```bash
# H.264，默认参数
./run_on_board_with_mac.sh -i /userdata/av/in_1080p_60f.nv12 -o /userdata/av/out.h264

# H.265
./run_on_board_with_mac.sh -i /userdata/av/in_1080p_60f.nv12 -o /userdata/av/out.h265 -t h265

# VBR、gop 30
./run_on_board_with_mac.sh -i /userdata/av/in_1080p_60f.nv12 -o /userdata/av/out_vbr.h264 -rc vbr -g 30
```
最后应该打印类似：
```
===== summary =====
output    : /userdata/av/out.h264 (h264)
frames    : 60 (I frames: 1)
time      : x.xx s, xxx.x fps
size      : xxxxxxx bytes
bitrate   : x.xx Mbps (target 4.00 Mbps)
```
- `I frames` 用 ffprobe 对一下：`ffprobe -v error -show_frames out.h264 | grep pict_type | sort | uniq -c`
- 60 帧只有 2 秒，码率控制还没稳定下来，实际码率和目标差一些是正常的；晚上用 300 帧的文件做实验会更准。
- `time` 里包含了读文件的时间，不是纯编码时间。

---

## 9. 跑通后：整理成 `MppEncoder` 类

M4 能正常出片之后，再按 readme 2.2 节的设计拆分：

| `main()` 里的代码段 | 移到哪里 |
|---|---|
| M1 的 create / init / 设参数 / SET_CFG | `MppEncoder::init(const EncoderConfig&)` |
| M2 的 GET_HDR_SYNC | `MppEncoder::get_header(std::vector<uint8_t>&)` |
| 循环里的 frame 设置 + put_frame + get_packet | `MppEncoder::encode(MppBuffer, bool eos, std::vector<uint8_t>&, bool& is_key)` |
| 释放 ctx / cfg | `~MppEncoder()` |
| buffer group、frm_buf、文件读写 | **留在 `main` 里**（Day 3 输入换成摄像头的 DMA-BUF） |

整理完再跑一遍，输出文件应该和整理前**完全一样**（可以用 `md5` 对比）。

---

## 10. 每一步的检查清单

- [ ] M1：打印 `encoder ready: 1920x1080 stride 1920x1088`
- [ ] M2：`xxd out.h264 | head -3` 能看到 `67` 和 `68`
- [ ] M3：`ffprobe -show_frames` 看到 1 帧 `pict_type=I`
- [ ] M4：VLC 播放 2 秒，画面正常，程序正常退出
- [ ] M5：`-t h265` 也能播
- [ ] M6：打印汇总，I 帧数和 ffprobe 一致
- [ ] 整理成类后输出不变
