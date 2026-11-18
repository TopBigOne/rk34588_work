# 基于 RK3588 的 NVR 学习计划

> **制定日期**：2026-11-18
> **目标**：在 ATK-DLRK3588 上从零做出一台 **mini NVR**，学会 NVR 岗位要求的整条音视频链路：接入 → 解码 → 缩放拼接 → 录像存储 → 回放 → 对外输出。最后把它整理成求职作品。
> **RK3588 SDK 开发包**：`/Users/dev/Documents/AV/openedv/Tone_rk358/atk_dlrk3588_linux5.10`（正点原子 ATK-DLRK3588 Linux 5.10 SDK，基于 rk3588 linux-5.10-gen-rkr8）
> 👉 **有什么不了解的（MPP、RGA、V4L2、驱动、板子上装了哪些库和工具），先到这个 SDK 里搜**：官方文档、源码、示例程序都在里面。怎么搜见第 9 节。
>
> **为什么是 NVR**：对照了真实 JD（见总路线文档 `Tone_rk358/doc/音视频全栈_学习路线与知识地图.md` 第 0.4 节），rk_work Day 1～8 学的 MPP、RGA、V4L2、多路解码拼图，和 NVR 岗位要求几乎逐条对应。差距在：**协议没自己实现过、没做过存储、没系统做过码控、没用过 perf**。这份计划就是来补这几块的。

---

## 1. 最终要做出来的东西

```
                         ┌──────────────────────── RK3588 mini NVR ────────────────────────┐
  IPC 1 ──RTSP──┐        │                                                                  │
  IPC 2 ──RTSP──┤        │  ┌──────────┐   ┌──────────┐   ┌───────────┐   ┌──────────────┐ │
  IPC 3 ──RTSP──┼──────► │  │ 接入      │──►│ MPP 解码  │──►│ RGA 拼 2×2 │──►│ Qt 本地预览   │ │
  IPC 4 ──RTSP──┘        │  │ RTSP 客户端│   └──────────┘   └───────────┘   └──────────────┘ │
  （先用板子自己当 IPC）   │  │ RTP 解包   │                                                    │
                         │  └────┬─────┘                                                     │
                         │       │ 不解码，直接存                                              │
                         │       ▼                                                           │
                         │  ┌──────────┐   ┌──────────┐   ┌──────────────────────────────┐ │
                         │  │ 录像分段   │──►│ 录像索引  │──►│ 回放：按时间查 → 解码 → 显示 │ │
                         │  │ MP4/TS    │   │ SQLite   │   └──────────────────────────────┘ │
                         │  └──────────┘   └──────────┘                                    │
                         │                                                                  │
                         │  对外输出：RTSP 转发 / HTTP-FLV / HLS ──► 浏览器、手机、VLC          │
                         │  平台对接：ONVIF（发现 IPC）、GB28181（接入国标平台）               │
                         └──────────────────────────────────────────────────────────────────┘
```

---

## 2. 学习原则（沿用 rk_work 的约定）

1. **核心代码自己写**：协议状态机、RTP 打包和解包、FU-A 分片重组、录像分段逻辑、MPP 封装，这些都由你手写；参数解析、统计、脚本这类样板代码可以由 Claude 写。面试会顺着项目一路深挖，没亲手写过的代码讲不清楚。
2. **先定架构，再写代码**：每个阶段先在流程文档里定好目录和分层（app / common / 各部件 / pipeline），依赖只能往下。你认可后再开始写。
3. **每个知识点划“够用线”**：判断标准是“不懂它，我能不能写出眼前这段代码？”。每个阶段都列了“不学”的内容，碰到了就记进“以后再挖”清单，不展开。
4. **每个阶段先回答“它对应 JD 的哪一条”**：答不上来的就不做。
5. **复用前面写过的代码**：MppEncoder、MppDecoder、RGA 缩放和拼接、帧队列、Qt 界面，都从 Day 4～8 拿过来，顺手按命名规范整理。

---

## 3. 总览

