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
- 2026-11-18 状态：所有阶段 ⬜，N0 未开始（Day 6 M3 未上板、M4 未写；Day 8 未上板）。
