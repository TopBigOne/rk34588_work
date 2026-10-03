# main.cpp 中 MPP 函数分类速查

> 📘 本文用到的 MppCtx、MppApi、MpiCmd、MppParam、MppEncCfg、MppBuffer、MppFrame、MppPacket、MppMeta 等类型是什么，见补充说明 [07_MPP核心数据类型.md](07_MPP核心数据类型.md)。

> `main.cpp` 一共用到了 **34 个** MPP 函数 / 接口，按"它在管什么"分成 7 类。
> 行号对应提交 `9d4841f` 的 `main.cpp`；函数原型来自 `rk_code/external/mpp/inc/` 下的头文件。
> 相关文档：[MppPacket和MppFrame的用法和区别.md](../MppPacket和MppFrame的用法和区别.md)、[nv12中yuv分布效果图和读取方式.md](../nv12中yuv分布效果图和读取方式.md)

---

## 0. 先看全貌：7 类函数，各管一件事

| # | 类别 | 管什么 | 函数前缀 / 写法 | 个数 |
|:---:|---|---|---|:---:|
| 1 | **编码器实例** | 创建、初始化、销毁编码器本身 | `mpp_create` / `mpp_init` / `mpp_destroy` | 3 |
| 2 | **控制命令** | 给编码器发指令：设参数、取头、设阻塞模式 | `mpi->control(ctx, 命令, 参数)` | 1（5 种命令） |
| 3 | **编码参数** | 准备一张"参数订单" | `mpp_enc_cfg_*` | 3 |
| 4 | **硬件内存** | 申请、使用、刷缓存、释放 DRM 内存 | `mpp_buffer_*` | 7 |
| 5 | **图像（输入）** | 描述一帧没压缩的图 | `mpp_frame_*` | 9 |
| 6 | **码流（输出）** | 描述一段压缩后的码流 | `mpp_packet_*` | 8 |
| 7 | **送帧 / 取包** | 真正让硬件干活 | `mpi->encode_put_frame` / `encode_get_packet` | 2 |
| + | **附加信息** | 从 packet 里取额外信息（是不是 I 帧） | `mpp_meta_get_s32` | 1 |

**记忆方法**：前缀就是类别。`mpp_buffer_` 管内存，`mpp_frame_` 管图像，`mpp_packet_` 管码流，`mpp_enc_cfg_` 管参数，`mpi->` 是对编码器本身的操作。

---

## 1. 编码器实例：`mpp_create` / `mpp_init` / `mpp_destroy`

| 函数 | 原型 | 作用 | main.cpp |
|---|---|---|:---:|
| `mpp_create` | `MPP_RET mpp_create(MppCtx *ctx, MppApi **mpi)` | 创建一个 MPP 实例，拿到两样东西：`ctx`（句柄，代表"这一个编码器"）和 `mpi`（函数表，后面的 `control` / `encode_put_frame` 都通过它调用） | 174 |
| `mpp_init` | `MPP_RET mpp_init(MppCtx ctx, MppCtxType type, MppCodingType coding)` | 把实例初始化成**编码器**（`MPP_CTX_ENC`）还是解码器，以及**编什么格式**（`MPP_VIDEO_CodingAVC` = H.264，`MPP_VIDEO_CodingHEVC` = H.265） | 177 |
| `mpp_destroy` | `MPP_RET mpp_destroy(MppCtx ctx)` | 销毁实例，释放编码器内部的所有资源 | 319 |

```cpp
MppCtx  ctx = nullptr;
MppApi *mpi = nullptr;
CHECK(mpp_create(&ctx, &mpi));                    // ① 创建
CHECK(mpi->control(ctx, MPP_SET_OUTPUT_TIMEOUT, &timeout));   // 要在 init 之前
CHECK(mpp_init(ctx, MPP_CTX_ENC, a.type));        // ② 决定是 H.264 还是 H.265
...
mpp_destroy(ctx);                                 // ③ 用完销毁
```

