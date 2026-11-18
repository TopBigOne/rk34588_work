# 第 30 周（2027-06-14 ～ 06-20）：N12 WebRTC · M1 信令和连通

> **所属**：Q3 · [N12 WebRTC 推流到浏览器](../2_nvr_work/README.md) ｜ [← 第 29 周](第29周_N11_M2M3_手写RTMP和延迟对比.md) ｜ [第 31 周 →](第31周_N12_M2_WebRTC发视频.md) ｜ [概览](../一年学习计划.md)
> **投入**：主线约 15 小时，并行约 5 小时（20 小时是上限，不是指标）
> **细化程度**：草案（开始本周前，按前面的实际进度和面试反馈再细化）
> **状态**：⬜

## 本周目标

用 libdatachannel（C++ 库）在板子上建一个 WebRTC 端，和 Mac 上浏览器里的网页交换 SDP 和 ICE candidate，建立连接，日志里看到 DTLS 握手成功。本周还不发视频。

## 学完能回答的问题

1. WebRTC 为什么需要"信令"？信令本身是不是 WebRTC 标准的一部分？
2. SDP offer/answer 是谁发给谁？里面最重要的几行是什么？
3. ICE candidate 是什么？局域网里为什么不需要 STUN/TURN 也能连？
4. DTLS 在这里做什么？媒体数据为什么是加密的？
5. 和 RTSP 比，WebRTC 建立连接多了哪几步？

## 开始前要准备好

- **面试多的那周主线可以停**，被问倒的问题记下来，回来补。
- **网络前提：板子必须用网线接入局域网。** WebRTC 媒体走 UDP，adb 只能转发 TCP。本周开始前要确认板子能拿到 IP，Mac 能 ping 通。
- 代码分工：按 C 方案（**待确认**），libdatachannel 的交叉编译和板子端代码由 Claude 写，你读懂、跑通。
- libdatachannel 的依赖（OpenSSL 等）和交叉编译方法**未核对**，Claude 在本周之前先编好，确认能在板子上运行。

## 主线任务

| # | 任务 | 预计 | 做完得到什么 |
|:---:|---|:---:|---|
| 1 | 读 WebRTC 建连流程：信令 → offer/answer → ICE → DTLS → SRTP | 3 h | 一张流程图 |
| 2 | 读 libdatachannel 的示例和 Claude 写的板子端代码：PeerConnection、本地描述、candidate 回调 | 3 h | 代码读懂，关键行有注释 |
| 3 | 最简单的信令：手动复制粘贴 SDP（板子打印 offer，粘到网页里；网页生成 answer，粘回板子） | 2 h | 两边交换成功 |
| 4 | 连通：看到 ICE 状态变为 connected、DTLS 握手完成 | 2 h | 日志截图 |
| 5 | 在 Chrome 的 `chrome://webrtc-internals` 里看连接状态，逐行读一遍 SDP | 3 h | SDP 每一行的注释 |
| 6 | （可选）把手动复制换成一个极简 WebSocket 信令服务 | 2 h | 打开网页自动连上 |

## 并行任务

- **E**：RTCP 包格式：SR、RR，以及 RFC 4585 里的 NACK、PLI（第 32 周要用）。
- **F**：复盘面试。

## 资料

- libdatachannel 的 GitHub 仓库和示例（**未核对**具体示例名）
- WebRTC 入门资料：MDN 的 WebRTC API 文档

## 验收

- 板子和浏览器建立 WebRTC 连接，DTLS 握手成功
- 能逐行讲清 SDP 的主要字段
- 能脱稿回答上面 5 个问题

## 本周不学

- STUN/TURN 服务器搭建（局域网用不到）
- DTLS、SRTP 的密码学细节（够用线：知道媒体是加密的、密钥由 DTLS 协商出来）
- WebRTC 源码

## 本周记录（周末填）

- 实际用时：
- 碰到的问题（写进问题案例库）：
- 下周要调整的：
