---
name: project-nvr-phase
description: 2026-11-18 起下一阶段的主线是基于 RK3588 做 mini NVR，计划在 2_nvr_work/README.md（N0～N10），Claude 重点辅导
metadata:
  type: project
---

2026-11-18 用户明确：下一阶段 Claude 要**重点辅导用户基于 RK3588 做 NVR 相关开发**。计划全文在 `rk_work/2_nvr_work/README.md`，以它为准（进度看第 8 节，查 SDK 看第 9 节）。

**Why:** 用户要从 Android App 开发转到音视频编解码 + 流媒体，主攻安防 NVR 岗位（对照真实 JD 定的，见 `~/Documents/AV/openedv/Tone_rk358/doc/音视频全栈_学习路线与知识地图.md` 0.4 节）。Day 1～8 的基础已移到 `1_base_work/`；差距是协议没自己实现过、没做过存储、没系统做过码控、没用过 perf。最后要整理成求职作品。

**How to apply:**
- 阶段：N0 收尾 Day 6/Day 8 → **N1 rtsp_server → N2 rtsp_client → N3 recorder → N5 mini_nvr → N6 rate_control**（加粗主干，做完可投简历）→ N4 playback、N7 HTTP-FLV/HLS、N8 ONVIF、N9 GB28181、N10 RKNN 事件录像。每个阶段是 `2_nvr_work/` 下一个工程，分支和提交前缀用工程名（`[n1_rtsp_server_M1]`）。
- **核心代码用户自己写**（协议状态机、RTP 打包/解包、FU-A、录像分段、MPP 封装）；Claude 写样板代码（参数解析、统计、脚本），负责文档、架构、编译上板验证和 review。和 Day 8 播放器“全部由 Claude 写”不同。
- 每阶段先定架构（流程文档里写目录、分层、所有权），用户认可再写代码；每个阶段先说清对应 JD 哪一条。
- N6 码控对比实验是 JD 要求，是 [[feedback-stay-on-codec-transport]]“不主动做 A/B”的例外。
- 硬件限制：adb 只转发 TCP → RTP 先走 RTSP interleaved；没有真 IPC → N1～N5 板子自己当 IPC，127.0.0.1 互连。
- 复用 Day 4～8 的 MppEncoder、MppDecoder、RGA 拼接、frame_queue、Qt 界面，复制时按新命名规范整理。
- 沿用 [[feedback-learning-depth-line]]、[[feedback-architecture-first]]、[[feedback-milestone-doc-structure]]。
- **2026-11-18 计划升级**：总纲 `rk_work/音视频研发培养计划.md`（目标：一年左右把能力和作品练到 3～5 年音视频研发水平；T 型；六条线 A 嵌入式多媒体 / B 传输 / C 存储和媒体处理 / D AI / E 原理工具 / F 面试基本功；练经验的五个办法：故障注入、问题案例库、模拟面试追问、带问题读开源、资深标准 review）。依据 `2_nvr_work/JD分析.md`（18 份 JD）。
- 新顺序：N0 → **N1 → N2 → N3 → N10 YOLO → N5**（= mini NVR v1，做完开始投 1～3 年岗位）→ N6（加 QP、smart GOP、x264 对比）→ N11 RTMP → N7（可选）→ N12 WebRTC（libdatachannel）→ N13 摄像头链路 → N9/N8 → N4 → N14 手机当 IPC。README 第 3、4、5、6、8 节已按这个改。
- **按周执行**：`rk_work/一年学习计划.md`（概览 + 第 7 节进度表）+ `一年学习计划/第NN周_*.md` 共 52 份（39b0df0）。第 1 周 = 2026-11-23。用户说"开始第 N 周"时：先核对上周、更新进度表，再把本周任务拆成 2～3 小时一块；第 25～52 周是草案，开始前细化。代码由谁写的 C 方案（主干用户写核心，浅的部分 Claude 写）**用户还没确认**。
- 用户定：每周约 20 小时（主线 15 + 并行 5）；Android 先用手机；Camera HAL 先不学，N13 后再定；不加 LVGL。
- 2026-11-18 状态：所有阶段 ⬜，N0 未开始（Day 6 M3 未上板、M4 未写；Day 8 未上板）。