⚠️ **H.264 还是 H.265 是在 `mpp_init` 这一步定死的**，后面再设 `codec:type` 也改不过来（M5 踩过的坑）。

---

## 2. 控制命令：`mpi->control(ctx, 命令, 参数)`

`control` 类似 Linux 的 `ioctl`：**一个函数，靠第二个参数决定干什么事**。

```cpp
MPP_RET (*control)(MppCtx ctx, MpiCmd cmd, MppParam param);   // MppParam 就是 void*
```

| 命令 | 参数 | 作用 | 什么时候调 | main.cpp |
|---|---|---|---|:---:|
| `MPP_SET_OUTPUT_TIMEOUT` | `MppPollType*`，填 `MPP_POLL_BLOCK` | 让 `encode_get_packet` **阻塞**等结果（不设的话可能立刻返回空） | `mpp_init` **之前** | 176 |
| `MPP_ENC_GET_CFG` | `MppEncCfg` | 把编码器**当前的默认参数**读到 cfg 里 | `mpp_enc_cfg_init` 之后 | 180 |
| `MPP_ENC_SET_CFG` | `MppEncCfg` | 把 cfg 里的参数**交给编码器，真正生效** | 设完所有参数之后 | 215 |
| `MPP_ENC_SET_HEADER_MODE` | `MppEncHeaderMode*`，填 `MPP_ENC_HEADER_MODE_EACH_IDR` | 每个 IDR 帧前面都自动带上 SPS/PPS（播放器中途打开也能解码） | `SET_CFG` 之后 | 217 |
| `MPP_ENC_GET_HDR_SYNC` | `MppPacket`（先 `set_length(0)`） | 把 SPS/PPS（H.265 还有 VPS）写进 packet，同步返回 | 开始编码之前 | 234 |

命名规律：`MPP_SET_` / `MPP_` 开头是通用命令（编码解码都能用），`MPP_ENC_` 开头是编码专用，`MPP_DEC_` 开头是解码专用（Day 2 会用到 `MPP_DEC_SET_INFO_CHANGE_READY`）。

---

## 3. 编码参数：`mpp_enc_cfg_*`

| 函数 | 原型 | 作用 | main.cpp |
|---|---|---|:---:|
| `mpp_enc_cfg_init` | `MPP_RET mpp_enc_cfg_init(MppEncCfg *cfg)` | 创建一个空的参数对象 | 179 |
| `mpp_enc_cfg_set_s32` | `MPP_RET mpp_enc_cfg_set_s32(MppEncCfg cfg, const char *name, RK_S32 val)` | 按**字符串名字**设一个整数参数（用了 21 次） | 182～211 |
| `mpp_enc_cfg_deinit` | `MPP_RET mpp_enc_cfg_deinit(MppEncCfg cfg)` | 释放参数对象 | 316 |

**标准用法：先读默认值，再改，再提交**
```cpp
CHECK(mpp_enc_cfg_init(&cfg));                    // 1. 建一张空订单
CHECK(mpi->control(ctx, MPP_ENC_GET_CFG, cfg));   // 2. 填上默认值
mpp_enc_cfg_set_s32(cfg, "prep:width", 1920);     // 3. 改自己关心的
...
CHECK(mpi->control(ctx, MPP_ENC_SET_CFG, cfg));   // 4. 交给编码器（不调这句，前面全白设）
```

**main.cpp 里设置的 key**（名字是"模块:参数"）：

