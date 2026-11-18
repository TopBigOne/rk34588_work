# MppPacket 和 MppFrame 的用法和区别

> 源码依据：`rk_code/external/mpp/inc/mpp_frame.h`、`inc/mpp_packet.h`、`mpp/base/mpp_frame.cpp`、`mpp/base/mpp_packet.cpp`、`mpp/mpp.cpp`。
> 下文的行号都对应 `rk_code/external/mpp` 里的源码。

---

## 1. 一句话区别

- **MppFrame  = 一张没压缩的图**（YUV / RGB 像素），描述"多宽多高、stride 多少、什么格式、像素在哪块内存"
- **MppPacket = 一段压缩后的码流**（H.264 / H.265 字节），描述"数据从哪开始、有多长"

```
              编码（今天）                            解码（Day 2）
  MppFrame ──encode_put_frame──────→┌────────┐     MppPacket ──decode_put_packet───→ ────────┐
  （NV12 图像）                     │ 编码器 │     （H.264 码流）                   │ 解码器 │
  MppPacket ←─encode_get_packet─────└────────┘     MppFrame ←──decode_get_frame─────└────────┘
  （H.264 码流）                                （NV12 图像）
```

**编码：Frame 进、Packet 出；解码：Packet 进、Frame 出。** 两者正好反过来。

---

## 2. 对比表

| | MppFrame | MppPacket |
|---|---|---|
| 装的是什么 | 未压缩的像素（YUV、RGB） | 压缩后的码流（H.264、H.265、JPEG） |
| 大小 | 固定：`hor_stride × ver_stride × 3/2`（NV12） | 不固定：每帧都不一样，I 帧大、P 帧小 |
| 关键描述信息 | 宽、高、`hor_stride`、`ver_stride`、像素格式 | 起始地址 `pos`、长度 `length` |
| 数据放在哪 | **必须**挂一个 `MppBuffer`（硬件内存） | 可以是 `MppBuffer`，也可以是普通内存指针 |
| 创建 | `mpp_frame_init(&frame)` | `mpp_packet_init` / `init_with_buffer` / `new` / `copy_init` |
| 释放 | `mpp_frame_deinit(&frame)` | `mpp_packet_deinit(&packet)` |
| 编码时 | **输入**：你创建 → `encode_put_frame` | **输出**：`encode_get_packet` 拿到 → 你释放 |
| 解码时 | **输出**：`decode_get_frame` 拿到 → 你释放 | **输入**：你创建 → `decode_put_packet` |
| 结束标志 | `mpp_frame_set_eos` / `get_eos` | `mpp_packet_set_eos` / `get_eos` |
| 附加信息 | `mpp_frame_get_meta`（ROI、OSD 等） | `mpp_packet_get_meta`（是否 I 帧、平均 QP 等） |
| 在 FFmpeg 里对应 | `AVFrame` | `AVPacket` |

---

## 3. 两者的共同点

### 3.1 都是"不透明句柄"
```c
typedef void* MppFrame;
typedef void* MppPacket;
```
外部看不到内部结构，只能通过 `mpp_frame_get_xxx` / `set_xxx` 读写。这些函数大部分是用宏批量生成的（`mpp_frame.cpp` 270 行的 `MPP_FRAME_ACCESSORS`），所以 grep 搜不到函数体。

### 3.2 本身都不存数据，数据在 MppBuffer 里
Frame / Packet 只是一张**说明书**，真正的字节在挂着的 `MppBuffer` 里：
```
MppFrame  ──┬── width / height / stride / fmt / eos / pts ...
            └── buffer ──→ MppBuffer（DRM 内存里的 NV12 像素）

MppPacket ──┬── data / size / pos / length / eos / pts ...
            └── buffer ──→ MppBuffer（DRM 内存里的码流）   ← 也可以没有，直接指向普通内存
```