> 每个阶段是 `2_nvr_work/` 下的一个工程，分支和提交前缀用工程名，例如 `[n1_rtsp_server_M1]`。
> 时间按 Day 5、Day 6 的实际节奏估算，每个阶段 1～2 周，**仅供参考**，不用赶。

| 阶段 | 工程 | 一句话 | 对应 JD | 估计 |
|:---:|---|---|---|:---:|
| N0 | （收尾 Day 6、Day 8） | 4 路拼图上屏；读懂播放器的音视频同步 | 拼接、音画同步 | 3～5 天 |
| **N1** | `n1_rtsp_server` | 自己写 RTSP 服务端，把 H.264/H.265 发出去（板子当 IPC） | RTSP/RTP | 2 周 |
| **N2** | `n2_rtsp_client` | 自己写 RTSP 客户端，拉流 → RTP 解包 → MPP 解码（NVR 接入） | 接入、拉流、解码 | 1.5 周 |
| **N3** | `n3_recorder` | 拉到的流不解码，按时间分段存成 MP4，建索引，空间满了循环覆盖 | **存储** | 2 周 |
| N4 | `n4_playback` | 按时间查录像 → 解码 → 显示，支持拖动和倍速 | 播放、卡顿 | 1.5 周 |
| **N5** | `n5_mini_nvr` | 把 N2～N4 整合成 4 路 NVR，跑 24 小时，用 perf 找瓶颈 | 性能、稳定性、资源占用 | 2 周 |
| **N6** | `n6_rate_control` | 码控实验报告：画质、码率、存储量之间怎么平衡 | **码控策略** | 1 周 |
| N7 | `n7_http_flv_hls` | 对外输出 HTTP-FLV 和 HLS，浏览器、手机能看 | HLS、HTTP-FLV | 1.5 周 |
| N8 | `n8_onvif` | 用 ONVIF 自动发现 IPC、拿到 RTSP 地址 | 安防通用 | 1 周 |
| N9 | `n9_gb28181` | 板子作为设备，接入 GB28181 国标平台 | GB28181 | 2 周 |
| N10 | `n10_ai_event`（加分） | 一路画面跑 RKNN 目标检测，检测到人就触发事件录像 | 边缘 AI（JD 2） | 1.5 周 |
| — | 作品整理 | GitHub README、架构图、实测数据、简历描述 | — | 3 天 |

**加粗的是主干**：N1 → N2 → N3 → N5 → N6 做完，就已经覆盖了 JD 1 的大部分要求，可以开始投简历；后面的阶段边投边做。

**硬件限制，会影响设计**：

- adb 只能转发 **TCP**，不能转发 UDP。所以 N1、N2 的 RTP 一律先走 **TCP（RTSP interleaved）**，Day 5 用 live555 时也是“必须走 TCP”。UDP 等板子用网线接入局域网后再试（可选）。
- 还没有真正的 IPC。N1～N5 用**板子自己当 IPC**：N1 的服务端和 N2 的客户端都在板子上，通过 `127.0.0.1` 互连，不走网络。N8 开始需要真 IPC（见第 6 节）。

---

## 4. 各阶段详细计划

### N0 收尾 Day 6、Day 8（3～5 天）

| 项目 | 内容 |
|---|---|
| 做什么 | ① Day 6 M3 上板验收（`mosaic.h265`，1803 帧）；② Day 6 M4：拼好的画面显示到屏幕；③ 读 Day 8 播放器的 `sync/media_clock.cpp` 和 `pipeline/video_worker.cpp`，搞懂“视频追音频”是怎么做的 |
| 为什么 | 拼接和上屏是 N5 的基础；音视频同步是 JD 1 第 4 条，Day 8 代码是 Claude 写的，至少同步这部分要自己能讲清楚 |
| 验收 | M3、M4 都在板子上跑通；能口头讲清：播放器以哪个时钟为准、视频帧来早了或晚了怎么处理 |

---

### N1 `n1_rtsp_server`：自己写 RTSP 服务端（2 周）

