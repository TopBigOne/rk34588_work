# 第 41 周（2027-08-30 ～ 09-05）：N14 手机当 IPC · Camera2 采集 + MediaCodec 硬件编码

> **所属**：Q4 · [N14 手机当 IPC](../2_nvr_work/README.md) ｜ [← 第 40 周](第40周_N4_M3M4_倍速和界面.md) ｜ [第 42 周 →](第42周_N14_M2_手机推流进NVR.md) ｜ [概览](../一年学习计划.md)
> **投入**：主线约 15 小时，并行约 5 小时（20 小时是上限，不是指标）
> **细化程度**：草案（开始本周前，按前面的实际进度和面试反馈再细化）
> **状态**：⬜

## 本周目标

- 用**手机**（不给板子刷 Android）：Camera2 采集 → MediaCodec 编码 H.264 → 存成 `.h264` 文件。
- 把 MediaCodec 和 MPP 放在一起对照：都是硬件编码器，接口设计有什么相同和不同。
- 代码由谁写：按概览的 C 方案，N14 由 Claude 写。你原来就做 Android 开发，想自己写也可以，开始前定。

## 学完能回答的问题

1. MediaCodec 的输入、输出 buffer 队列是怎么工作的？和 MPP 的 `encode_put_frame` / `encode_get_packet` 对照，有什么相同和不同？
2. 用 Surface 作为编码器输入时，Camera2 的画面是怎么"直接"进编码器的？有没有经过 CPU 拷贝？
3. `BUFFER_FLAG_CODEC_CONFIG` 标记的那个输出 buffer 里装的是什么？
4. MediaCodec 输出的 H.264 是 Annex-B（带起始码）还是 AVCC？（本周用实测回答）
5. `KEY_I_FRAME_INTERVAL` 和 MPP 的 `rc:gop` 是一回事吗？

## 开始前要准备好

- 一台 Android 手机，开发者模式和 USB 调试都打开。
- Android Studio 环境（你原来的开发环境）。
- 第 7～9 周 E 线写的 H.264 SPS 解析器。

## 主线任务

| # | 任务 | 预计 | 做完得到什么 |
|:---:|---|:---:|---|
| 1 | 定架构：Android 工程，分采集（camera）、编码（encoder）、输出（sink）三层，和 rk_work 的分层对应 | 2 h | 架构写进流程文档 |
| 2 | Camera2 打开后置摄像头，预览 | 1.5 h | 手机上能看到预览 |
| 3 | 创建 H.264 编码器：MediaFormat 里设宽高、码率、帧率、I 帧间隔，颜色格式用 Surface 输入；`createInputSurface()` 交给 Camera2 作为输出目标 | 3 h | 编码器启动，日志里能看到输出 buffer |
| 4 | 输出循环：`dequeueOutputBuffer` 取码流，处理 `BUFFER_FLAG_CODEC_CONFIG`，写成 `.h264` 文件 | 2.5 h | `adb pull` 拿到文件，ffplay 能播 |
| 5 | 分析码流：ffprobe 看格式和帧类型；用自己的 SPS 解析器解宽、高、profile；确认是不是 Annex-B | 2 h | 第 4 个问题的答案（实测） |
| 6 | 写一页对照：MediaCodec 和 MPP 的创建、配置、送帧、取码流、释放，各用哪个接口 | 2 h | 对照表（以后面试讲"两个平台的硬编"用） |
| 7 | 码控：分别设 `BITRATE_MODE_CBR` 和 `BITRATE_MODE_VBR`，看实际码率，和第 25 周 N6 的结论对照 | 2 h | 一张码率对比表 |

## 并行任务

- **E**：用 Android Studio 的 Profiler 或 `adb shell top` 看编码时的 CPU 占用，和 MPP 硬编对照。
- **F**：按面试反馈补短板。

## 资料

- [2_nvr_work/README.md](../2_nvr_work/README.md) 的 N14 一节
- Android 官方文档：MediaCodec、MediaFormat、Camera2
- Day 1、Day 3 的 MPP 编码代码（用来对照）

## 验收

- 手机录 30 秒，`.h264` 文件在 Mac 上能用 ffplay 播放，帧率、分辨率和设置的一致。
- 对照表写完，能口头讲清 MediaCodec 和 MPP 编码接口的异同。

## 本周不学

- Codec2、OMX 框架层（第 47 周探路时再看）
- Camera HAL（第 48 周探路时再看）
- 音频采集和编码

## 本周记录（周末填）

- 实际用时：
- 碰到的问题（写进问题案例库）：
- 下周要调整的：
