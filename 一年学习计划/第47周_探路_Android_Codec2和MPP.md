# 第 47 周（2027-10-11 ～ 10-17）：探路 · Android Codec2 和 MPP

> **所属**：Q4 · [总纲](../音视频研发培养计划.md) ｜ [← 第 46 周](第46周_下一年方向调研.md) ｜ [第 48 周 →](第48周_探路_Camera_HAL3.md) ｜ [概览](../一年学习计划.md)
> **投入**：主线约 15 小时，并行约 5 小时（20 小时是上限，不是指标）
> **细化程度**：草案（开始本周前，按第 46 周的调研结果再细化）
> **状态**：⬜

## 本周目标

- **探路，不是正式学**：用一周时间，在 Mac 上读源码，搞清楚 Android 上调用 `MediaCodec` 时，底下是怎么一路走到 RK 的 MPP 的。
- 不给板子刷 Android。
- 本周结束时，判断"Android 底层多媒体"值不值得作为下一年的方向。

## 学完能回答的问题

1. Codec2 框架里的 C2Component、C2Work、C2Buffer 分别是什么？
2. App 调用 `MediaCodec.queueInputBuffer`，要经过哪几层才到达编解码组件？
3. 一个基于 MPP 的 Codec2 解码组件，要把 C2Work 翻译成 MPP 的哪些调用？（回答时标明哪些是读源码确认过的，哪些是推断）
4. MPP 文档里说 Android 用"外部分帧"和"gralloc 内存导入外部 buffer group"，这两件事在我们的 Linux 代码里分别对应什么？

## 开始前要准备好

- 第 46 周的调研结论。
- 读 AOSP 源码的方式（官方的在线代码搜索，或者在本地只拉 `frameworks/av` 一个仓库）。
- RK 的 Codec2 组件源码：**位置待找**。已核对：本地 SDK（atk_dlrk3588_linux5.10）是 Linux SDK，`external/mpp` 和 `docs` 里都搜不到 Codec2。RK 的 Android SDK 里应该有对应的组件（**推断**），需要找公开的源码或镜像。

## 主线任务

| # | 任务 | 预计 | 做完得到什么 |
|:---:|---|:---:|---|
| 1 | Codec2 基本概念：C2Component、C2Work、C2Buffer，组件怎么收 work、怎么回调 `onWorkDone` | 3 h | 一张概念图 |
| 2 | 读 AOSP 的 `frameworks/av/media/codec2/`：从 MediaCodec 到 Codec2 组件的调用路径（路径开始前核对） | 3 h | 一张调用图 |
| 3 | 找 RK 的 Codec2 组件源码。找不到的话，读 AOSP 自带的软件组件（例如软件 H.264 解码组件），理解一个组件要实现哪些函数 | 2.5 h | 源码位置；或者"找不到"的结论和替代方案 |
| 4 | 对照 MPP 文档：`rk_code/external/mpp/doc/Rockchip_Developer_Guide_MPP_CN.md` 第 330 行（Android SDK 用外部分帧）、第 500 行（Android 上 gralloc 内存通过 `MPP_DEC_SET_EXT_BUF_GROUP` 交给解码器）；和我们 Day 2、Day 6 的用法对比 | 2 h | 对照表 |
| 5 | 写一页"C2Work → MPP 调用"的映射，确认过的和推断的分开标 | 2.5 h | 映射表 |
| 6 | 结论：这个方向要学什么、和这一年的重合度、值不值得作为下一年的方向 | 2 h | 一页结论，写进总纲第 2 节 |

## 并行任务

- **E**：无。
- **F**：继续第 46 周列出的面试基本功要求。

## 资料

- AOSP `frameworks/av/media/codec2/`
- `rk_code/external/mpp/doc/Rockchip_Developer_Guide_MPP_CN.md` 第 330、500 行
- [JD分析.md](../2_nvr_work/JD分析.md) 里的 J11、J14（Android 底层多媒体岗位）

## 验收

- 调用图和映射表写完，确认过的和推断的分开标注。
- 有一页明确的结论：下一年学还是不学。

## 本周不学

- OMX（已经被 Codec2 取代，只需要知道它是什么）
- 编译 AOSP、刷机

## 本周记录（周末填）

- 实际用时：
- 碰到的问题（写进问题案例库）：
- 下周要调整的：