### 3.3 挂 buffer 时都会加引用计数
| 操作 | 对 buffer 的引用计数 | 源码 |
|---|---|---|
| `mpp_frame_set_buffer(frame, buf)` | +1 | `mpp_frame.cpp` 107～122 行 |
| `mpp_frame_deinit(&frame)` | -1 | `mpp_frame.cpp` 76～95 行 |
| `mpp_packet_init_with_buffer(&pkt, buf)` | +1 | `mpp_packet.cpp` 96 行 |
| `mpp_packet_deinit(&pkt)` | -1 | `mpp_packet.cpp` 170～171 行 |

所以 **deinit frame / packet 不会把你自己 `mpp_buffer_get` 来的 buffer 释放掉**，你自己申请的还要自己 `mpp_buffer_put`。这就是 CLEANUP 里既有 `mpp_frame_deinit` 又有 `mpp_buffer_put(frmBuf)` 的原因，不会重复释放。

### 3.4 deinit 之后句柄会被置成 NULL
`mpp_frame_deinit(&frame)`、`mpp_packet_deinit(&packet)` 最后都有 `*frame = NULL` / `*packet = NULL`。所以 CLEANUP 里 `if (packet) mpp_packet_deinit(&packet);` 这种写法是安全的，不会二次释放。

---

## 4. MppFrame 详解

### 4.1 常用字段

| 字段 | set / get | 说明 |
|---|---|---|
| `width` / `height` | `mpp_frame_set_width` / `set_height` | 有效图像的宽高（1920 × 1080） |
| `hor_stride` / `ver_stride` | `mpp_frame_set_hor_stride` / `set_ver_stride` | 内存里每行的字节数 / Y 平面的行数（1920 × 1088） |
| `fmt` | `mpp_frame_set_fmt` | 像素格式，`MPP_FMT_YUV420SP` 就是 NV12 |
| `buffer` | `mpp_frame_set_buffer` / `get_buffer` | 像素数据所在的 `MppBuffer` |
| `eos` | `mpp_frame_set_eos` / `get_eos` | 是不是最后一帧 |
| `pts` / `dts` | `mpp_frame_set_pts` / `get_pts` | 时间戳（推流、封装 MP4 时需要） |
| `meta` | `mpp_frame_get_meta` | 附加信息：输出 packet、ROI、OSD 等 |
| `info_change` | `mpp_frame_get_info_change` | **解码用**：分辨率等信息变了 |
| `errinfo` / `discard` | `mpp_frame_get_errinfo` / `get_discard` | **解码用**：这一帧有错误 / 应该丢弃 |

⚠️ **编码时 frame 的宽、高、stride、格式必须和 `prep:*` 配置一致**，不一致编码器可能报错或者出花屏。

### 4.2 编码时的用法（M3 / M4）

```cpp
MppFrame frame = nullptr;

// 1. 创建
CHECK(mpp_frame_init(&frame));

// 2. 填写"说明书"
mpp_frame_set_width(frame, 1920);
mpp_frame_set_height(frame, 1080);
mpp_frame_set_hor_stride(frame, 1920);
mpp_frame_set_ver_stride(frame, 1088);
mpp_frame_set_fmt(frame, MPP_FMT_YUV420SP);
mpp_frame_set_eos(frame, 0);

// 3. 挂上像素数据（frmBuf 里已经按 stride 逐行读好了，并且 sync_end 过）
mpp_frame_set_buffer(frame, frmBuf);         // frmBuf 引用计数 +1

// 4. 送进编码器
CHECK(mpi->encode_put_frame(ctx, frame));

// 5. 马上释放 frame
mpp_frame_deinit(&frame);                    // frmBuf 引用计数 -1，frmBuf 本身还在，下一帧继续用
```

