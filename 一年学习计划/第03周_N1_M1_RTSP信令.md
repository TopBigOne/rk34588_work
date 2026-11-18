# 第 03 周（2026-12-07 ～ 12-13）：N1 · M1 RTSP 信令

> **所属**：Q1 · [N1 n1_rtsp_server：自己写 RTSP 服务端](../2_nvr_work/README.md) ｜ [← 第 02 周](第02周_N0_Day6的M4和Day8上板.md) ｜ [第 04 周 →](第04周_N1_M2_RTP打包.md) ｜ [概览](../一年学习计划.md)
> **投入**：主线约 15 小时，并行约 5 小时（20 小时是上限，不是指标）
> **细化程度**：详细
> **状态**：⬜

## 本周目标

不用 live555，自己写 RTSP 服务端的信令部分：ffplay 连上来以后，能走完 OPTIONS → DESCRIBE → SETUP → PLAY → TEARDOWN（这周还没有画面）。

## 学完能回答的问题

1. RTSP 和 HTTP 长得很像，它们最大的不同是什么？（提示：状态、Session、谁在发数据）
2. DESCRIBE 回复里的 SDP，`m=`、`a=rtpmap`、`a=fmtp` 三行各说明了什么？`sprop-parameter-sets` 里放的是什么、为什么要 base64？
3. SETUP 请求里的 `Transport: RTP/AVP/TCP;unicast;interleaved=0-1` 是什么意思？0 和 1 分别给谁用？
4. `CSeq` 和 `Session` 两个头各起什么作用？服务端回复时漏了 `CSeq` 会怎样？
5. 为什么这个项目的 RTP 必须走 TCP，而不是 UDP？

## 开始前要准备好

- N0 完成（第 01、02 周）。
- Claude 已写好 N1 的**架构文档**（目录、分层、类的职责）和 `m1流程.md`，参考实现在 N1 工程的 `claude_reference/` 里上板跑过。架构要你先认可。
- 工程目录：`2_nvr_work/n1_rtsp_server/`；分支 `n1_rtsp_server`，提交前缀 `[n1_rtsp_server_M1]`。
- 端口转发：`adb forward tcp:8554 tcp:8554`（Mac 上普通用户不能监听 1024 以下端口，Day 5 也是这么做的）。

## 主线任务

> 代码由谁写：按 C 方案（待用户确认），**RTSP 请求解析和状态机由你写**；socket 的样板（监听、accept）、日志、运行脚本可以直接复制 Claude 的。

| # | 任务 | 预计 | 做完得到什么 |
|:---:|---|:---:|---|
| 1 | 读 Claude 的架构文档，确认目录和分层；读 Day 5 的 `m3_RTSP 播放.md` 第 4.1、4.2 节回忆 RTSP 的大致流程 | 2 h | 架构你认可 |
| 2 | 先看"标准答案"：Day 5 的 live555 跑起来，Mac 上 `ffplay -v debug -rtsp_transport tcp rtsp://localhost:8554/out_720p.265`，把终端里打印的 RTSP 请求和回复抄下来 | 2 h | 一份完整的请求/回复样本，后面对照用 |
| 3 | TCP 监听 + 读一个完整的请求（以空行 `\r\n\r\n` 结尾），解析请求行（方法、URL、版本）和头（`CSeq`、`Transport`、`Session`） | 3 h | 日志打印出 ffplay 发来的每个请求 |
| 4 | 回复 OPTIONS（`Public:` 列出支持的方法）、DESCRIBE（`Content-Type: application/sdp` + SDP 正文；SPS/PPS 先从文件开头读出来再 base64） | 3 h | ffplay 收到 SDP，`-v debug` 日志里能看到它解析出 h264 |
| 5 | 回复 SETUP（生成 Session ID，回 `Transport`）、PLAY（`Range`、`RTP-Info` 可先省略）、TEARDOWN；用一个枚举记录连接状态 | 3 h | ffplay 走完握手，停在等数据的状态 |
| 6 | 和 Claude 的参考实现对照 review；填本周记录 | 2 h | review 清单里"必须改"的都改完 |

## 并行任务（E 原理和工具 / F 面试基本功）

| 线 | 任务 | 预计 |
|---|---|:---:|
| E | Wireshark 基础：在 Mac 上抓回环网卡 `lo0`，过滤 `tcp.port == 8554`，看 RTSP 文本。端口不是 554 时要用"Decode As… → RTSP"（ffplay 和 adb 之间的流量走本机回环，所以在 Mac 上就能抓到：**推断**，本周实测确认） | 2 h |
| F | Linux socket：`socket`、`bind`、`listen`、`accept`、`recv`、`send` 每个函数的返回值；`recv` 返回 0 是什么意思；为什么一次 `recv` 不一定读到一个完整请求 | 3 h |

## 资料

- **RFC 2326**（RTSP 1.0）：第 10 节各方法（10.1 OPTIONS、10.2 DESCRIBE、10.4 SETUP、10.5 PLAY、10.7 TEARDOWN）；第 12.39 节 Transport 头；第 10.12 节 interleaved
- **RFC 4566**（SDP）：第 5 节各行的格式
- **RFC 6184**（H.264 的 RTP 格式）：第 8.1 节 `sprop-parameter-sets`、`packetization-mode`
- Day 5 的 live555 经验：`1_base_work/day5_transcoder/day5编码流程/m3_RTSP 播放.md`（第 4.2 节为什么必须 RTP over TCP，第 4.5 节端口），脚本 `run_rtsp_server_via_usb.sh`
- 读码流文件：Day 5 的 `1_base_work/day5_transcoder/src/source/stream_file_reader.cpp`
- 对照代码（读，不抄）：ZLMediaKit 的 `src/Rtsp/RtspSession.cpp`（在 GitHub 上，本地没有）

## 验收

- [ ] ffplay 连上以后，服务端日志依次打印 OPTIONS、DESCRIBE、SETUP、PLAY，每个回复的 `CSeq` 和请求一致
- [ ] ffplay `-v debug` 的输出里能看到 SDP 被解析，编码格式是 h264，宽高正确
- [ ] 关掉 ffplay 时，服务端能收到 TEARDOWN 或检测到连接断开（`recv` 返回 0），不崩溃
- [ ] 第 2 步抄下来的 live555 样本和你的回复逐行对照过

## 本周不学

- RTSP 鉴权（Digest）：N5 以后再说。
- RTSP 2.0、GET_PARAMETER 心跳：用不上。
- epoll、多客户端：先一个连接，M4 再处理多客户端。
- SDP 里 `b=`、`t=` 等其他行的细节：照抄最小可用的写法。

## 本周记录（周末填）

- 实际用时：
- 碰到的问题（写进问题案例库）：
- 下周要调整的：
