# 07 · MPP 核心数据类型

> 作为 [06_本项目中用的mpp_函数.md](06_本项目中用的mpp_函数.md) 和 [mpi_enc_test.cpp中mpp函数分类速查.md](../mpi_enc_test.cpp中mpp函数分类速查.md) 的补充：那两篇讲"函数怎么调"，这篇讲"函数操作的这些东西到底是什么"。
> 源码依据：公开头文件 `rk_code/external/mpp/inc/`，内部实现 `rk_code/external/mpp/mpp/`。
> 返回总览：[../readme.md](../readme.md)

---

## 0. 一张表看全部 11 个类型

| 类型 | 公开定义 | 头文件 | 内部真身 | 一句话 | 比喻 |
|---|---|---|---|---|---|
| **MppCtx** | `typedef void*` | `rk_type.h:130` | `MpiImpl*`（里面包一个 C++ 的 `Mpp` 对象） | **一个编码器 / 解码器实例** | 一台机器 |
| **MppApi** | `struct`（15 个函数指针） | `rk_mpi.h:73` | 全局只有一张表 `mpp_api` | **操作实例的函数表** | 机器的操作面板 |
| **MppCtxType** | `enum` | `rk_type.h:64` | — | **实例当编码器还是解码器** | 机器的工作模式 |
| **MpiCmd** | `enum` | `rk_mpi_cmd.h:59` | — | **`control` 的命令编号** | 面板上的按钮编号 |
| **MppParam** | `typedef void*` | `rk_type.h:131` | 随命令变 | **`control` 的参数** | 按下按钮时附带的东西 |
| **MppEncCfg** | `typedef void*` | `rk_venc_cfg.h:23` | `MppEncCfgImpl*` | **编码参数集合** | 一张订单 |
| **MppBufferGroup** | `typedef void*` | `rk_type.h:137` | `MppBufferGroupImpl*` | **硬件内存池** | 托盘仓库 |
| **MppBuffer** | `typedef void*` | `rk_type.h:136` | `MppBufferImpl*` | **一块硬件内存** | 一个托盘 |
| **MppFrame** | `typedef void*` | `rk_type.h:133` | `MppFrameImpl*` | **一帧未压缩图像的描述** | 贴在托盘上的照片标签 |
| **MppPacket** | `typedef void*` | `rk_type.h:134` | `MppPacketImpl*` | **一段压缩码流的描述** | 成品袋子 |
| **MppMeta** | `typedef void*` | `rk_type.h:140` | `MppMetaImpl*` | **挂在 Frame / Packet 上的附加信息** | 袋子上的便利贴 |

**规律**：除了 `MppApi`（结构体）和 `MppCtxType` / `MpiCmd`（枚举），其余全是 `void*`，也就是**不透明句柄**：
- 你拿到的只是一个指针，看不到里面的字段；
- 只能通过对应的函数读写（`mpp_frame_get_width`、`mpp_packet_get_pos`……）；
- 好处是 MPP 改内部结构不影响你的程序；坏处是**类型检查弱**：`MppFrame` 和 `MppPacket` 都是 `void*`，传反了编译器不会报错。

---

## 1. 它们之间的关系

```
            mpp_create
                │
     ┌──────────┴───────────┐
     ▼                      ▼
  MppCtx ctx            MppApi *mpi ──→ 全局函数表 mpp_api（所有实例共用）
  （一个编码器实例）           │
     ▲                      ├─ control(ctx, MpiCmd, MppParam)
     │ 每次调用都要把 ctx 传回去  ├─ encode_put_frame(ctx, MppFrame)
     └──────────────────────┤─ encode_get_packet(ctx, MppPacket*)
                            └─ reset(ctx) ...
  mpp_init(ctx, MppCtxType, MppCodingType)：定下"编码器 + H.264"

  control 的参数（MppParam = void*）按命令不同，传的东西不同：
    MPP_ENC_SET_CFG      → MppEncCfg
    MPP_ENC_GET_HDR_SYNC → MppPacket
    MPP_SET_OUTPUT_TIMEOUT → MppPollType*

  MppBufferGroup（内存池）
       │ mpp_buffer_get
       ▼
  MppBuffer ◄──── set_buffer ──── MppFrame ──── meta ──→ MppMeta
  （硬件内存）◄─ init_with_buffer ── MppPacket ─── meta ──→ MppMeta
```

---

## 2. MppCtx：一个编码器实例

```c
typedef void* MppCtx;                      // rk_type.h:130
```

