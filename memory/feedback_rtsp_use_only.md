---
name: feedback-rtsp-use-only
description: 2026-11-20 用户定：RTSP 会用就行不研究，跳过 N1、N2，从 N3 开始；RTSP 用 SRS + libavformat
metadata:
  type: feedback
---

RTSP 只要会用（像 HTTP 会 GET/POST 就行），不手写协议、不研究。N1（自写 RTSP 服务端）、N2（自写 RTSP 客户端）跳过，一年计划从 N3 开始写代码。

**Why:** 用户 2026-11-20 说“rtsp 相关我想能会用就行，不要多做研究”，可以用 GitHub 上的项目或本地 SRS 源码（`/Users/dev/Documents/AV/srs`，7.0，带 `trunk/conf/rtsp.conf` 的 RTMP→RTSP）。

**How to apply:** 后面阶段计划里写“输入来自 N1/N2”的，一律换成 FFmpeg libavformat 读文件或拉 `rtsp://`；RTSP 服务端用 SRS。讲到 RTSP 时只讲怎么用（URL、端口、命令），不展开信令、RTP 打包。规则已写进 `3_one_year_work/0_参考代码编写约定.md`，N3 的改法写在 N3 计划开头的“变更”。相关：[[project-n0-status]]、[[feedback-stay-on-codec-transport]]