**为什么 put 完就能马上 deinit、马上往 frmBuf 里写下一帧？**
因为设置了阻塞模式（`MPP_POLL_BLOCK`）。`Mpp::put_frame` 把任务交给编码器后，会**一直等到编码器把这个任务还回来**才返回（`mpp/mpp.cpp` 723～746 行）。返回时编码器已经用完这一帧了。
`mpi_enc_test.c` 840 行的注释也说了：谁创建的资源谁负责释放（resource creator must be the resource destroyer）。

### 4.3 最后一帧：只带 EOS、不带图像

```cpp
CHECK(mpp_frame_init(&frame));
// ... 宽高 stride 格式照样设 ...
mpp_frame_set_eos(frame, 1);           // 告诉编码器：没有了
mpp_frame_set_buffer(frame, nullptr);  // 不挂图像（mpi_enc_test.c 736～737 行）
CHECK(mpi->encode_put_frame(ctx, frame));
mpp_frame_deinit(&frame);
```
编码器收到后会把手里剩下的码流都吐出来，最后一个 packet 带 `eos` 标志。

### 4.4 解码时的用法（Day 2 预习）

解码时 frame 是**解码器给你的**，你负责读和释放：
```cpp
MppFrame frame = nullptr;
CHECK(mpi->decode_get_frame(ctx, &frame));
if (frame) {
    if (mpp_frame_get_info_change(frame)) {
        // 第一次拿到 frame 通常是这个：解码器解析出了分辨率，告诉你"该准备缓冲区了"
        RK_U32 w  = mpp_frame_get_width(frame);
        RK_U32 h  = mpp_frame_get_height(frame);
        RK_U32 hs = mpp_frame_get_hor_stride(frame);
        RK_U32 vs = mpp_frame_get_ver_stride(frame);
        // ... 按需要设置缓冲区 ...
        mpi->control(ctx, MPP_DEC_SET_INFO_CHANGE_READY, NULL);   // mpi_dec_test.c 162 行
    } else if (!mpp_frame_get_errinfo(frame) && !mpp_frame_get_discard(frame)) {
        MppBuffer buf = mpp_frame_get_buffer(frame);
        uint8_t* yuv  = (uint8_t*)mpp_buffer_get_ptr(buf);   // 解码出来的 NV12，带 stride
        // ... 按 stride 逐行写到文件 / 送去显示 ...
    }
    if (mpp_frame_get_eos(frame)) { /* 解码结束 */ }
    mpp_frame_deinit(&frame);    // 用完一定要释放，否则解码器的缓冲区会被占满
}
```
解码出来的 NV12 也是**带 stride 的**，写到文件时要逐行写（`fwrite` 每行写 `width` 字节，源地址每行跳 `hor_stride`），和今天的 `read_nv12_frame` 正好反过来。

---

## 5. MppPacket 详解

### 5.1 四个关键字段：data / size / pos / length

`mpp_packet.h` 38～44 行的说明：

```
data                                                  data + size
 │                                                         │
 ▼                                                         ▼
 ┌─────────────────────────────────────────────────────────┐
 │ ░░░░░░ 已处理 ░░░░░░│▓▓▓▓▓▓▓▓▓ 有效数据 ▓▓▓▓▓▓▓▓│       │
 └─────────────────────────────────────────────────────────┘
                       ▲                            ▲
                      pos                    pos + length
```

| 字段 | 意思 | 用法 |
|---|---|---|
| `data` | 整块内存的起点 | 一般不用管 |
| `size` | 整块内存的总大小（容量） | 一般不用管 |
| **`pos`** | **有效数据从哪开始** | `fwrite` 的源地址：`mpp_packet_get_pos` |
| **`length`** | **有效数据有多长** | `fwrite` 的长度：`mpp_packet_get_length` |

**读码流永远用 `pos` + `length`**，不要用 `data` + `size`：
```cpp
fwrite(mpp_packet_get_pos(packet), 1, mpp_packet_get_length(packet), fpOut);
```

### 5.2 四种创建方式