**真身**（`mpp/inc/mpi_impl.h:31`）：
```c
struct MpiImpl_t {
    MpiImpl         *check;    // 指向自己，用来检查传进来的 ctx 是不是合法的
    MppCtxType      type;      // 编码 / 解码（mpp_init 时填）
    MppCodingType   coding;    // H.264 / H.265 ...（mpp_init 时填）
    MppApi          *api;      // 指向全局函数表
    Mpp             *ctx;      // 真正干活的 C++ 对象
};
```

**生命周期**：`mpp_create` 创建 → `mpp_init` 定类型 → 用 → `mpp_destroy` 销毁。

**要点**：
- **一个 ctx 就是一路编码**。`mpi_enc_test` 的 `-s 4` 就是开 4 个线程、各建一个 ctx，同时编 4 路。
- 每次调用 `mpi->xxx(ctx, ...)` 都要把 ctx 传进去，告诉函数"操作的是哪一台机器"。

---

## 3. MppApi：操作实例的函数表

```c
typedef struct MppApi_t {                  // rk_mpi.h:73
    RK_U32  size;
    RK_U32  version;
    // 简单数据流接口
    MPP_RET (*decode)(MppCtx ctx, MppPacket packet, MppFrame *frame);
    MPP_RET (*decode_put_packet)(MppCtx ctx, MppPacket packet);
    MPP_RET (*decode_get_frame)(MppCtx ctx, MppFrame *frame);
    MPP_RET (*encode)(MppCtx ctx, MppFrame frame, MppPacket *packet);
    MPP_RET (*encode_put_frame)(MppCtx ctx, MppFrame frame);       // ← 我们用的
    MPP_RET (*encode_get_packet)(MppCtx ctx, MppPacket *packet);   // ← 我们用的
    MPP_RET (*isp)(...);  MPP_RET (*isp_put_frame)(...);  MPP_RET (*isp_get_frame)(...);
    // 高级数据流接口（task 队列）
    MPP_RET (*poll)(MppCtx ctx, MppPortType type, MppPollType timeout);
    MPP_RET (*dequeue)(MppCtx ctx, MppPortType type, MppTask *task);
    MPP_RET (*enqueue)(MppCtx ctx, MppPortType type, MppTask task);
    // 控制接口
    MPP_RET (*reset)(MppCtx ctx);
    MPP_RET (*control)(MppCtx ctx, MpiCmd cmd, MppParam param);    // ← 我们用的
    RK_U32 reserv[16];
} MppApi;
```

| 分组 | 函数 | 什么时候用 |
|---|---|---|
| 编码 | `encode_put_frame` / `encode_get_packet` | ✅ Day 1 |
| | `encode`（put + get 合成一步） | 简单场景，少用 |
| 解码 | `decode_put_packet` / `decode_get_frame` | Day 2 |
| ISP | `isp*` | 头文件注释说"将来支持" |
| 高级 | `poll` / `dequeue` / `enqueue` | 自己管理任务队列，一般用不到 |
| 控制 | `control` / `reset` | ✅ Day 1 |

**为什么用函数表、不直接写成函数？**
`mpp_create` 返回的 `mpi` 指向一张**全局静态表**（`mpp/mpi.cpp:392` 的 `static MppApi mpp_api`），所有实例共用。这是 C 语言模拟"接口"的常见写法，类比 Kotlin 的 `interface` / C++ 的虚函数表：调用方只认这张表，背后的实现可以换。

---

## 4. MppCtxType：编码器还是解码器

```c
typedef enum {                             // rk_type.h:64
    MPP_CTX_DEC,     // 解码器
    MPP_CTX_ENC,     // 编码器   ← 我们用的
    MPP_CTX_ISP,     // ISP（注释：将来支持）
    MPP_CTX_BUTT,    // 无效值（MPP 里 _BUTT 结尾的都是"枚举的末尾"）
} MppCtxType;
```

只在 `mpp_init(ctx, MPP_CTX_ENC, MPP_VIDEO_CodingAVC)` 里用一次，和第三个参数 `MppCodingType`（H.264 / H.265 / MJPEG……）一起**决定这个实例是什么**。`init` 之后就改不了了（M5 里 `-t h265` 不生效就是因为这里写死了）。

---

## 5. MpiCmd：control 的命令编号

```c
MPP_RET (*control)(MppCtx ctx, MpiCmd cmd, MppParam param);
```

