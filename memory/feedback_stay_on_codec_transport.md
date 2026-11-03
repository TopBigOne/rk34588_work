---
name: feedback-stay-on-codec-transport
description: 2026-11-02 用户指出学偏了：主线是音视频编解码和传输，mmap/dma_heap/DRM 这类底层只学到"够用"为止
metadata:
  type: feedback
---

2026-11-02 用户说"现在已经学偏了，我们主要学音视频编解码和传输相关的技术知识点"。

偏在哪：Day 3 原计划是 day3_cam2h264（摄像头 → H.264，已完成），之后我又带着做了 day3_mmap 的 M1～M4（/proc/self/maps、dma_heap、DMA_BUF sync 计时、DRM dumb buffer、PRIME），一路挖底层内存机制，和编解码、传输越来越远。

用户给的尺度：day3_mmap 学到 /proc/self/maps（M1）和 dma_heap（M1 第 3 步）"就差不多了"。也就是说，M3（sync 计时）、M4（DRM dumb、PRIME）都多了。M2（两个指针指向同一块摄像头 buffer）用户没说多余。

**Why:** 用户的目标是音视频编解码和传输。底层知识只是工具，挖太深既占时间，又看不出和主线的关系（M3 时用户就说过"不知道学了啥"）。

**How to apply:**
- 每个新实验、新文档，开头先写一句：它服务于哪个编解码或传输问题。答不上来就不做，或者压到一两段说明。
- 用户追问底层原理时照常讲清楚，但不主动扩展成新的里程碑。
- 按 国庆5天的学习计划/README.md 的主线走：Day 4 RGA（为编码服务：缩放、双码流），Day 5 转码 + RTSP 传输。
- 主线上还没碰的点要优先：GOP / I P B 帧、码率控制、SPS/PPS/NAL、PTS/DTS、RTP/RTSP 打包。

相关：[[feedback-experiment-design]]

2026-11-03 补充：用户看了一份新加坡“安卓框架开发工程师”招聘（AOSP、Binder、Camera HAL3、RK BSP），我提议以后可以在 RK3588 刷 Android 追 MediaCodec → Codec2 → MPP。用户明确说“不能半途而废，先把 MPP 学完”。Android Framework / HAL 方向等 MPP 主线学完再提，不要中途插进来。