**一句话**：不用 live555，自己写一个 RTSP 服务端，把 H.264/H.265 码流发出去，ffplay/VLC 能看。这就是一台 IPC 在网络上做的事情。

| 里程碑 | 做什么 | 验收 |
|---|---|---|
| M1 RTSP 信令 | TCP 监听 → 解析请求 → 回复 OPTIONS / DESCRIBE（带 SDP）/ SETUP / PLAY / TEARDOWN | ffplay 连上后走完握手（RTSP 交互日志对得上），这时候还没有画面 |
| M2 RTP 打包 | 读 Day 5 的 H.264 文件 → 按起始码切 NAL → 小 NAL 用单包、大 NAL 用 **FU-A 分片** → 加 RTP 头 → 用 `$` + 通道号 + 长度（interleaved）从同一个 TCP 连接发出 | ffplay 出画面；按 30fps 节流后播放速度正常 |
| M3 H.265 + 摄像头实时源 | H.265 的 FU 打包（和 H.264 只差 NAL 头）；输入换成 Day 4 的摄像头编码器（实时） | 摄像头画面实时可看，延迟 < 500ms |
| M4 多客户端 + 抓包对照 | 同时 2 个客户端；每个 IDR 前带上 SPS/PPS；在 Mac 上用 Wireshark 抓包，和 live555 发出的包逐字段对比 | 2 个 ffplay 同时看不卡；抓包里 RTP 序号、时间戳连续 |

- **新技术点（够用线以内）**：RTSP 方法和状态码、SDP 里的 `m=`、`a=rtpmap`、`a=fmtp`（sprop-parameter-sets）；RTP 头 12 字节的字段（V、PT、seq、timestamp、SSRC、M 位）；视频 90kHz 时间戳；FU-A 的 FU indicator / FU header；interleaved 帧格式；socket、`accept`、`send`。
- **不学**：RTCP 的完整报告格式（只发最简单的 SR 或先不发）、RTSP 2.0、鉴权（Digest，放到 N5 以后）、epoll（先一个客户端一个线程）。
- **复用**：Day 4 的 `camera_capture` + `mpp_encoder`，Day 5 的 `stream_file_reader`。
- **资料**：RFC 2326（RTSP）、RFC 3550（RTP）、RFC 6184（H.264 打包）、RFC 7798（H.265 打包）；对照代码看 ZLMediaKit 的 `src/Rtsp/`。

---

### N2 `n2_rtsp_client`：自己写 RTSP 客户端（1.5 周）

**一句话**：NVR 接入 IPC 时扮演的是**客户端**。连上 N1 的服务端 → 拉流 → 把 RTP 包还原成完整的 NAL → 送进 MPP 解码。

| 里程碑 | 做什么 | 验收 |
|---|---|---|
| M1 信令 + 收包 | 发 OPTIONS…PLAY，解析 SDP 拿到编码格式和 SPS/PPS；从 interleaved 流里拆出 RTP 包 | 打印出的 seq 连续、timestamp 每帧加 3000（30fps） |
| M2 解包重组 | 单包 NAL 直接还原，**FU-A 分片按 S/E 位拼回完整 NAL**，补上起始码，存成 `.h264` | 存出的文件 ffplay 能播，和 N1 发的源文件逐帧比 md5 一致 |
| M3 接上解码 | 重组好的帧 → Day 5 的 MppDecoder → NV12 | 解码帧数正确，0 错误帧 |
| M4 断线重连 | 服务端停掉再启动，客户端自动重连；丢包时（seq 跳变）丢弃到下一个 IDR 再继续 | 重启服务端 10 次，客户端都能恢复出画面 |

- **新技术点**：SDP 解析、RTP 乱序和丢包的判断（seq 回绕）、帧边界（M 位）、为什么丢包后要等 IDR。
- **不学**：抖动缓冲（Jitter Buffer）的完整算法（TCP 下不乱序）、RTCP 反馈。
- **复用**：Day 5、Day 6 的 `mpp_decoder`、`frame_queue`。

---

### N3 `n3_recorder`：录像存储（2 周）

