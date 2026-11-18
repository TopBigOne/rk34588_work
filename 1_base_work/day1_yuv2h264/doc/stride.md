# stride（跨距）

> 从 [笔记_10_02.md](笔记_10_02.md) 拆出来，补上了本项目代码里的用法。
> NV12 在文件和缓冲区里怎么排、怎么逐行读，配图版见 [nv12中yuv分布效果图和读取方式.md](../nv12中yuv分布效果图和读取方式.md)。

---

## 1. 一句话

**stride：内存里一行的起点到下一行起点的字节数。** 硬件要求每行按固定字节数对齐，所以每行末尾可能多出一段填充；行数也可能被补齐。

```
          ←──────── width（有效像素）───────→←─ 填充 ─→
          ┌──────────────────────────────────┬────────┐
  第 0 行 │ Y Y Y Y Y Y Y Y ……               │ ░░░░░░ │
  第 1 行 │ Y Y Y Y Y Y Y Y ……               │ ░░░░░░ │
          │ ……                               │        │
          └──────────────────────────────────┴────────┘
          ←──────────── hor_stride（一行实际占的字节）──────────→
```

---

## 2. 两种 stride

| | 别名 | 含义 | 1920×1080 时 |
|---|---|---|---|
| `hor_stride` | width stride、pitch | 一行占多少**字节**（含行尾填充） | `ALIGN(1920, 16)` = **1920**（本来就是 16 的倍数，没有填充） |
| `ver_stride` | height stride | Y 平面占多少**行**（含补齐的行） | `ALIGN(1080, 16)` = **1088**（多出 8 行） |

- 本来就对齐时，`hor_stride == width`、`ver_stride == height`；没对齐时向上取整。
- **单位是字节，不是像素**：NV12 的 Y 每像素 1 字节，所以两者数值相同；换成 RGB888（每像素 3 字节），`hor_stride` 至少是 `width × 3`。
- 例子：1280×720 → stride 1280×720（都已对齐）；1366×768 → stride 1376×768。

---

## 3. 为什么要对齐

硬件（编码器、RGA、显示）按固定宽度的块读写内存：
- 编码器按 16×16 的宏块处理图像，1080 不是 16 的倍数，最后一行宏块只有 8 行有效 —— 所以缓冲区要补成 1088 行
- DMA 一次搬运的字节数是固定的，每行起点对齐了效率才高

所以缓冲区比图像"大一圈"，多出来的部分是填充，内容不重要。

---

## 4. ver_stride 决定了 UV 从哪里开始

NV12 是先放 Y 平面，再放 UV 平面。UV 的起点是 **Y 平面占的总字节数**：

```c
buf_uv = buf_y + hor_stride * ver_stride;   // 1920 × 1088
```

**UV 从缓冲区的第 1088 行开始，不是第 1080 行。** 而文件里是紧密排列的，UV 从第 1080 行开始 —— 这就是不能整帧 `fread`、必须逐行读的原因。

---

## 5. 逐行读（`read_image` / `ReadYUV::read_nv12_rows`）

每行只从文件读 `width` 个字节，写到缓冲区里 `row * hor_stride` 的位置：

| 平面 | 读几行 | 每行读多少字节 | 写到哪 |
|---|---|---|---|
| Y | `height`（1080） | `width`（1920） | `dst + row * hor_stride` |
| UV | `height / 2`（540） | `width`（1920，U、V 交错） | `dst + hor_stride * ver_stride + row * hor_stride` |

不逐行读（整帧一次 fread）的后果：UV 起点错了 8 行，画面颜色错位，底部发绿。详见 [nv12中yuv分布效果图和读取方式.md](../nv12中yuv分布效果图和读取方式.md) 第 5、8 节。

---

## 6. 两次对齐：16 和 64 是两回事

| | 对齐到 | 用途 | 1920×1080 | 在哪 |
|---|---|---|---|---|
| stride | **16** | 告诉编码器图像怎么排布 | 1920 × 1088 | `mpi_enc_test.c` `test_ctx_init` 160～161 行；本项目 `MppEncoder::init` |
| 缓冲区大小 | **64** | 申请内存时再留余量（和官方 demo 一样） | `ALIGN(1920,64) × ALIGN(1088,64) × 3/2` = 1920 × 1088 × 1.5 = **3,133,440 字节** | `mpi_enc_test.c` 225 行；本项目 `ReadYUV::prepare` |

1920 和 1088 正好都是 64 的倍数，所以这里两次对齐结果一样；换个分辨率（比如 1366 → stride 1376，缓冲区按 1408 算）就不一样了。

---

## 7. 在本项目代码里

| 位置 | 做什么 |
|---|---|
| `src/encoder/mpp_encoder.cpp` `MppEncoder::init` | `horStride = ALIGN(cfg.width, 16)`、`verStride = ALIGN(cfg.height, 16)`；设置 `prep:hor_stride` / `prep:ver_stride` |
| `src/encoder/mpp_encoder.cpp` `MppEncoder::encode` | `mpp_frame_set_hor_stride` / `set_ver_stride` —— **必须和 `prep:*` 一致** |
| `src/source/read_yuv.cpp` `ReadYUV::prepare` | 按 64 对齐算缓冲区大小，申请 DRM 内存 |
| `src/source/read_yuv.cpp` `ReadYUV::read_nv12_rows` | 按 stride 逐行读 |
| `src/pipeline/encode_pipeline.cpp` `run` | `source_.prepare(encoder_.hor_stride(), encoder_.ver_stride())`：输入缓冲区的排布从编码器拿，保证两边一致 |

---

## 8. 自测

- [ ] 1920×1080 的 NV12，为什么 `ver_stride` 是 1088？UV 分量的起始地址在哪里？
- [ ] `frame_size` 为什么再按 64 对齐一次？和 16 对齐有什么区别？
- [ ] stride 的单位是字节还是像素？RGB888 时 `hor_stride` 至少多大？
- [ ] `prep:hor_stride` 和 `mpp_frame_set_hor_stride` 不一致会怎样？
