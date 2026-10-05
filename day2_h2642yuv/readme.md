# Day 2 · MPP 硬件解码：`day2_h2642yuv`

> **一句话目标**：读一个 H.264 文件 → 用 RK3588 的 MPP 硬件解码成 NV12 → 在 Mac 上用 ffplay 能正常播放，并测出解码速度
> **总时间**：约 8 小时（准备 0.5h + 读代码 3h + 写代码 4h + 实验 1h）
> **和 Day 1 的关系**：Day 1 是 NV12 → H.264，Day 2 反过来。MppCtx / control / MppPacket / MppFrame / MppBuffer / EOS 全部复用，新东西只有 **info change** 和**怎么给解码器提供内存**

---

## 1. 这个程序在做什么

```
 Mac                              RK3588 开发板
 ─────────────────────            ─────────────────────────────────────────────────────────
 aaa.264（55.7MB，1803 帧）
   │ adb push
   ▼
                                  /userdata/av/aaa.264                  （H.264 裸流）
                                           │ ① 每次读 64KB（不管帧边界），包成 MppPacket
                                           ▼
                                   ┌──────────────────┐
                                   │ MPP 硬件解码器      │ ② MPP 内部自己切帧（split_parse），
                                   │ （RK3588 的 VPU）   │    解码成 NV12，放进我们给的内存池
                                   └──────────────────┘
                                           │ ③ 每拿到一帧 MppFrame，按 width × height 逐行写出（去掉 stride 填充）
                                           ▼
 ffplay 播放 ←──────adb pull──────  /userdata/av/out.nv12                （原始 NV12，每帧 3,110,400 字节）
```

| | 输入 | 输出 |
|---|---|---|
| 文件 | `aaa.264` | `out.nv12` |
| 内容 | 压缩后的 H.264 码流 | 没压缩的 YUV 像素（NV12） |
| 大小 | 平均每帧约 31KB | 每帧固定 1920 × 1080 × 3/2 = 3,110,400 字节 |
| MPP 里叫 | `MppPacket` | `MppFrame`（装在 `MppBuffer` 里） |

⚠️ **全部 1803 帧解码出来是 5.6GB**，板子 `/userdata` 和 adb pull 都吃不消。写文件时用 `-n` 限制帧数（比如 `-n 60`，约 187MB）；测速度时不写文件。

### 和 Day 1 对照

| | Day 1 编码 | Day 2 解码 |
|---|---|---|
| 方向 | `MppFrame` → `MppPacket` | `MppPacket` → `MppFrame` |
| 送 / 取 | `encode_put_frame` / `encode_get_packet` | `decode_put_packet` / `decode_get_frame` |
| 参数配置 | `MppEncCfg`：宽高、码率、GOP…… 一大堆 | `MppDecCfg`：基本只设 `base:split_parse` —— 宽高从码流里读 |
| 内存谁申请 | 我们申请输入帧缓冲（1 块） | 解码器告诉我们要多大（**info change**），我们建内存池，解码器自己从池里拿（24 块） |
| 默认阻塞方式 | 我们设了 `MPP_POLL_BLOCK` | 默认**非阻塞**：送不进去返回 `MPP_ERR_BUFFER_FULL`，取不到返回空 |
| stride | 我们算好告诉编码器 | 解码器告诉我们（`1920×1088`） |
| 写文件 | 码流连续，一次 `fwrite` | 图像有 stride 填充，**逐行** `fwrite` |

---

## 2. `main.cpp` 只做了 6 件事

| # | 在干什么 | 比喻 | 关键调用 | 里程碑 |
|:---:|---|---|---|:---:|
| ① | **开一台解码机** | 找一家冲印店 | `mpp_create` → `mpp_init(MPP_CTX_DEC)` | M1 |
| ② | **告诉它码流怎么来** | "我按固定长度给你，你自己切" | `MPP_DEC_GET_CFG` → `base:split_parse = 1` → `MPP_DEC_SET_CFG` | M1 |
| ③ | **送码流** | 把胶卷一段一段递过去 | `fread` 64KB → `mpp_packet_init` → `decode_put_packet`（满了等一下再送同一包） | M2 |
| ④ | **第一次取：它说要多大的相纸** | 店员："你的照片是 1920×1080，给我 24 张这么大的纸" | `decode_get_frame` → `info_change` → 建内存池 → `SET_EXT_BUF_GROUP` → `SET_INFO_CHANGE_READY` | M2 |
| ⑤ | **取图像，写文件** | 一张张取回冲好的照片 | `decode_get_frame` → 跳过 errinfo/discard → 逐行 `fwrite` → `mpp_frame_deinit` | M3 / M4 |
| ⑥ | **收尾** | 最后一段胶卷标上"完"，等最后一张照片 | 最后一包 `mpp_packet_set_eos` → 一直取到 `mpp_frame_get_eos` 为真 → 释放 | M4 |