**一句话**：拉到的流**不解码**，直接封装成 MP4，按 10 分钟一个文件切分，记下每个文件的起止时间，硬盘满了删掉最早的录像。

| 里程碑 | 做什么 | 验收 |
|---|---|---|
| M1 转封装 | N2 拿到的 H.264 帧 → FFmpeg libavformat 写成 MP4（Annex-B 转 AVCC，设置 extradata，计算 PTS） | 生成的 MP4 在 Mac 上能播、能拖进度条 |
| M2 分段 | 每 N 分钟（先用 1 分钟测）在**下一个 IDR** 处切换新文件；按 `通道/日期/时间.mp4` 组织目录 | 连续录 10 分钟得到 10 个文件，每个文件都从 I 帧开始，首尾相接不丢帧 |
| M3 索引 | 每段录像写一条 SQLite 记录（通道、开始时间、结束时间、路径、大小） | 能按“某通道 + 时间段”查出对应的文件 |
| M4 循环覆盖 + 断电保护 | 剩余空间低于阈值就删最早的录像（文件和索引一起删）；改用 fragmented MP4，异常断电后已写的部分仍然能播 | 用小分区或配额模拟写满，能自动删旧录像；录像中途 `kill -9`，文件仍能播放 |

- **新技术点**：MP4 box 结构（ftyp、moov、mdat，够用线是“知道 moov 存索引、没写 moov 文件就播不了”）、Annex-B 和 AVCC 的区别、PTS/DTS 和 time_base、fragmented MP4、SQLite 的增删查、`statvfs` 查剩余空间。
- **不学**：手写 MP4 muxer（先用 FFmpeg，原理懂就行）、文件系统底层、RAID。
- **存储位置**：先用 `/userdata`；接上 SATA 硬盘或 U 盘后再换（见第 6 节）。
- **资料**：FFmpeg `doc/examples/remux.c`；Day 8 的 `annexb_converter`、`mp4_demuxer` 可以对照着看。

---

### N4 `n4_playback`：录像回放（1.5 周）

**一句话**：输入“通道 + 时间点”，从索引里找到文件，从最近的 I 帧开始解码显示，支持拖动和倍速。

| 里程碑 | 做什么 | 验收 |
|---|---|---|
| M1 按时间定位 | 查索引 → 打开 MP4 → seek 到目标时间之前最近的 I 帧 | 指定任意时间点，画面和时间对得上（误差 < 1 个 GOP） |
| M2 跨文件连续播放 | 一个文件播完自动接下一个 | 跨 3 个文件连续播放不黑屏 |
| M3 倍速 | 2 倍速：加快送显；8 倍速以上：只解 I 帧 | 2×、4×、8× 都流畅 |
| M4 Qt 界面 | 时间轴 + 播放、暂停、拖动（复用 Day 7、Day 8 的界面） | 在板子屏幕上能操作 |

- **新技术点**：seek 和关键帧、为什么高倍速只能解 I 帧（解码能力和带宽）、播放时钟。
- **复用**：Day 8 的 `mp4_demuxer`、`mpp_video_decoder`、`video_widget`。

---

### N5 `n5_mini_nvr`：4 路 NVR 整合 + 性能和稳定性（2 周）

**一句话**：把 N2～N4 拼起来：4 路同时接入、同时录像、2×2 预览，连续跑 24 小时，用 perf 找出瓶颈并优化。

| 里程碑 | 做什么 | 验收 |
|---|---|---|
| M1 架构 | 每路一个“通道”对象（接入 → 分发给录像和预览）；配置文件写 4 路地址；画出线程模型 | 架构文档写好，你认可 |
| M2 4 路接入 + 录像 + 预览 | 4 个 N1 服务端实例当 4 台 IPC；预览复用 Day 6 的 RGA 拼图 | 4 路同时录像，屏幕上 2×2 画面都在动 |
| M3 性能分析 | `top` 看 CPU，`/proc/<pid>/status` 看内存，**perf** 找热点（比如多余的拷贝） | 写一份性能报告：各线程 CPU、内存曲线、优化前后对比 |
| M4 24 小时稳定性 | 连续跑 24 小时，中途随机重启“IPC” | 内存不上涨、没有崩溃、每路录像连续 |

