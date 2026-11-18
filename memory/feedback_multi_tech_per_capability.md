---
name: feedback-multi-tech-per-capability
description: 2026-11-18 用户要求每个能力点用多种技术实现（如传输用 RTSP/RTMP/WebRTC），按"一深多浅"：一种手写吃透，其他用库，用实测数据对比
metadata:
  type: feedback
---

用户说："每一个点，你可以考虑用不同的技术实现，例如传输：可以用 rtsp，rtmp，webrtc 等"。

**Why:** 目标是练到 3～5 年音视频研发的水平（见 `rk_work/音视频研发培养计划.md`），需要广度；但 20 小时/周，全部手写做不完。

**How to apply:**
- 每个能力点：一种手写（核心代码用户写），其他用成熟库（libdatachannel、FFmpeg、RKNN 官方库），重点讲清和手写那种的区别。
- 同一场景下的实测对比（延迟、CPU、画质、文件大小）是用户要的，不受 [[feedback-stay-on-codec-transport]] 里"不主动做 A/B"的限制。
- 对照表在 `2_nvr_work/README.md` 第 2 节原则 6。
- 主干知识点的够用线提高到"面试连续追问三层答得上"，主干以外仍按 [[feedback-learning-depth-line]]。
