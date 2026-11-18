# 第 43 周（2027-09-13 ～ 09-19）：N14 手机当 IPC · MediaMuxer 录像、MediaExtractor 回读

> **所属**：Q4 · [N14 手机当 IPC](../2_nvr_work/README.md) ｜ [← 第 42 周](第42周_N14_M2_手机推流进NVR.md) ｜ [第 44 周 →](第44周_作品整理v2.md) ｜ [概览](../一年学习计划.md)
> **投入**：主线约 15 小时，并行约 5 小时（20 小时是上限，不是指标）
> **细化程度**：草案（开始本周前，按前面的实际进度和面试反馈再细化）
> **状态**：⬜

## 本周目标

- 用 MediaMuxer 把手机编码的码流录成 MP4；用 MediaExtractor 读回来，交给 MediaCodec 解码显示，支持 seek。
- 和 N3（libavformat 写 MP4）、N4（回放 seek）对照。
- N14 收尾：写一张"Android 多媒体 API ↔ RK MPP/FFmpeg"对照表；故障注入、模拟面试、问题案例库。

## 学完能回答的问题

1. MediaMuxer 的 `addTrack` 为什么要用编码器输出的 MediaFormat？里面的 csd-0、csd-1 最后变成了 MP4 里的什么（和 N3 的 extradata、avcC 对照）？
2. 录到一半进程被杀、`stop()` 没调用，MP4 还能不能播？为什么？N3 是怎么解决这个问题的？
3. MediaExtractor 的 `SEEK_TO_PREVIOUS_SYNC` 和 FFmpeg 的 `AVSEEK_FLAG_BACKWARD` 是一回事吗？
4. Android 的 MediaExtractor + MediaCodec + MediaMuxer，分别对应 FFmpeg 的哪几个库？

## 开始前要准备好

- 第 41、42 周的 Android 工程。
- 第 10～14 周 N3、第 39～40 周 N4 的流程文档（用来对照）。

## 主线任务

| # | 任务 | 预计 | 做完得到什么 |
|:---:|---|:---:|---|
| 1 | MediaMuxer 录 MP4：用编码器输出的格式 `addTrack`，`start`，逐帧 `writeSampleData`，`stop` | 3 h | 手机上录的 MP4 在 Mac 上能播、能拖进度条 |
| 2 | 对照 N3：用自己的 MP4 box 遍历器看两个 MP4 的结构（moov 在前还是在后、avcC 的内容）；录到一半杀进程，看文件能不能播 | 2 h | 一张对照表 + 杀进程的实测结果 |
| 3 | MediaExtractor 读回：`selectTrack`、`seekTo`、`readSampleData`，喂给 MediaCodec 解码，显示到 Surface | 3 h | 手机上能回放刚录的 MP4，能拖动 |
| 4 | 对照 N4 的 seek：定位精度、为什么也只能落到关键帧 | 1 h | 第 3 个问题的答案 |
| 5 | **N14 总结**：Android 多媒体 API ↔ RK MPP/FFmpeg 对照表（采集、编码、解码、封装、拆封装、显示） | 2 h | 对照表（以后面试讲 Android + 嵌入式两端用） |
| 6 | **收尾**：故障注入（录像中被杀、编码器抛 `MediaCodec.CodecException`、相机被别的 App 抢占）+ 模拟面试 5 问 + 问题案例库 | 3 h | N14 完成；问题案例库新增至少 1 条 |
| 7 | 本周记录 | 1 h | — |

## 并行任务

- **E**：拿手机录的 MP4 和 N3 录的 MP4，各用 ffprobe `-show_frames` 看一遍时间戳，比较 PTS 是否均匀。
- **F**：按面试反馈补短板。

## 资料

- [2_nvr_work/README.md](../2_nvr_work/README.md) 的 N14 一节
- Android 官方文档：MediaMuxer、MediaExtractor
- 第 10～14 周 E 线写的 MP4 box 遍历器

## 验收

- 手机录的 MP4 在手机和 Mac 上都能播放、拖动。
- 对照表写完，能口头讲清 Android 多媒体 API 和 MPP/FFmpeg 的对应关系。
- 三种故障都有处理，App 不崩溃。

## 本周不学

- Codec2、OMX 框架层（第 47 周探路）
- MediaRecorder（更高层的封装，学不到细节）

## 本周记录（周末填）

- 实际用时：
- 碰到的问题（写进问题案例库）：
- 下周要调整的：