- **新技术点**：多路并发的线程模型、一路数据同时给两个消费者（录像、预览）、perf 的 `record` 和 `report`、内存泄漏怎么看。
- **不学**：epoll 高并发改造（路数少，用不上；放到 N7 以后看需要）。

---

### N6 `n6_rate_control`：码控实验报告（1 周）

**一句话**：JD 1 第 2 条原话是“平衡画质、码率、带宽与存储空间”。用实验数据回答：**同样的画面，哪种码控、多大码率最合适，一路摄像头一天要占多少硬盘？**

> 这是 JD 直接要求的实验，是“不主动做 A/B 对比”这条约定的例外。

| 里程碑 | 做什么 | 验收 |
|---|---|---|
| M1 码控模式 | 同一段素材（静态场景 + 运动场景各一段）分别用 CBR、VBR、AVBR、FIXQP 编码 | 表格：设定码率 vs 实际码率、码率波动 |
| M2 画质 | 在 Mac 上用 FFmpeg 算 PSNR、SSIM（可选 VMAF） | 曲线：码率 vs 画质，找出“画质不再明显提升”的拐点 |
| M3 GOP 和存储 | GOP 30 / 60 / 120 对码率和 seek 速度的影响；换算成每路每天的存储量 | 结论：给 4 路 1080p 的 NVR 推荐一组参数，并说明理由 |

- **新技术点**：CBR/VBR/AVBR/FIXQP 的区别、QP 和画质的关系、GOP 对码率和回放 seek 的影响、PSNR/SSIM 是什么。
- **复用**：Day 1 的编码器。

---

### N7 `n7_http_flv_hls`：对外输出 HTTP-FLV 和 HLS（1.5 周）

**一句话**：浏览器、手机不支持 RTSP，NVR 要转成 HTTP-FLV（低延迟）和 HLS（兼容性好）。

| 里程碑 | 做什么 | 验收 |
|---|---|---|
| M1 FLV 封装 | 自己写 FLV header + tag（视频 tag 里放 AVC sequence header 和 NALU） | 写出的 `.flv` 文件 ffplay 能播 |
| M2 HTTP-FLV | 极简 HTTP 服务，用 chunked 方式持续输出 FLV | 浏览器里用 flv.js 能看，记录延迟 |
| M3 HLS | 复用 N3 的分段思路，输出 TS 分片 + m3u8 | Safari 或手机浏览器能看；能解释 HLS 的延迟为什么高 |
| M4 Android 端 | 用 ExoPlayer 播 HLS 和 HTTP-FLV（老本行，很快） | 手机上能看，记录两种协议的延迟 |

- **新技术点**：FLV tag 结构、HTTP chunked、TS 的 PAT/PMT/PES（够用线：知道 TS 是 188 字节的小包，PAT→PMT→PES 怎么找到视频）、m3u8 格式。
- **网络**：浏览器访问需要板子接入局域网，或者用 `adb forward` 转发 HTTP 端口（HTTP 是 TCP，可以转发）。

---

### N8 `n8_onvif`：ONVIF 设备发现（1 周）

**一句话**：NVR 接上网线就能“自动搜到”局域网里的摄像头，靠的就是 ONVIF。

| 里程碑 | 做什么 | 验收 |
|---|---|---|
| M1 设备发现 | WS-Discovery：往组播地址发 Probe，收 ProbeMatch | 能搜到局域网里的 IPC，打印出它的地址 |
| M2 拿流地址 | SOAP 调用 GetProfiles → GetStreamUri（带 WS-Security 鉴权） | 拿到 RTSP 地址，交给 N2 拉流成功 |

- **前提**：需要 1 台支持 ONVIF 的真 IPC，板子要接入局域网（组播和 UDP 走不了 adb）。
- **不学**：PTZ 云台控制、事件订阅（JD 没要求，以后再说）。