几个看着很怪的东西，各自解决什么问题：

| 东西 | 为什么非要它 |
|---|---|
| `split_parse = 1` | 我们按 64KB 读文件，一包里可能有半帧、也可能有好几帧；让 MPP 内部重新切成一帧一帧 |
| info change | 解码器要先解析 SPS 才知道图像多大、要多少内存；第一次 `get_frame` 拿到的是这个"通知"，不是图像 |
| 内存池 `limit_config(buf_size, 24)` | H.264 有很多参考帧，解码器同时要占用 20 多块内存；池子给少了解码器会卡住 |
| `MPP_ERR_BUFFER_FULL` 后重送**同一包** | 输入队列只能放 4 包；满了说明解码器忙，丢掉这包就漏帧了 |
| `mpp_frame_deinit` | 每一帧都占着池子里的一块内存，不还回去解码器就没内存用了，会卡死 |
| 逐行写 `width` 字节 | 解码出来的缓冲区是 `1920×1088`，直接整块写的话，ffplay 播放时每帧底部多出 8 行垃圾，画面会滚动错位 |

---

## 3. 进度

| 里程碑 | 内容 | 状态 |
|:---:|---|:---:|
| M1 | 解码器初始化（split_parse） | ⬜ |
| M2 | 送码流，拿到 info change，配置内存池 | ⬜ |
| M3 | 拿到第 1 帧图像，写成 NV12 | ⬜ |
| M4 | 解完整个文件（或前 N 帧），处理 EOS | ⬜ |
| M5 | 命令行参数（`-i -o -t -n`） | ⬜ |
| M6 | 统计汇总（帧数、fps、错误帧） | ⬜ |
| — | 整理成 `MppDecoder` 类（RAII、分模块） | ⬜ |
| — | 实验 | ⬜ |

---

## 4. 文档导航

| 顺序 | 文档 | 什么时候看 |
|:---:|---|---|
| 01 | [环境准备](guide/01_环境准备.md) | 第一次：CLion profile、脚本、把 `aaa.264` 传到板子 |
| 02 | [读源码](guide/02_读源码.md) | 写代码前：按顺序读 `mpi_dec_test.c`、`mpi_dec_utils.c`，答完 4 个问题 |
| 03 | [具体写 day2_h2642yuv 的流程](具体写day2_h2642yuv的流程.md) | 写代码时：M1～M6 每一步的调用、完整代码、怎么验证、易错点 |

Day 1 里已经学过、这里直接复用的知识（不再重复）：

| 知识点 | 在哪 |
|---|---|
| MPP 核心类型（MppCtx、MppApi、MppPacket、MppFrame、MppBuffer……） | [../day1_yuv2h264/guide/07_MPP核心数据类型.md](../day1_yuv2h264/guide/07_MPP核心数据类型.md) |
| MppPacket 的 `data / size / pos / length` | [../day1_yuv2h264/MppPacket和MppFrame的用法和区别.md](../day1_yuv2h264/MppPacket和MppFrame的用法和区别.md) |
| stride、NV12 布局、逐行读写 | [../day1_yuv2h264/doc/stride.md](../day1_yuv2h264/doc/stride.md)、[nv12 文档](../day1_yuv2h264/nv12中yuv分布效果图和读取方式.md) |
| SPS / PPS、GOP | [sps_pps.md](../day1_yuv2h264/doc/sps_pps.md)、[gop.md](../day1_yuv2h264/doc/gop.md) |
| DRM / DMA 缓冲区 | [DRM缓冲区.md](../day1_yuv2h264/doc/DRM缓冲区.md) |
| fread / fwrite | [fread和fwrite 的详细用法.md](<../day1_yuv2h264/doc/fread和fwrite 的详细用法.md>) |
| 全部技术点总览 | [技术点地图.md](../day1_yuv2h264/doc/技术点地图.md) |