| 模块 | key | 值 | 意思 |
|---|---|---|---|
| `prep:` 输入图像 | `width` / `height` | 1920 / 1080 | 有效图像大小 |
| | `hor_stride` / `ver_stride` | 1920 / 1088 | 内存里一行多少字节 / Y 平面多少行 |
| | `format` | `MPP_FMT_YUV420SP` | NV12 |
| `rc:` 码率控制 | `mode` | `MPP_ENC_RC_MODE_CBR` / `VBR` / `AVBR` | 码率模式 |
| | `fps_in_*` / `fps_out_*` | 30 / 1，`flex = 0` | 输入、输出帧率，固定帧率 |
| | `bps_target` / `bps_max` / `bps_min` | 4M / 4M×17/16 / CBR 4M×15/16、VBR 4M/16 | 目标码率和上下限 |
| | `gop` | 60（fps × 2） | 每 60 帧一个 I 帧 |
| `codec:` | `type` | `MPP_VIDEO_CodingAVC` / `HEVC` | 编码格式 |
| `h264:` 只对 H.264 | `profile` | 100 | High Profile |
| | `level` | 40 | Level 4.0 |
| | `cabac_en` / `cabac_idc` | 1 / 0 | 开 CABAC 熵编码 / 初始化表编号 |

⚠️ key 是字符串，**拼错了编译器发现不了**。拼错时 `mpp_enc_cfg_set_s32` 会返回 `MPP_NOK`（`mpp_enc_cfg.cpp` 487～489 行），但 main.cpp 没有检查它的返回值，所以会悄悄失效。

---

## 4. 硬件内存：`mpp_buffer_*`

按"申请 → 使用 → 释放"的顺序：

| 阶段 | 函数 | 原型（简化） | 作用 | main.cpp |
|---|---|---|---|:---:|
| **申请** | `mpp_buffer_group_get_internal` | `(MppBufferGroup *group, MppBufferType type)` | 建一个**内存池**。`MPP_BUFFER_TYPE_DRM`：硬件能访问的内存；`MPP_BUFFER_FLAGS_CACHABLE`：CPU 读写走缓存（快，但要手动刷） | 228 |
| | `mpp_buffer_get` | `(MppBufferGroup group, MppBuffer *buffer, size_t size)` | 从池子里**拿一块**指定大小的内存 | 229、230 |
| **使用** | `mpp_buffer_get_ptr` | `void* (MppBuffer buffer)` | 拿到这块内存的 **CPU 地址**，CPU 才能往里写 | 248 |
| | `mpp_buffer_sync_begin` | `(MppBuffer buffer)` | **CPU 开始读写之前**调用 | 253 |
| | `mpp_buffer_sync_end` | `(MppBuffer buffer)` | **CPU 写完之后**调用：把缓存里的数据刷到内存，硬件才看得到 | 255 |
| **释放** | `mpp_buffer_put` | `(MppBuffer buffer)` | 还回一块内存（引用计数 -1，到 0 才真正释放） | 322、325 |
| | `mpp_buffer_group_put` | `MPP_RET (MppBufferGroup group)` | 销毁内存池 | 329 |

```cpp
CHECK(mpp_buffer_group_get_internal(&bufGrp, MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE));
CHECK(mpp_buffer_get(bufGrp, &frmBuf, frameSize));          // 3,133,440 字节
dst = (uint8_t*)mpp_buffer_get_ptr(frmBuf);

mpp_buffer_sync_begin(frmBuf);                               // CPU 要写了
read_nv12_frame(fpIn, dst, ...);                             // 逐行写入
mpp_buffer_sync_end(frmBuf);                                 // 写完，刷缓存

mpp_buffer_put(frmBuf);                                      // 用完还回去
mpp_buffer_group_put(bufGrp);                                // 最后销毁池子
```

**为什么不能用 `malloc`**：硬件编码器通过 DMA 直接读内存，只能访问 DRM 这类"硬件可见"的内存。
**这几个其实是宏**：`mpp_buffer_get` → `mpp_buffer_get_with_tag`，`mpp_buffer_sync_begin` → `mpp_buffer_sync_begin_f`……（`mpp_buffer.h` 233～290 行）。所以板子上出厂的旧库缺 `mpp_buffer_sync_begin_f` 时，报的是这个带 `_f` 的名字。

---

## 5. 图像（输入）：`mpp_frame_*`

`MppFrame` 是贴在图像缓冲区上的**标签**：写清楚多宽多高、什么格式、像素在哪。