---

### N9 `n9_gb28181`：接入国标平台（2 周）

**一句话**：板子作为 GB28181 设备，注册到国标平台，平台点播时把视频用 PS 格式推上去。

| 里程碑 | 做什么 | 验收 |
|---|---|---|
| M1 搭平台 | Mac 上用 docker 跑 ZLMediaKit + WVP-PRO（开源国标平台） | 平台网页能打开 |
| M2 注册和心跳 | 板子发 SIP REGISTER（Digest 鉴权）、定时发 Keepalive | 平台上显示设备在线 |
| M3 点播推流 | 收到 INVITE → 回 SDP → H.264 封装成 **PS 流** → RTP 推给 ZLMediaKit | 平台网页上能看到板子的画面 |

- **新技术点**：SIP 基础（REGISTER、INVITE、MESSAGE）、GB28181 的设备编码规则、PS 封装（PS header、PSM、PES）。
- **前提**：板子接入局域网（SIP 走 UDP）。
- **资料**：GB/T 28181-2022；WVP-PRO 和 ZLMediaKit 的文档。

---

### N10 `n10_ai_event`：AI 事件录像（加分，1.5 周）

**一句话**：一路画面送进 NPU 做目标检测，检测到人就画框，并触发 N3 的“事件录像”。这条链路就是 JD 2 说的“解码 → 推理 → 后处理”。

| 里程碑 | 做什么 | 验收 |
|---|---|---|
| M0 环境确认 | 确认 5.10 系统上 RKNN 能跑：先跑通 SDK 自带的 `external/rknpu2/examples/rknn_yolov5_demo` | demo 输出检测框（总路线文档里写过“可能要切 6.1”，先在这一步确认） |
| M1 接入流水线 | 解码出的 NV12 → RGA 缩放并转成 RGB → RKNN 推理 → 后处理 | 每帧输出检测结果，记录推理耗时 |
| M2 画框 + 事件录像 | 用 RGA 或 CPU 画框；检测到人时，把前后 10 秒的录像单独存一份 | 走过镜头前，能生成一段事件录像 |

---

### 作品整理（3 天）

- GitHub 仓库：README 里放架构图、功能清单、实测数据（N5 的性能报告、N6 的码控结论）、演示 GIF。
- 简历描述尽量用 JD 原话，见第 5 节。

---

## 5. 和 JD、简历的对应关系

| JD 1 原文 | 哪个阶段做 | 简历上可以这么写 |
|---|---|---|
| 音视频采集、编解码、存储、传输及播放 | N1～N5 | 基于 RK3588 实现 4 路 mini NVR：RTSP 接入、MPP 硬件解码、录像存储与回放、多协议输出 |
| 视频编码及码控策略优化，平衡画质、码率、带宽与存储 | N6 | 对比 CBR/VBR/AVBR/FIXQP 和不同 GOP，给出 4 路 1080p 场景的码控参数和存储量评估 |
| 解码、编码、缩放、拼接及色彩空间转换 | Day 4～6、N5 | 基于 MPP + RGA 实现多路解码、缩放与 2×2 拼接，DMA-BUF 零拷贝 |
| 延迟、卡顿、音画不同步 | N0、N4、N7 | 分析并对比 RTSP / HTTP-FLV / HLS 的端到端延迟 |
| 性能、稳定性及资源占用优化 | N5 | 用 perf 定位热点并优化，4 路 24 小时稳定运行 |
| RTSP/RTP、HLS、HTTP-FLV | N1、N2、N7 | 自研 RTSP 服务端和客户端（RTP over TCP、FU-A 分片与重组）、FLV 封装与 HTTP-FLV 输出 |
| 瑞芯微平台硬件编解码 SDK | 全部 | 熟悉 RK3588 MPP、RGA、V4L2 |
| GDB、perf | 全程、N5 | gdbserver 远程调试，perf 性能分析 |

JD 2 额外对应：N9（GB28181）、N10（RKNN，“解码 → 推理 → 后处理”）。

---

