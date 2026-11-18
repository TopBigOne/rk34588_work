# 第 38 周（2027-08-09 ～ 08-15）：N8 ONVIF · 自动发现 IPC、拿到 RTSP 地址

> **所属**：Q4 · [N8 ONVIF](../2_nvr_work/README.md) ｜ [← 第 37 周](第37周_N9_M3_PS封装推流.md) ｜ [第 39 周 →](第39周_N4_M1M2_按时间回放.md) ｜ [概览](../一年学习计划.md)
> **投入**：主线约 15 小时，并行约 5 小时（20 小时是上限，不是指标）
> **细化程度**：草案（开始本周前，按前面的实际进度和面试反馈再细化）
> **状态**：⬜

## 本周目标

- NVR 接上网线后，自动搜到局域网里的 IPC，并通过 ONVIF 拿到它的 RTSP 地址。
- 拿到的地址交给 N2 的 RTSP 客户端拉流；N5 的 mini NVR 多一路**真摄像头**。
- **前提**：要有一台支持 ONVIF 和 H.265 的 IPC。**没买到的话，本周当缓冲周**，N8 往后放。
- 代码由谁写：按概览的 C 方案，N8 由 Claude 写，你负责读懂、跑通、抓包。

## 学完能回答的问题

1. WS-Discovery 是怎么"搜"到设备的？发到哪个地址、哪个端口，设备回什么？
2. ONVIF 的接口为什么是 SOAP（HTTP 上发 XML）？GetProfiles 和 GetStreamUri 各拿到什么？
3. WS-Security 的 PasswordDigest 是怎么算的？为什么设备时间不对时，鉴权可能会失败？
4. 组播为什么不能通过 adb 转发？

## 开始前要准备好

- 一台 IPC（支持 ONVIF + H.265），接在同一个路由器上。README 第 6 节的硬件表里这一项还是 ☐。
- 板子已接入局域网（第 30 周起就需要）。
- 第 7～9 周写的 N2 客户端、第 19～22 周的 N5 能正常运行。

## 主线任务

| # | 任务 | 预计 | 做完得到什么 |
|:---:|---|:---:|---|
| 1 | IPC 上手：网页登录、打开 ONVIF、建一个用户；按说明书给的地址用 VLC 拉 RTSP | 1.5 h | VLC 能看到 IPC 的画面 |
| 2 | 先看现成工具发的包：在 Mac 上找一个 ONVIF 客户端工具（选哪个开始前定，**未核对** Mac 上有哪些能用），Wireshark 抓一次"发现 + 取流地址"的全过程 | 2 h | 一份标注过的抓包：Probe、ProbeMatch、GetProfiles、GetStreamUri |
| 3 | WS-Discovery：往组播地址 `239.255.255.250` 的 3702 端口发 Probe，收 ProbeMatch，解析出设备的服务地址（XAddrs） | 3 h | 程序打印出 IPC 的 IP 和服务地址 |
| 4 | SOAP 调用：HTTP POST 发 GetCapabilities、GetProfiles | 2.5 h | 打印出 IPC 的所有 profile（主码流、子码流） |
| 5 | WS-Security 鉴权 + GetStreamUri。PasswordDigest = Base64(SHA1(nonce + created + password)) | 3 h | 拿到每个 profile 的 RTSP 地址 |
| 6 | 交给 N2 拉流；N5 的配置里加上这一路 | 1.5 h | mini NVR 预览里出现真摄像头的画面 |
| 7 | **收尾**：故障注入（IPC 断电、密码错、IPC 时间不对）+ 模拟面试 5 问 + 问题案例库 | 1.5 h | N8 完成；问题案例库新增至少 1 条 |

## 并行任务

- **E**：拿真 IPC 的码流跑一遍自己的工具：SPS 解析器、ffprobe。看它的 GOP、码控方式，和第 25～27 周 N6 的结论对照。
- **F**：按面试反馈补短板。

## 资料

- [2_nvr_work/README.md](../2_nvr_work/README.md) 的 N8 一节
- ONVIF Core Specification：只看 Discovery 部分；Media Service：只看 GetProfiles、GetStreamUri 两个接口
- WS-Security UsernameToken Profile：PasswordDigest 的定义

## 验收

- 程序启动后几秒内能列出局域网里的 IPC，并打印出 RTSP 地址。
- N2 用这个地址拉流，解码 0 错误帧。
- 密码错误时，程序给出清楚的错误提示，不会崩溃。

## 本周不学

- PTZ 云台控制、事件订阅、图像参数设置（JD 没要求）
- 自己实现 ONVIF 服务端（让板子被别人发现）

## 本周记录（周末填）

- 实际用时：
- 碰到的问题（写进问题案例库）：
- 下周要调整的：