`MpiCmd` 是 `rk_mpi_cmd.h` 里的一个大枚举（60 多个命令）。**编号是有结构的**（`rk_mpi_cmd.h:22～26`）：

```
 bit 20～23        bit 16～19         bit 0～15
 ┌──────────┬──────────────────┬──────────────────┐
 │  模块     │  上下文           │  命令序号         │
 │ OSAL=1   │  DEC=1           │                  │
 │ MPP=2    │  ENC=2           │                  │
 │ CODEC=3  │  ISP=3           │                  │
 └──────────┴──────────────────┴──────────────────┘
```

| 命令段 | 起始 | 例子 | 能用在 |
|---|---|---|---|
| `MPP_CMD_BASE` | `0x200000` | `MPP_SET_OUTPUT_TIMEOUT` | 编码、解码都能用 |
| `MPP_DEC_CMD_BASE` | `0x310000` | `MPP_DEC_SET_INFO_CHANGE_READY` | 只给解码器 |
| `MPP_ENC_CMD_BASE` | `0x320000` | `MPP_ENC_SET_CFG`、`MPP_ENC_GET_HDR_SYNC` | 只给编码器 |

所以从名字就能看出用在哪：`MPP_` 通用，`MPP_ENC_` 编码专用，`MPP_DEC_` 解码专用。

**头文件里标了 `deprecated` 的不要用**：比如 `MPP_ENC_SET_PREP_CFG` / `MPP_ENC_SET_RC_CFG` / `MPP_ENC_SET_CODEC_CFG`（`rk_mpi_cmd.h:126～130`），注释写着 "use MPP_ENC_SET_CFG instead"；`MPP_SET_OUTPUT_BLOCK` 也是（M1 时改掉过）。

---

## 6. MppParam：control 的参数

```c
typedef void* MppParam;                    // rk_type.h:131
```

`control` 一个函数要支持 60 多种命令，每种命令要的参数类型都不一样，所以参数只能写成 `void*`，**传什么由命令决定**：

| 命令 | 传什么 | 怎么写 |
|---|---|---|
| `MPP_SET_OUTPUT_TIMEOUT` | `MppPollType` 的地址 | `MppPollType t = MPP_POLL_BLOCK;` → `&t` |
| `MPP_ENC_GET_CFG` / `SET_CFG` | `MppEncCfg`（本身就是指针） | `cfg` |
| `MPP_ENC_SET_HEADER_MODE` | `MppEncHeaderMode` 的地址 | `&header_mode` |
| `MPP_ENC_GET_HDR_SYNC` | `MppPacket`（本身就是指针） | `packet` |
| `MPP_DEC_SET_INFO_CHANGE_READY` | 不需要参数 | `NULL` |

**规律**：参数本来就是句柄（`MppEncCfg`、`MppPacket`）就直接传；是普通变量（枚举、整数）就传**地址**。

⚠️ **类型完全靠自己对，编译器帮不上忙。** 例子：`rk_mpi_cmd.h:76` 的注释写着 `MPP_SET_OUTPUT_TIMEOUT` 的参数是 `RK_S64`，但 MPP 内部实际是这样读的（`mpp/mpp.cpp:1137`）：
```c
MppPollType timeout = (param) ? *((MppPollType *)param) : MPP_POLL_NON_BLOCK;
```
按 `MppPollType`（4 字节）读。所以我们传 `MppPollType` 的地址是对的，**头文件的注释反而过时了**。拿不准的时候，以 `mpp.cpp` 里 `control` 的实现为准。

---

## 7. MppEncCfg：编码参数集合

```c
typedef void* MppEncCfg;                   // rk_venc_cfg.h:23
```
**真身**（`mpp/base/inc/mpp_enc_cfg_impl.h`）：
```c
typedef struct MppEncCfgImpl_t {
    RK_S32        size;
    MppEncCfgSet  cfg;      // 一个大结构体：prep / rc / codec / h264 / h265 / split ...
} MppEncCfgImpl;
```

**按字符串设参数是怎么实现的**：`mpp_enc_cfg_set_s32(cfg, "rc:gop", 60)` 先用 `"rc:gop"` 在一张查找表里找到它对应 `MppEncCfgSet` 的哪个字段、什么类型，再写进去（`mpp/base/mpp_enc_cfg.cpp:478～494`）。找不到就返回 `MPP_NOK`，这就是 `CFG_SET` 能查出拼写错误的原因。