## 6. 需要准备的硬件

| 物品 | 哪个阶段要用 | 状态 |
|---|---|---|
| ATK-DLRK3588 + IMX415 + USB 线 | 全部 | ✅ |
| 网线（板子接入家里路由器） | N7 起（UDP、组播、浏览器访问） | ☐ |
| U 盘或 SATA 硬盘（SATA 还要硬盘电源） | N3 起（先用 `/userdata` 也行） | ☐ |
| 支持 ONVIF + H.265 的网络摄像头 × 1～2 | N8 必需，N5 有的话更真实 | ☐ |
| 板子的屏幕或 HDMI 显示器 | N0、N4、N5 预览 | ✅（Day 7 已在板子屏幕上运行 Qt） |

---

## 7. 每个阶段的固定流程（沿用 Day 5 起的做法）

1. **readme**：一句话目标、数据流图、里程碑表。
2. **架构**：目录、分层、类的职责和所有权，写进流程文档，你认可后才写代码。
3. **每个里程碑**：你写核心代码 → 我按 `-Wall -Wextra -Wshadow` 编译、上板验证、review（一次给全清单，标“必须改 / 建议改”）。
4. **“理解 Mx”文档**：干了什么、输入、输出（含实测）、用到的技术点、实际业务场景、怎么运行。
5. **新技术点各写一篇 md**，放进当天工程的 `doc/`。

---

## 8. 进度

| 阶段 | 状态 | 完成日期 | 备注 |
|---|:---:|---|---|
| N0 收尾 Day 6、Day 8 | ⬜ | | Day 6 M3 未上板、M4 未写；Day 8 未上板 |
| N1 rtsp_server | ⬜ | | |
| N2 rtsp_client | ⬜ | | |
| N3 recorder | ⬜ | | |
| N4 playback | ⬜ | | |
| N5 mini_nvr | ⬜ | | |
| N6 rate_control | ⬜ | | |
| N7 http_flv_hls | ⬜ | | |
| N8 onvif | ⬜ | | |
| N9 gb28181 | ⬜ | | |
| N10 ai_event | ⬜ | | |
| 作品整理 | ⬜ | | |

---

## 9. RK3588 SDK 开发包：遇到不了解的，先搜这里

**位置**：`/Users/dev/Documents/AV/openedv/Tone_rk358/atk_dlrk3588_linux5.10`（下文简称 `SDK/`）

这是正点原子给 ATK-DLRK3588 配的完整 Linux 5.10 SDK，2026-11-18 已全部检出。板子上跑的系统（内核、驱动、MPP、RGA、Buildroot 根文件系统）就是用它编出来的，所以**“板子上为什么是这样”的答案基本都在这里**。

### 9.1 先查哪里

| 想了解什么 | 去哪里找 |
|---|---|
| MPP 接口、编码参数、解码流程 | `SDK/external/mpp/doc/Rockchip_Developer_Guide_MPP_CN.md`（可以直接搜文字）；示例 `SDK/external/mpp/test/`（`mpi_enc_test.c`、`mpi_dec_test.c`、`mpi_dec_multi_test.c` 等） |
| RGA 缩放、拼接、格式转换，报错原因 | `SDK/external/linux-rga/docs/Rockchip_Developer_Guide_RGA_CN.md`、`Rockchip_FAQ_RGA_CN.md`；示例 `SDK/external/linux-rga/samples/` |
| 摄像头、ISP、V4L2 链路 | 文档 `SDK/docs/cn/Common/ISP/ISP30/`；ISP 引擎和取流示例 `SDK/external/camera_engine_rkaiq/`（`rkisp_demo`）；板子的摄像头设备树 `SDK/kernel/arch/arm64/boot/dts/rockchip/rk3588-alientek-cameras.dtsi` |
| MPP、RGA 的内核驱动 | `SDK/kernel/drivers/video/rockchip/mpp/` |
| GStreamer 的硬件编解码插件 | `SDK/external/gstreamer-rockchip/gst/rockchipmpp/` |
| 多媒体框架（VI → VPSS → VENC 风格的接口） | `SDK/external/rockit/mpi/`（文档在 `doc/`，示例在 `example/mod/`） |
| NPU 目标检测（N10） | `SDK/external/rknpu2/examples/rknn_yolov5_demo/` |
| 板子的根文件系统里装了哪些库和工具 | 正点原子的配置 `SDK/buildroot/configs/alientek_rk3588_defconfig`（以及它 `#include` 的 `rockchip/*.config`）；每个软件包的编译方式在 `SDK/buildroot/package/<包名>/` |
| 各模块的 PDF 文档 | `SDK/docs/cn/`（目录清单见 `SDK/docs/cn/docs_list_cn.txt`） |