| 函数 | 做了什么 | 数据拷贝吗 | 用在哪 |
|---|---|---|---|
| `mpp_packet_new(&pkt)` | 创建一个空 packet，什么都没指向 | — | 很少直接用 |
| `mpp_packet_init(&pkt, ptr, size)` | 包装一块**普通内存**：`data = pos = ptr`，`size = length = size` | **不拷贝**，只是指过去 | 解码：把从文件读到的码流包一下送进解码器 |
| `mpp_packet_init_with_buffer(&pkt, buf)` | 包装一个 **MppBuffer**：`data = pos = buffer 地址`，`size = length = buffer 大小`，buffer 引用 +1 | 不拷贝 | 编码：给编码器准备一个装码流的"空容器" |
| `mpp_packet_copy_init(&pkt, src)` | 新建一个 packet，把 `src` 的数据**拷贝**一份 | **拷贝** | 需要保留一份副本时 |

`mpp_packet.h` 28～29 行的注释：
```
mpp_packet_init      = mpp_packet_new + mpp_packet_set_data + mpp_packet_set_size
mpp_packet_copy_init = mpp_packet_init + memcpy
```

⚠️ `mpp_packet_init(&pkt, ptr, size)` **不拷贝**：packet 用完之前，`ptr` 指向的内存不能释放或者被覆盖。

### 5.3 为什么 M2 里一定要 `set_length(0)`

看 `mpp_packet_init_with_buffer` 的实现（`mpp_packet.cpp` 93～94 行）：
```c
p->data = p->pos    = mpp_buffer_get_ptr(buffer);
p->size = p->length = mpp_buffer_get_size(buffer);   // ← length 被设成了整个 buffer 的大小！
```
刚创建出来的 packet，`length` 等于 **整个 buffer 的大小**（3,133,440），意思是"里面已经有 3MB 有效数据了"。
拿它当**输出容器**交给编码器时，编码器会以为里面已经有数据，结果出错。所以要先清零，告诉编码器"这是个空容器"：
```cpp
CHECK(mpp_packet_init_with_buffer(&packet, pktBuf));
mpp_packet_set_length(packet, 0);                       // mpi_enc_test.c 646 行：It is important to clear output packet length!!
CHECK(mpi->control(ctx, MPP_ENC_GET_HDR_SYNC, packet)); // 编码器往里写 SPS/PPS，length 变成 40
```

### 5.4 编码时拿到的 packet（M3 / M4）

```cpp
MppPacket packet = nullptr;
CHECK(mpi->encode_get_packet(ctx, &packet));   // 编码器给你一个 packet
if (packet) {
    void*  ptr = mpp_packet_get_pos(packet);
    size_t len = mpp_packet_get_length(packet);
    fwrite(ptr, 1, len, fpOut);

    RK_U32 eos = mpp_packet_get_eos(packet);   // 编码器说"这是最后一个"

    // 是不是 I 帧：从 packet 的 meta 里取（编码器在 mpp_enc_impl.cpp 里设置的）
    RK_S32 is_intra = 0;
    if (mpp_packet_has_meta(packet))
        mpp_meta_get_s32(mpp_packet_get_meta(packet), KEY_OUTPUT_INTRA, &is_intra);

    mpp_packet_deinit(&packet);                // 用完你负责释放
}
```

**这个 packet 的内存是谁的？** 两种情况：
| 情况 | packet 的内存 |
|---|---|
| 没给 frame 挂输出 packet（我们的简单写法） | 编码器自己分配的（`mpp/codec/mpp_enc_impl.cpp` 1400～1443 行） |
| 用 `mpp_meta_set_packet(meta, KEY_OUTPUT_PACKET, packet)` 挂了自己的 packet（`mpi_enc_test.c` 745～747 行的写法） | 你自己的 `pktBuf` |

两种情况下，拿到 packet 后都要 `mpp_packet_deinit`。

### 5.5 packet 的其他标志（了解）