| 函数 | 作用 | main.cpp |
|---|---|:---:|
| `mpp_frame_init(&frame)` | 创建一个空 frame | 261 |
| `mpp_frame_set_width(frame, w)` | 有效宽度 1920 | 262～ |
| `mpp_frame_set_height(frame, h)` | 有效高度 1080 | |
| `mpp_frame_set_hor_stride(frame, hs)` | 每行字节数 1920 | |
| `mpp_frame_set_ver_stride(frame, vs)` | Y 平面行数 1088 | |
| `mpp_frame_set_fmt(frame, fmt)` | 像素格式 `MPP_FMT_YUV420SP`（NV12） | |
| `mpp_frame_set_eos(frame, eos)` | 是不是最后一帧（1 = 我们告诉编码器"没有了"） | |
| `mpp_frame_set_buffer(frame, buf)` | 挂上像素数据所在的 `MppBuffer`（引用计数 +1）；最后的 EOS 帧挂 `nullptr` | 269 |
| `mpp_frame_deinit(&frame)` | 释放 frame（挂着的 buffer 引用计数 -1，**buffer 本身还在**），并把 `frame` 置成 NULL | 271、310 |

⚠️ 宽、高、两个 stride、格式这 5 个值**必须和 `prep:*` 参数一致**。
这些 `set_xxx` 都是 `mpp_frame.cpp` 270 行的宏 `MPP_FRAME_ACCESSORS` 批量生成的，所以 grep 搜不到函数体。

---

## 6. 码流（输出）：`mpp_packet_*`

| 函数 | 作用 | main.cpp |
|---|---|:---:|
| `mpp_packet_init_with_buffer(&pkt, buf)` | 用一块 `MppBuffer` 包一个 packet，当**输出容器**（buffer 引用计数 +1） | 232 |
| `mpp_packet_set_length(pkt, 0)` | 把有效长度清零。上一个函数会把 `length` 设成整个 buffer 的大小，不清零编码器会以为容器是满的 | 233 |
| `mpp_packet_get_pos(pkt)` | 有效数据**从哪开始**（`fwrite` 的源地址） | 235、287 |
| `mpp_packet_get_length(pkt)` | 有效数据**有多长**（`fwrite` 的长度） | 235、236、275 |
| `mpp_packet_get_eos(pkt)` | 是不是最后一个包（编码器告诉我们"码流全给你了"） | 277 |
| `mpp_packet_has_meta(pkt)` | 这个包有没有附加信息 | 281 |
| `mpp_packet_get_meta(pkt)` | 取出附加信息（`MppMeta`） | 282 |
| `mpp_packet_deinit(&pkt)` | 释放 packet，并把 `pkt` 置成 NULL | 237、293、313 |

```cpp
// 写码流的固定写法：永远用 pos + length，不用 data + size
fwrite(mpp_packet_get_pos(packet), 1, mpp_packet_get_length(packet), fpOut);
```

### 附加信息：`mpp_meta_get_s32`
```cpp
RK_S32 isIntra = 0;
if (mpp_packet_has_meta(packet))
    mpp_meta_get_s32(mpp_packet_get_meta(packet), KEY_OUTPUT_INTRA, &isIntra);   // 1 = I 帧
```
原型：`MPP_RET mpp_meta_get_s32(MppMeta meta, MppMetaKey key, RK_S32 *val)`（main.cpp 282 行）。
⚠️ 是 `KEY_OUTPUT_INTRA`，不是 `KEY_OUTPUT_FRAME`（M6 踩过的坑：后者存的是 frame 对象，取不出整数）。

---

## 7. 送帧 / 取包：真正让硬件干活

| 函数 | 原型 | 作用 | main.cpp |
|---|---|---|:---:|
| `mpi->encode_put_frame` | `MPP_RET (*)(MppCtx ctx, MppFrame frame)` | 把一帧图像**送进**编码器。阻塞模式下，等编码器用完这一帧才返回，所以返回后可以马上 `frame_deinit`、往 `frmBuf` 写下一帧 | 270 |
| `mpi->encode_get_packet` | `MPP_RET (*)(MppCtx ctx, MppPacket *packet)` | 从编码器**取出**一个压缩好的 packet（设了 `MPP_POLL_BLOCK` 就阻塞等）。拿到的 packet 由你负责 `deinit` | 273 |

