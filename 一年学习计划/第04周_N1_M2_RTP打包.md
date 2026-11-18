# 第 04 周（2026-12-14 ～ 12-20）：N1 · M2 RTP 打包

> **所属**：Q1 · [N1 n1_rtsp_server：自己写 RTSP 服务端](../2_nvr_work/README.md) ｜ [← 第 03 周](第03周_N1_M1_RTSP信令.md) ｜ [第 05 周 →](第05周_N1_M3_H265和摄像头.md) ｜ [概览](../一年学习计划.md)
> **投入**：主线约 15 小时，并行约 5 小时（20 小时是上限，不是指标）
> **细化程度**：详细
> **状态**：⬜

## 本周目标

PLAY 之后，把 H.264 文件按 NAL 切开、装进 RTP 包，从同一条 TCP 连接发给 ffplay，**出画面**，并且按 30 fps 的速度播放。

## 学完能回答的问题

1. RTP 固定头 12 字节里有哪些字段？V、PT、M、sequence number、timestamp、SSRC 各填什么？
2. 视频的 RTP 时间戳为什么用 90 kHz？30 fps 时，相邻两帧的时间戳差多少？同一帧切成的多个包，时间戳一样吗？
3. 一个 NAL 太大放不进一个包时，FU-A 怎么切？FU indicator 和 FU header 各占几位、填什么？S 位和 E 位什么时候置 1？
4. M 位（marker）在视频里表示什么？接收端靠它做什么？
5. interleaved 模式下，一个 RTP 包前面加的 4 个字节分别是什么？

## 开始前要准备好

- 第 03 周的信令已跑通，ffplay 能停在 PLAY 之后等数据。
- 输入文件：板子上的 `/userdata/av/aaa.264`（H.264 1920×1080，1803 帧；Day 2 的输入，Day 6 也用过）。
- Claude 的 `m2流程.md` 和参考实现。

## 主线任务

> 代码由谁写：按 C 方案（待用户确认），**NAL 切分、RTP 头、FU-A 分片、interleaved 封装都由你写**，这是 N1 最核心的部分。

| # | 任务 | 预计 | 做完得到什么 |
|:---:|---|:---:|---|
| 1 | 读 RFC 3550 第 5.1 节（RTP 头）、RFC 6184 第 5.6 节（单 NAL 包）和第 5.8 节（FU-A），画出 FU indicator / FU header 的位图 | 2.5 h | 一张自己画的位图，能对照它写代码 |
| 2 | 按起始码（`00 00 01` 或 `00 00 00 01`）把文件切成一个个 NAL，打印每个 NAL 的类型（`nal[0] & 0x1F`）和长度 | 2.5 h | 日志能看出 SPS(7)、PPS(8)、IDR(5)、非 IDR(1) 的顺序 |
| 3 | 按"访问单元"（一帧）分组：一帧的所有 NAL 用同一个时间戳，每帧加 3000；写 RTP 头 | 2 h | 一个函数：输入一个 NAL + 时间戳 + 是否帧内最后一个，输出 RTP 包 |
| 4 | 小 NAL 单包发；大 NAL 用 FU-A 切（每片负载先定 1400 字节左右），只有最后一片的 E 位为 1；整帧最后一个包的 M 位为 1 | 3 h | ffplay 出画面 |
| 5 | interleaved：每个 RTP 包前加 `$`（0x24）、通道号（SETUP 时的 0）、2 字节大端长度，从 RTSP 那条 TCP 连接发出 | 1 h | （和第 4 步一起验收） |
| 6 | 按 30 fps 节流：每发完一帧，按"应该发出的时刻"睡到点，而不是固定睡 33 ms（固定睡会越来越慢） | 2 h | 播放速度正常，1803 帧大约 60 秒播完 |
| 7 | review + 填本周记录 | 2 h | — |

## 并行任务（E 原理和工具 / F 面试基本功）

| 线 | 任务 | 预计 |
|---|---|:---:|
| E | Wireshark 抓本周的流：对着抓到的包，找出 RTP 头的每个字段，验证 seq 每包加 1、timestamp 每帧加 3000、M 位在每帧最后一个包上 | 2 h |
| F | `send` 在 TCP 上可能只发出一部分（返回值小于要发的长度），写一个 `sendAll`；阻塞和非阻塞 socket 的区别 | 3 h |

## 资料

- **RFC 3550**：第 5.1 节 RTP Fixed Header Fields
- **RFC 6184**：第 5.6 节 Single NAL Unit Packet，第 5.8 节 Fragmentation Units (FUs)
- **RFC 2326**：第 10.12 节 Embedded (Interleaved) Binary Data（`$` + 通道号 + 长度）
- H.264 NAL 头（1 字节：forbidden_zero_bit、nal_ref_idc、nal_unit_type）：H.264 标准第 7.3.1 节
- Day 5 的 `m3_RTSP 播放.md` 第 4.3 节：SDP 里的 SPS/PPS 为什么必须有
- 对照代码（读，不抄）：ZLMediaKit 的 `src/Extension/H264Rtp.cpp`（GitHub，本地没有；文件名是推断，以仓库实际为准）

## 验收

- [ ] ffplay 出画面，没有花屏
- [ ] 1803 帧播完用时约 60 秒（误差 1 秒以内）
- [ ] Wireshark 里：seq 连续递增；同一帧的包时间戳相同，相邻帧差 3000；FU-A 的 S/E 位正确
- [ ] 问题案例库：本周碰到的花屏、卡住等问题至少记 1 条（如果一次就成功，记"最容易出错的地方"）

## 本周不学

- RTCP（Sender Report）：先不发，ffplay 也能播。
- STAP-A（多个小 NAL 合成一个包）：只用单包和 FU-A。
- 按 MTU 精确算分片大小：走 TCP 时不受 MTU 限制，先用固定 1400 字节；UDP 时再考虑。
- B 帧和 DTS：Day 1～6 的码流没有 B 帧（**推断**，用 `ffprobe -show_frames -select_streams v aaa.264 | grep pict_type | sort | uniq -c` 确认有没有 B 帧）。

## 本周记录（周末填）

- 实际用时：
- 碰到的问题（写进问题案例库）：
- 下周要调整的：
