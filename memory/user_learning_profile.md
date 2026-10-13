---
name: user-learning-profile
description: "用户在学 RK3588 MPP 音视频编解码（Android 背景，用中文交流），按\"国庆 5 天学习计划\"推进"
metadata:
  node_type: memory
  type: user
  originSessionId: 8f840744-0df2-415d-9dc8-43ed13063bd9
  modified: 2026-10-13T15:09:59.648Z
---

用户正在系统学习 RK3588（正点原子 ATK-DLRK3588 板子）上的 MPP 硬件编解码，学习计划在 `rk_work/国庆5天的学习计划/README.md`：Day 1 编码（day1_yuv2h264）、Day 2 解码（day2_h2642yuv）、Day 3 摄像头实时编码（day3_cam2h264，V4L2 + DMA-BUF + MPP），Day 1～3 是必须完成的基础。

- 用中文交流；会追问底层原理（"为什么能转成 uint8_t*""内存池到底是啥""你是怎么测的"），喜欢图文并茂、有源码依据的解释。
- 原本做 Android（工作目录里有 longbridge Android 工程），C/C++ 指针、句柄、RAII 这些在学；常踩"多传 &"、"ctx 和 api 传反"这类 void* 隐式转换的坑。
- 担心"AI 这么强还要不要从 0 学"，也会有不想学的时候——给建议时别施压，见 [[feedback-teaching-workflow]]。
- 开发环境：Mac M4 + CLion 交叉编译（编译器通过 OrbStack 的 rk-dev 虚拟机转发），板子用 USB/adb 连接（规则见 rk_work/CLAUDE.md）。

当前进度见 [[project-day2-status]]。
