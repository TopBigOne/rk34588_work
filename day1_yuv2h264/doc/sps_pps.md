# SPS / PPS（参数集）

> 从 [笔记_10_02.md](笔记_10_02.md) 拆出来，补上了板子实测的字节。
> H.265 多出来的 VPS 单独见 [h265_vps.md](h265_vps.md)。

---

## 1. 一句话

**SPS / PPS 是解码必需的"说明书"**：分辨率、profile、熵编码方式这些信息不在每一帧里重复，而是集中写在参数集里。解码器拿不到参数集就解不了码，所以要放在文件最开头。

| | 全称 | 管的范围 | 里面有什么 |
|---|---|---|---|
| **SPS** | Sequence Parameter Set，序列参数集 | 整个序列共用 | 分辨率、profile（100 = High）、level（40 = 4.0）、参考帧数 |
| **PPS** | Picture Parameter Set，图像参数集 | 图像级 | 熵编码方式（CABAC / CAVLC）、初始 QP |
| **VPS** | Video Parameter Set，视频参数集（**只有 H.265 有**） | 整个视频 | 分层、多层编码信息，见 [h265_vps.md](h265_vps.md) |

引用关系：slice → PPS → SPS（→ VPS）。每一帧的 slice 头里记着用哪个 PPS，PPS 里记着用哪个 SPS。

---

## 2. 实测：文件开头的 40 字节（H.264，1080p）

板子上编出来的 `out.h264`，`xxd -l 40` 看开头：

```
00000000: 0000 0001 6764 1028 ac1b 1aa0 7802 25e5  ....gd.(....x.%.
00000010: 8400 0003 0004 0000 0300 f23c 2211 a800  ...........<"...
00000020: 0000 0168 ee31 b01b                      ...h.1..
```

拆开来：

| 偏移 | 字节 | 是什么 |
|---|---|---|
| 0x00～0x03 | `00 00 00 01` | 起始码 |
| 0x04 | `67` | NAL 头：类型 = `0x67 & 0x1F` = **7 = SPS** |
| 0x05 | `64` | profile_idc = 100 = **High** |
| 0x06 | `10` | 约束标志 |
| 0x07 | `28` | level_idc = 40 = **Level 4.0** |
| 0x08～0x1E | …… | SPS 剩余字段（分辨率等，用指数哥伦布编码，肉眼看不出来） |
| 0x1F～0x22 | `00 00 00 01` | 起始码 |
| 0x23 | `68` | NAL 头：类型 = `0x68 & 0x1F` = **8 = PPS** |
| 0x24～0x27 | `ee 31 b0 1b` | PPS 内容 |

SPS 31 字节 + PPS 9 字节 = **40 字节**，和程序打印的 `header : 40 bytes` 一致。用 ffprobe 也能确认：`profile=High`、`level=40`。

### 认 NAL 类型

| 编码 | 计算 | 常见值 |
|---|---|---|
| H.264 | `nal_unit_type = 第 1 个字节 & 0x1F` | 7 SPS、8 PPS、5 IDR 帧、1 P 帧、6 SEI |
| H.265 | `nal_unit_type = (第 1 个字节 >> 1) & 0x3F` | 32 VPS、33 SPS、34 PPS、19/20 IDR、39 SEI |

H.264 第 0 帧紧跟在后面，开头是 `00 00 00 01 06`（SEI，MPP 写的版本信息），然后才是 IDR 图像数据。

---

## 3. 实测：H.265 的文件头（83 字节）

```
00000000: 0000 0001 4001 0c01 ……   → (0x40 >> 1) & 0x3F = 32 = VPS
……        0000 0001 4201 0101 ……   → 33 = SPS
……        0000 0001 4401 c0f3 ……   → 34 = PPS
```

| | 字节数（含起始码） |
|---|---|
| VPS | 27 |
| SPS | 45 |
| PPS | 11 |
| 合计 | **83**（程序打印 `header : 83 bytes`） |

顺序是 VPS → SPS → PPS，和引用关系反过来（被引用的先出现）。

---

## 4. 代码里怎么拿到

```cpp
// src/encoder/mpp_encoder.cpp  MppEncoder::get_header
std::vector<uint8_t> container(64 * 1024);
MppPacket rawPacket = nullptr;
CHECK(mpp_packet_init(&rawPacket, container.data(), container.size()));
MppPacketPtr headerPacket(rawPacket);
mpp_packet_set_length(headerPacket.get(), 0);                                   // ⚠️ 先清零
CHECK(encoderApi_->control(encoderCtx_, MPP_ENC_GET_HDR_SYNC, headerPacket.get()));
```

- **`MPP_ENC_GET_HDR_SYNC`**：编码器把参数集 `memcpy` 到我们给的容器里（`mpp_enc_impl.cpp` 977～999 行），同步返回。
- **`set_length(0)` 不能省**：`mpp_packet_init` 会把 length 设成整个容器大小，不清零编码器会以为容器满了。
- 拿到后由 `EncodePipeline::write_header` 写在文件最开头。

### `MPP_ENC_HEADER_MODE_EACH_IDR`

```cpp
MppEncHeaderMode headerMode = MPP_ENC_HEADER_MODE_EACH_IDR;
encoderApi_->control(encoderCtx_, MPP_ENC_SET_HEADER_MODE, &headerMode);
```

每个 IDR 帧前面都自动再带一份 SPS/PPS。好处：播放器从中间开始播（直播、拖动进度条、丢包后恢复）时，遇到下一个 IDR 就能拿到参数集，接着解码。代价：每个 GOP 多几十字节，可以忽略。

---

## 5. 和 profile / level 设置的关系

SPS 里的 profile / level 来自编码参数：

| 参数 | 本项目设置 | 写进 SPS 的值 |
|---|---|---|
| `h264:profile` | 100 | `profile_idc = 0x64` |
| `h264:level` | 30fps 时不设，MPP 按分辨率自动选 | 1080p → `level_idc = 0x28`（4.0） |
| | 60fps 时自己设 42 | `0x2A`（4.2） |

MPP 只按分辨率自动调 level，不看帧率（`h264e_sps.c` 139～159 行）。1080p60 如果还写 4.0，有的解码器会认为超出能力而拒绝播放，所以帧率超过 30 时要自己设。

---

## 6. 自测

- [ ] SPS 和 PPS 分别放什么？为什么要写在文件开头？
- [ ] 看到 `00 00 00 01 67`，怎么知道这是 SPS？H.265 的 `40 01` 呢？
- [ ] SPS/PPS 是在哪一步拿到的？为什么要先 `set_length(0)`？
- [ ] `MPP_ENC_HEADER_MODE_EACH_IDR` 起什么作用？
- [ ] 1080p60 为什么要手动设 `h264:level`？