### 9.2 和 NVR 各阶段相关的已知事实（2026-11-18 从 SDK 里查到的）

| 阶段 | 事实 | 依据 |
|---|---|---|
| N1、N2 | live555 是正点原子默认配置里启用的（Day 5 用的 `live555MediaServer` 就来自这里） | `alientek_rk3588_defconfig` 第 70 行 `BR2_PACKAGE_LIVE555=y` |
| N3、N4 | FFmpeg 在默认配置里启用了 | `buildroot/configs/rockchip/alientek.config` 第 56 行 `BR2_PACKAGE_FFMPEG=y` |
| N3 | SQLite 有 Buildroot 包（`buildroot/package/sqlite/`）；默认配置里是否启用、板子上有没有，**未核对** | — |
| N5 | perf 由 `rockchip/debug.config` 启用，而默认配置 `#include` 了 `debug.config`；板子上能不能直接用 `perf`，**未实测** | `debug.config` 第 12 行 `BR2_PACKAGE_LINUX_TOOLS_PERF=y` |
| N5 | 板子上抓包用的 tcpdump 只在 `rockchip/tools/extra.config` 里启用，默认配置没有 `#include` 它 → 板子上大概率没有，抓包先在 Mac 上做 | `tools/extra.config` 第 7 行 |

### 9.3 怎么搜

```shell
SDK=/Users/dev/Documents/AV/openedv/Tone_rk358/atk_dlrk3588_linux5.10

# 按关键字搜源码和 md 文档（只搜文本文件，-I 跳过二进制）
grep -rnI "MPP_ENC_SET_CFG" $SDK/external/mpp/
grep -rnI "imcomposite" $SDK/external/linux-rga/docs/

# 查某个软件包有没有、默认配置有没有启用
ls $SDK/buildroot/package/ | grep -i sqlite
grep -rn "BR2_PACKAGE_SQLITE" $SDK/buildroot/configs/alientek_rk3588_defconfig $SDK/buildroot/configs/rockchip/

# 找 PDF 文档
find $SDK/docs/cn -iname "*rga*"
```

也可以直接让 Claude 去搜，比如“在 SDK 里查一下 MPP 的 `rc:mode` 有哪些取值”。

### 9.4 注意

- **只在 Mac 上读代码，不在 Mac 上编译**：编译 SDK 要在 x86 Ubuntu 上做（见 `Tone_rk358/doc/Ubuntu编译SDK_操作清单.md`）。Mac 的磁盘也只剩几 GB 了。
- **SDK 的 kernel 在 Mac 上有约 10 个文件显示“已修改”**（如 `xt_DSCP.h` 和 `xt_dscp.h`）：macOS 文件系统不区分大小写，名字只差大小写的文件互相覆盖了。这不是真的改动，和音视频无关，不要在 Mac 上提交 kernel 的改动。
- **SDK 里的 MPP 版本和板子上用的不一定一样**：rk_work 运行时用的是板子上 `/userdata/mpp_build/lib` 里的库（见 `rk_work/CLAUDE.md`），平时对照的 MPP 源码在 `rk_work/rk_code/external/mpp/`。两边行为不一致时，以板子实际用的版本为准。
- **`app/rkadk` 不适配 RK3588**（只适配 rv1106、rv1126、rk3506），录像、推流的业务逻辑只能参考，不能直接用。