```cpp
CHECK(mpi->encode_put_frame(ctx, frame));   // 送进去
mpp_frame_deinit(&frame);
CHECK(mpi->encode_get_packet(ctx, &packet)); // 取出来
// ... fwrite ...
mpp_packet_deinit(&packet);
```

---

## 8. 申请和释放一一对应

每个"申请"都要有一个对应的"释放"，`CLEANUP` 里倒着来：

| 申请 | 释放 | CLEANUP 行号 |
|---|---|:---:|
| `mpp_frame_init` | `mpp_frame_deinit` | 310 |
| `mpp_packet_init_with_buffer` / `encode_get_packet` 拿到的 | `mpp_packet_deinit` | 313 |
| `mpp_enc_cfg_init` | `mpp_enc_cfg_deinit` | 316 |
| `mpp_create` | `mpp_destroy` | 319 |
| `mpp_buffer_get` | `mpp_buffer_put` | 322、325 |
| `mpp_buffer_group_get_internal` | `mpp_buffer_group_put` | 329 |

**命名规律**：`init` ↔ `deinit`，`create` ↔ `destroy`，`get` ↔ `put`。看到左边的，就要想到右边的。

**不需要释放的**：`mpp_buffer_get_ptr` 返回的地址、`mpp_packet_get_pos` 返回的地址、`mpp_packet_get_meta` 返回的 meta，它们都属于别的对象，跟着那个对象一起释放。

---

## 9. 按程序执行顺序串起来

```
【准备】
 mpp_create ─→ control(SET_OUTPUT_TIMEOUT) ─→ mpp_init                      ① 编码器实例
 mpp_enc_cfg_init ─→ control(GET_CFG) ─→ set_s32 × 21 ─→ control(SET_CFG)   ③ 参数
 control(SET_HEADER_MODE)                                                    ② 控制命令
 mpp_buffer_group_get_internal ─→ mpp_buffer_get × 2                        ④ 内存
 mpp_packet_init_with_buffer ─→ set_length(0) ─→ control(GET_HDR_SYNC)
   ─→ fwrite(get_pos, get_length) ─→ mpp_packet_deinit                       ⑥ 写 SPS/PPS
 mpp_buffer_get_ptr

【每一帧】 while (!pktEos)
 sync_begin ─→ read_nv12_frame ─→ sync_end                                  ④ 内存
 mpp_frame_init ─→ set_width/height/stride/fmt/eos/buffer                   ⑤ 图像
 encode_put_frame ─→ mpp_frame_deinit                                       ⑦ 送
 encode_get_packet ─→ get_length / get_eos / meta ─→ fwrite ─→ packet_deinit ⑦⑥ 取

【收尾】 CLEANUP
 frame_deinit ─→ packet_deinit ─→ enc_cfg_deinit ─→ mpp_destroy
   ─→ buffer_put × 2 ─→ buffer_group_put ─→ fclose × 2
```

---

## 10. 返回值：`MPP_RET`

几乎所有函数都返回 `MPP_RET`（定义在 `mpp_err.h`）：
- `MPP_OK`（0）：成功
- 负数：失败，比如 `MPP_NOK`（-1）、`MPP_ERR_NULL_PTR`、`MPP_ERR_VALUE`

main.cpp 用 `CHECK(...)` 包起来：不等于 `MPP_OK` 就打印出错的那一句和返回值，然后 `goto CLEANUP`。
**不返回 `MPP_RET` 的**（返回 `void` 或数值）：`mpp_frame_set_*`、`mpp_packet_set_length`、`mpp_packet_get_*`、`mpp_buffer_get_ptr`，这些不需要 `CHECK`。