**生命周期**：`mpp_enc_cfg_init` → `GET_CFG` 填默认值 → `set_*` 修改 → `SET_CFG` 交给编码器 → `mpp_enc_cfg_deinit`。
它是一个**独立对象**，不属于 ctx，所以要自己 `deinit`。

---

## 8. MppBufferGroup：硬件内存池

```c
typedef void* MppBufferGroup;              // rk_type.h:137
```
**真身** `MppBufferGroupImpl`（`mpp/base/inc/mpp_buffer_impl.h`）里的关键字段：`type`（什么内存）、`mode`（内部 / 外部）、`limit_size` / `limit_count`（限额）、`usage`（已用多少）、`buffer_count`（有几块）。

**两个维度**：
| 维度 | 取值 | 意思 |
|---|---|---|
| **内存类型** `MppBufferType` | `NORMAL` | 普通 malloc，只用于测试，硬件用不了 |
| | `ION` | 老的安卓 / Linux 内存分配方式 |
| | `EXT_DMA` | **外部导入**的 DMA-BUF（摄像头给的） |
| | `DRM` ← 我们用的 | 通过 DRM 设备分配，硬件能直接访问 |
| | `DMA_HEAP` | 较新内核（5.10+）的分配方式 |
| **标志位**（和类型按位或） | `MPP_BUFFER_FLAGS_CACHABLE` ← 我们用的 | CPU 读写走缓存（快，但写完要 `sync_end`） |
| | `MPP_BUFFER_FLAGS_CONTIG` | 物理连续（CMA） |
| **模式** `MppBufferMode` | `INTERNAL` ← 我们用的 | 内存由 MPP 分配（`mpp_buffer_group_get_internal`） |
| | `EXTERNAL` | 内存由外部提供，MPP 只是登记一下（`mpp_buffer_group_get_external`，Day 3 接摄像头会用） |

`mpp_buffer.h:125` 的注释：5.10 以上内核，MPP 默认优先选 `DMA_HEAP > DRM > ION`。

---

## 9. MppBuffer：一块硬件内存

```c
typedef void* MppBuffer;                   // rk_type.h:136
```
**真身** `MppBufferImpl` 的关键字段：
```c
MppBufferInfo info;       // type / size / ptr（CPU 地址）/ hnd / fd（DMA-BUF 文件描述符）/ index
RK_U32        group_id;   // 属于哪个池
RK_U32        uncached;   // 是不是不走缓存
RK_S32        ref_count;  // 引用计数
```

**常用读取函数**（都是宏，展开后带 `_with_caller` / `_f`）：
| 函数 | 拿到什么 | 用途 |
|---|---|---|
| `mpp_buffer_get_ptr(buf)` | CPU 地址 | CPU 读写像素（M3 的 `dst`） |
| `mpp_buffer_get_size(buf)` | 大小 | |
| `mpp_buffer_get_fd(buf)` | **DMA-BUF 的 fd** | 把这块内存交给别的硬件（RGA、显示、摄像头），**零拷贝**，Day 3 会用 |

**引用计数**：`mpp_buffer_get` 后是 1；`mpp_frame_set_buffer` / `mpp_packet_init_with_buffer` 各 +1；对应的 `deinit` 各 -1；`mpp_buffer_put` -1；**减到 0 才真正还给内存池**。所以 frame 先 deinit、你最后 put，顺序不会出错。

---

## 10. MppFrame 和 MppPacket

详见 [MppPacket和MppFrame的用法和区别.md](../MppPacket和MppFrame的用法和区别.md)，这里只补充"真身"：

| | MppFrame（`MppFrameImpl`） | MppPacket（`MppPacketImpl`） |
|---|---|---|
| 描述 | `width` `height` `hor_stride` `ver_stride` `fmt` | `data` `pos` `size` `length` |
| 时间 | `pts` `dts` | `pts` `dts` |
| 标志 | `eos` `info_change` `errinfo` `discard` | `flag`（eos 等）、`status` |
| 数据 | `buffer`（MppBuffer） | `buffer`（MppBuffer，可以为空，直接指普通内存） |
| 附加 | `meta`（MppMeta） | `meta`（MppMeta）、`segments`（分片信息） |

两者**本身都不装像素 / 码流**，只是"说明书 + 指向 MppBuffer 的指针"。

---

## 11. MppMeta：附加信息口袋

```c
typedef void* MppMeta;                     // rk_type.h:140
```
**真身**（`mpp/base/inc/mpp_meta_impl.h`）：
```c
typedef struct MppMetaImpl_t {
    ...
    RK_S32      ref_count;
    RK_S32      node_count;    // 现在装了几个值
    MppMetaVal  vals[];        // 每个 key 一个格子
} MppMetaImpl;
```

