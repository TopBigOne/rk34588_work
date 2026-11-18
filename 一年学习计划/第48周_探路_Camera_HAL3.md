# 第 48 周（2027-10-18 ～ 10-24）：探路 · Android Camera HAL3

> **所属**：Q4 · [总纲](../音视频研发培养计划.md) ｜ [← 第 47 周](第47周_探路_Android_Codec2和MPP.md) ｜ [第 49 周 →](第49周_面试专项_音视频.md) ｜ [概览](../一年学习计划.md)
> **投入**：主线约 15 小时，并行约 5 小时（20 小时是上限，不是指标）
> **细化程度**：草案（开始本周前，按第 46 周的调研结果再细化）
> **状态**：⬜

## 本周目标

- **探路，不是正式学**：在 Mac 上读 AOSP 的 Camera HAL3 接口，跟一次 `processCaptureRequest` 的完整流程，并和第 35 周 N13 学过的 V4L2、ISP 对照。
- 不给板子刷 Android。
- 本周结束时，做出总纲第 2 节里留下的决定：**Camera HAL 要不要学**。
- 本周末顺便做一次第 36～48 周的小结（年度回顾放在第 52 周）。

## 学完能回答的问题

1. 从 App 的 Camera2 API 到 HAL，中间有哪几层（CameraService、CameraProvider……）？
2. HAL3 的 `configure_streams`、`process_capture_request`、`process_capture_result`、`notify` 各做什么？
3. 一次 capture request 里有什么（settings 和 output buffers）？HAL 什么时候回 result，什么时候回 buffer？
4. HAL 底下是怎么调用 V4L2 的？3A 是在哪里算的？（和 N13 里的 rkaiq 对照）
5. Camera HAL 的工作和"音视频"方向差多远？

## 开始前要准备好

- 第 46 周的 Camera HAL JD 统计。
- 第 35 周 N13 画的摄像头链路图。
- AOSP 源码：`hardware/libhardware/include/hardware/camera3.h`（HAL3 的 C 接口头文件，注释里有很长的流程说明）；新版本用的是 `hardware/interfaces/camera/` 下的接口定义。路径开始前核对。
- RK 的 camera HAL 源码：**位置待找**（本地只有 Linux SDK）。

## 主线任务

| # | 任务 | 预计 | 做完得到什么 |
|:---:|---|:---:|---|
| 1 | 分层图：Camera2 API（你熟悉的）→ CameraService → CameraProvider → HAL → V4L2 驱动 | 2.5 h | 一张分层图 |
| 2 | 读 `camera3.h` 的注释：`configure_streams`、`process_capture_request`、`process_capture_result`、`notify` | 3 h | 每个接口一句话说明 |
| 3 | 跟一次 capture：request 进来 → HAL 处理 → result（metadata）和 buffer 分别回调；画时序图 | 3 h | 时序图 |
| 4 | 对照 N13：HAL 底下的 V4L2 调用（REQBUFS、QBUF、DQBUF）和 Day 3 的代码对比；3A 在哪里跑。找 RK camera HAL 源码，找不到就写"推断" | 2.5 h | 对照表 |
| 5 | **决定 Camera HAL 要不要学**：对照第 46 周的 JD 统计、本周读源码的感受、和音视频方向的距离 | 2 h | 结论写进总纲第 2 节 |
| 6 | 第 36～48 周小结：哪些阶段按时完成，哪些顺延或跳过，原因是什么 | 2 h | 小结写进概览的进度表 |

## 并行任务

- **E**：无。
- **F**：继续第 46 周列出的面试基本功要求。

## 资料

- AOSP `hardware/libhardware/include/hardware/camera3.h`
- AOSP `hardware/interfaces/camera/`
- 第 35 周 N13 的流程文档、Day 3 的 V4L2 代码

## 验收

- 分层图、时序图、对照表都写完；确认过的和推断的分开标。
- "Camera HAL 要不要学"有明确结论，写进总纲。

## 本周不学

- 编译 AOSP、刷机、真正写一个 HAL
- ISP 图像调优（tuning）

## 本周记录（周末填）

- 实际用时：
- 碰到的问题（写进问题案例库）：
- 下周要调整的：
