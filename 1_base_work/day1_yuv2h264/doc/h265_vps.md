# H.265 的 VPS（视频参数集）

VPS 是 Video Parameter Set 的缩写。H.264 的码流头只有 SPS、PPS 两层，H.265 在它们上面又加了一层 VPS。

## 1. 三层参数集

| 层级 | H.264 | H.265 | 管多大范围 |
|---|---|---|---|
| 视频 | 没有 | **VPS** | 整个视频，包括所有层（layer）和时域子层 |
| 序列 | SPS | SPS | 一个序列（从 IDR 开始），管分辨率、profile、level |
| 图像 | PPS | PPS | 一帧或一组帧，管熵编码、初始 QP |

它们之间是逐级引用的：

```
slice 头 → pps_id → PPS → sps_id → SPS → vps_id → VPS
```

解码器解一帧时，顺着这条链往上找参数。少了任何一级都解不了码，所以 H.265 文件开头的顺序必须是 **VPS → SPS → PPS → IDR 帧**。

## 2. 为什么要多加一层

H.265 设计时就考虑到了以后的扩展：

* **分层编码**：比如 SHVC（同一个码流里同时有 720p 基础层和 1080p 增强层）、MV-HEVC（3D 视频的左眼和右眼）
* **时域分层**：一个码流按帧率分出 15fps 和 30fps 等几层，网络差的时候可以直接丢掉高层

这些"多个层之间怎么组织"的信息，放进任何一个层自己的 SPS 都不合适，所以单独放到 VPS 里。

**对我们来说**：RK MPP 编的是普通的单层 Main profile 码流，VPS 里基本都是固定值。可以把它当成"H.265 必须带上的文件头"，不用太关心里面的内容。

## 3. VPS 里有哪些字段

MPP 生成 VPS 的代码在 `rk_code/external/mpp/mpp/codec/enc/h265/h265e_header_gen.c` 第 299 行的 `h265e_vps_write`。主要字段：

| 字段 | 含义 | MPP 写进去的值 |
|---|---|---|
| `vps_video_parameter_set_id` | VPS 的编号，SPS 用这个编号来引用它 | 0 |
| `vps_max_sub_layers_minus1` | 时域子层数减 1 | 0（只有 1 层） |
| profile_tier_level | profile、tier、level | 跟 `h265:profile` / `h265:level` 一样 |
| `vps_max_dec_pic_buffering_minus1` | 解码器最少要缓存多少帧，减 1 | 由参考帧数决定 |
| `vps_num_reorder_pics` | 最多要重排多少帧（有 B 帧时才大于 0） | — |
| `vps_timing_info_present_flag` | 后面是否带帧率等时间信息 | — |

> 官方文档（`Rockchip_Developer_Guide_MPP_CN.md` 第 690～691 行）写的是"`h265:profile` / `h265:level` 是 VPS 里的 profile_idc / level_idc"。其实 SPS 里也有一份一样的 profile_tier_level，两个地方的值是一致的。

## 4. 在码流里怎么认出 VPS

H.265 的 NAL 头有 **2 个字节**（H.264 只有 1 个），类型在第 1 个字节的 bit1～bit6：`type = (byte0 >> 1) & 0x3F`。

| NAL | type | 起始码后的两个字节 |
|---|---|---|
| **VPS** | 32 | `40 01` |
| SPS | 33 | `42 01` |
| PPS | 34 | `44 01` |
| IDR 帧（IDR_W_RADL） | 19 | `26 01` |

一个正常的 .h265 文件，开头应该是：

```
00 00 00 01 40 01 ...   ← VPS
00 00 00 01 42 01 ...   ← SPS
00 00 00 01 44 01 ...   ← PPS
00 00 00 01 26 01 ...   ← IDR 帧
```

编出 H.265 后可以这样检查：

```bash
xxd out.h265 | head -5
# 或者看每个 NAL 解析出来的字段
ffmpeg -i out.h265 -c copy -bsf:v trace_headers -f null - 2>&1 | grep -A3 "Video Parameter Set"
```

## 5. 跟代码的关系

* 把编码类型改成 `MPP_VIDEO_CodingHEVC` 后，调用 `MPP_ENC_GET_HDR_SYNC` 拿到的头是 **VPS + SPS + PPS 三个连在一起**（`h265e_header_gen.c` 第 721～731 行依次写 VPS、SPS、PPS）。代码不用改，照样先写进文件开头就行。
* 设置 `MPP_ENC_HEADER_MODE_EACH_IDR` 后，每个 IDR 帧前面也会带上这三个。
* 常见的坑：自己拼 H.265 码流（比如推 RTSP、封装 MP4）时只带了 SPS/PPS、漏掉 VPS，解码器会报错或者黑屏。