**key**：`MppMetaKey` 枚举，一共 30 个（`mpp_meta.h:60`），每个 key 是 4 个字母拼成的编号，比如 `KEY_OUTPUT_INTRA = FOURCC_META('o','i','d','r')`。**每个 key 绑定了一种值类型**。

**按值类型分的读写函数**：
| 值类型 | set | get | 例子 |
|---|---|---|---|
| 整数 | `mpp_meta_set_s32` / `s64` | `mpp_meta_get_s32` / `s64` | `KEY_OUTPUT_INTRA`、`KEY_ENC_AVERAGE_QP` |
| 指针 | `mpp_meta_set_ptr` | `mpp_meta_get_ptr` | `KEY_USER_DATA`、`KEY_OSD_DATA` |
| Frame | `mpp_meta_set_frame` | `mpp_meta_get_frame` | `KEY_INPUT_FRAME`、`KEY_OUTPUT_FRAME` |
| Packet | `mpp_meta_set_packet` | `mpp_meta_get_packet` | `KEY_OUTPUT_PACKET` |
| Buffer | `mpp_meta_set_buffer` | `mpp_meta_get_buffer` | `KEY_MOTION_INFO` |

带 `_d` 后缀的（如 `mpp_meta_get_s32_d`）多一个默认值参数：取不到就返回默认值。

⚠️ **两个坑**（`mpp/base/mpp_meta.cpp:310～328`）：
1. **key 和函数的类型必须对上**：`KEY_OUTPUT_FRAME` 绑定的是 Frame 类型，用 `mpp_meta_get_s32` 去取，查表时类型对不上，直接返回 `MPP_NOK`。M6 里 I 帧数一直是 0 就是这个原因。
2. **get 是"取走"，不是"看一眼"**：取成功后这个格子会被标成无效（`META_VAL_VALID | META_VAL_READY → META_VAL_INVALID`），**同一个 key 第二次取会失败**。要多处用，就取一次存到自己的变量里。

---

## 12. 谁创建、谁释放

| 类型 | 创建 | 释放 | 谁负责 |
|---|---|---|---|
| MppCtx | `mpp_create` | `mpp_destroy` | 你 |
| MppApi | `mpp_create` 顺带返回 | **不用释放**（全局静态表） | — |
| MppEncCfg | `mpp_enc_cfg_init` | `mpp_enc_cfg_deinit` | 你 |
| MppBufferGroup | `mpp_buffer_group_get_internal` | `mpp_buffer_group_put` | 你 |
| MppBuffer | `mpp_buffer_get` | `mpp_buffer_put` | 你（引用计数到 0 才真正释放） |
| MppFrame（编码输入） | `mpp_frame_init` | `mpp_frame_deinit` | 你 |
| MppFrame（解码输出） | `decode_get_frame` 给你 | `mpp_frame_deinit` | 你 |
| MppPacket（编码输出） | `encode_get_packet` 给你 | `mpp_packet_deinit` | 你 |
| MppPacket（自己包的） | `mpp_packet_init_with_buffer` | `mpp_packet_deinit` | 你 |
| MppMeta | 随 Frame / Packet 自动创建 | 随 Frame / Packet 自动释放 | **不用管** |
| MppCtxType / MpiCmd | 枚举值 | — | — |
| MppParam | 指向你自己的变量 | 看那个变量 | — |

---

## 13. 和 FFmpeg 对照

| MPP | FFmpeg | 说明 |
|---|---|---|
| `MppCtx` + `MppApi` | `AVCodecContext` + `AVCodec` | 实例 + 操作 |
| `MppCtxType` + `MppCodingType` | `avcodec_find_encoder(AV_CODEC_ID_H264)` | 选编码器 / 解码器和格式 |
| `MpiCmd` + `MppParam` | `av_opt_set` / 直接改结构体字段 | 设置和控制 |
| `MppEncCfg`（字符串 key） | `AVCodecContext` 字段 / `AVDictionary` | 编码参数 |
| `MppFrame` | `AVFrame` | 未压缩图像 |
| `MppPacket` | `AVPacket` | 压缩码流 |
| `MppBuffer` | `AVBufferRef` | 带引用计数的内存 |
| `MppBufferGroup` | `AVBufferPool` | 内存池 |
| `MppMeta` | `AVFrameSideData` / `AVPacketSideData` | 附加信息 |
