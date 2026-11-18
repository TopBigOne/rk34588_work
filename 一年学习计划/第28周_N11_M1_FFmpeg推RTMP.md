# 第 28 周（2027-05-31 ～ 06-06）：N11 RTMP · M1 先用 FFmpeg 推通

> **所属**：Q3 · [N11 RTMP 推流](../2_nvr_work/README.md) ｜ [← 第 27 周](第27周_N6_M5_硬编和x264对比.md) ｜ [第 29 周 →](第29周_N11_M2M3_手写RTMP和延迟对比.md) ｜ [概览](../一年学习计划.md)
> **投入**：主线约 15 小时，并行约 5 小时（20 小时是上限，不是指标）
> **细化程度**：草案（开始本周前，按前面的实际进度和面试反馈再细化）
> **状态**：⬜

## 本周目标

板子把 MPP 编码好的 H.264 用 RTMP 推到 Mac 上的流媒体服务器（mediaMTX），VLC 或 ffplay 从服务器拉流能看。用 Wireshark 抓包，认出 RTMP 握手、connect、publish 和第一个视频 tag。

## 学完能回答的问题

1. RTMP 是"推"，RTSP 通常是"拉"，这对 IPC、NVR、直播平台意味着什么？
2. RTMP 握手的 C0/C1/C2、S0/S1/S2 各是什么？
3. 为什么第一个视频 tag 要是 AVC sequence header？里面装的是什么？
4. 为什么 RTMP 里放的是 AVCC 格式，不是 Annex-B？

## 开始前要准备好

- **面试多的那周主线可以停**，被问倒的问题记下来，回来补。
- 代码分工：按 C 方案（**待确认**），推流代码由 Claude 写，你读懂、跑通、测数据。
- Mac 上装 mediaMTX（默认 RTMP 端口 1935，**未核对**，以安装后的配置文件为准）。
- 网络：板子到 Mac 的方向，**推断**可以用 `adb reverse tcp:1935 tcp:1935` 走 USB，本周第一件事就是验证；不行的话要先用网线把板子接入局域网。

## 主线任务

| # | 任务 | 预计 | 做完得到什么 |
|:---:|---|:---:|---|
| 1 | 读 RTMP 规范的握手和 chunk 两节，读 FLV 规范的 tag 结构 | 3 h | 一页笔记：握手、chunk、消息类型、FLV tag |
| 2 | Mac 上跑 mediaMTX；验证 `adb reverse` 能不能让板子连到 Mac 的 1935 端口 | 1 h | 连通，或者改用网线的结论 |
| 3 | 读 Claude 写的推流代码：libavformat 的 flv 输出 + `rtmp://` 地址；重点看 extradata 怎么从 SPS/PPS 生成 | 2 h | 代码读懂，关键行加了注释 |
| 4 | 上板推流，VLC/ffplay 从 mediaMTX 拉流 | 2 h | 画面出来 |
| 5 | Wireshark 抓包（在 Mac 上抓），找出 C0/C1/C2、`connect`、`publish`、第一个视频 tag | 3 h | 截图 + 每一步的说明 |
| 6 | 把 AVC sequence header 的每个字节对照规范写注释 | 2 h | 一张字节注释表 |
| 7 | 粗测延迟（方法见第 29 周），先有个数 | 2 h | 一个初测数字 |

## 并行任务

- **E**：AMF0 编码格式（number、string、object 怎么编码），为第 29 周可选的手写推流做准备。
- **F**：复盘面试。

## 资料

- Adobe 的 RTMP 规范、FLV 规范（**未核对**具体版本和链接，到时候搜）
- FFmpeg 源码 `libavformat/flvenc.c`（看它怎么写 tag）
- mediaMTX 官方文档（**未核对**）

## 验收

- 板子推流，Mac 上拉流能看
- 抓包能指出握手、connect、publish、第一个视频 tag
- 能脱稿回答上面 4 个问题

## 本周不学

- RTMP 服务端实现、RTMPS、RTMP 的音频
- mediaMTX 的内部实现

## 本周记录（周末填）

- 实际用时：
- 碰到的问题（写进问题案例库）：
- 下周要调整的：