| 函数 | 意思 |
|---|---|
| `mpp_packet_get_eos` | 最后一个 packet |
| `mpp_packet_is_partition` | 这个 packet 只是一帧的一部分（slice 分片输出，低延迟编码用） |
| `mpp_packet_is_soi` / `is_eoi` | 一帧的第一片 / 最后一片（Start / End Of Image） |
| `mpp_packet_get_pts` / `get_dts` | 时间戳 |

我们没有开分片输出，**一帧就是一个 packet**，所以不用管 partition。

---

## 6. 完整的一帧编码过程：两者怎么配合

```
                     你的程序                                 MPP 编码器
                        │
 ① mpp_buffer_get       │  frmBuf（DRM 内存，3,133,440 字节）
 ② read_nv12_frame      │  按 stride 逐行写入 NV12
 ③ sync_end             │  刷缓存
                        │
 ④ mpp_frame_init       │  frame（说明书）
 ⑤ set 宽高/stride/fmt   │
 ⑥ set_buffer(frmBuf)   │  frame ──→ frmBuf
                        │
 ⑦ encode_put_frame ────┼──────────────────────────→  硬件通过 DMA 读 frmBuf
                        │                            压缩成 H.264
                        │ ←── 任务还回来（阻塞结束）
 ⑧ mpp_frame_deinit     │  frame 没了，frmBuf 还在
                        │
 ⑨ encode_get_packet ←──┼───────────────────────────  packet ──→ 码流内存
 ⑩ fwrite(pos, length)  │  写进 out.h264
 ⑪ mpp_packet_deinit    │
                        │
   下一帧回到 ②，frmBuf 重复使用
```

---

## 7. 常见错误

| 错误 | 后果 |
|---|---|
| frame 的宽高 / stride / 格式和 `prep:*` 不一致 | 编码报错，或者画面错位、花屏 |
| 用 `malloc` 的内存当 frame 的 buffer | `set_buffer` 只收 `MppBuffer`；硬件也访问不了普通内存 |
| 往 frmBuf 写完没 `sync_end` 就 put_frame | 硬件读到缓存里还没刷下去的旧数据，花屏 / 残影 |
| `init_with_buffer` 做输出容器时忘了 `set_length(0)` | 编码器以为容器已满，输出出错（源码注释特别强调） |
| 写码流用 `get_data` + `get_size` | 写出整个容量（3MB 垃圾），而不是实际码流 |
| `mpp_packet_init(&pkt, ptr, size)` 后马上释放 `ptr` | packet 指向的内存失效（init 不拷贝） |
| `encode_get_packet` / `decode_get_frame` 拿到后不 deinit | 内存一直涨；解码时内部缓冲区被占满，解码卡住 |
| 以为 `mpp_frame_deinit` 会释放 frmBuf，就不 `mpp_buffer_put` 了 | frmBuf 泄漏（deinit 只是引用 -1） |
| 解码时忽略 `info_change`，没回 `MPP_DEC_SET_INFO_CHANGE_READY` | 解码器一直等，拿不到真正的图像 |

---

## 8. 一张表记住

| | 编码 | 解码 |
|---|---|---|
| 你**创建**的 | MppFrame（装 NV12） | MppPacket（装码流） |
| 送进去 | `encode_put_frame(ctx, frame)` | `decode_put_packet(ctx, packet)` |
| 你**拿到**的 | MppPacket（码流） | MppFrame（NV12，带 stride） |
| 取出来 | `encode_get_packet(ctx, &packet)` | `decode_get_frame(ctx, &frame)` |
| 拿到后要做 | `fwrite(pos, length)` → `mpp_packet_deinit` | 按 stride 逐行写 → `mpp_frame_deinit` |
| 结束 | 送 `eos=1` 的空 frame，等 `packet` 的 eos | 送 `eos=1` 的 packet，等 `frame` 的 eos |
