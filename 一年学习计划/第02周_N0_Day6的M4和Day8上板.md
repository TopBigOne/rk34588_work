# 第 02 周（2026-11-30 ～ 12-06）：N0 · Day 6 M4 + Day 8 上板

> **所属**：Q1 · [N0 收尾 Day 6、Day 8](../2_nvr_work/README.md) ｜ [← 第 01 周](第01周_N0_Day6的M3上板.md) ｜ [第 03 周 →](第03周_N1_M1_RTSP信令.md) ｜ [概览](../一年学习计划.md)
> **投入**：主线约 15 小时，并行约 5 小时（20 小时是上限，不是指标）
> **细化程度**：详细
> **状态**：⬜

## 本周目标

把拼好的 2×2 画面显示到板子屏幕上（M4）；新增 M5：4 路不同帧率按一个主时钟对齐拼图，亲手写一遍“画面追时钟”；读懂 Day 8 播放器“视频追音频”是怎么做的。

## 学完能回答的问题

1. 播放器以哪个时钟为准？为什么通常选音频，而不是视频？
2. 一帧视频解出来时，如果比时钟"早"，播放器怎么做？如果"晚"了 200 ms，又怎么做？阈值是多少？
3. 裸的 H.265 码流（没有时间戳）交给播放器，播放速度由什么决定？为什么可能播得飞快？
4. 板子上运行图形程序，为什么要设置 `XDG_RUNTIME_DIR`？不设会怎样？
5. M5 里 15 fps 那一路为什么每帧要显示两次？60 fps 那一路丢掉的帧去哪了？主时钟为什么要等 4 路都拿到第一帧才开始走？

## 开始前要准备好

- 第 01 周的 M3 已跑通，`mosaic.h265` 验收通过。
- 板子接好屏幕，weston 桌面能显示（Day 7 已经在板子屏幕上跑过 Qt）。
- Day 8 测试视频：`1_base_work/day8_qt_player/test_media/sync_test_1080p.mp4`（已核对存在；生成脚本 `make_test_media.sh`）。

## 主线任务

> 代码由谁写（2026-11-20 定）：M4、M5 先由 Claude 写参考代码（`n0_nvr_mosaic/claude_reference/第02周_N0_Day6的M4和Day8上板/`），你照 `m4流程.md`、`m5流程.md` 在 `n0_nvr_mosaic/src/` 里重写；Day 8 只读不写。Day 8 播放器 2026-11-19 已经上板通过，本周不再上板。

| # | 任务 | 预计 | 做完得到什么 |
|:---:|---|:---:|---|
| 1 | 手敲第 01 周剩下的 M2、M3 核心文件：`rga/rga_compositor.cpp`、`pipeline/mosaic_compose_stage.cpp`、`encoder/mpp_encoder.cpp`、`app/m3_encode_mosaic.cpp`；自己的版本上板跑 m2、m3 | 4 h | 数字和参考代码一样（1803 张大图，mosaic.h265 1803 帧） |
| 2 | M4：上板跑 `run_m4_on_board_with_mac.sh`（板子上 `mkfifo`，编码输出写进 FIFO，`gst-launch-1.0 filesrc location=<FIFO> ! h265parse ! mppvideodec ! waylandsink`，要 `XDG_RUNTIME_DIR=/var/run`）；读 `m4流程.md` | 2 h | 屏幕上 2×2 画面在动；记下是不是播得飞快（裸码流没有时间戳，**这是允许的**，写进问题案例库） |
| 3 | 读 `m5流程.md`：主时钟、每帧的时间戳怎么来、每一格“换帧 / 重复 / 丢弃”的规则 | 1.5 h | 能在纸上画出“时钟刻度 × 4 路帧”的时间轴 |
| 4 | M5 手敲：`sync/master_clock.cpp`、`FrameQueue::tryPopIfDue`、`MosaicComposeStage::composeAlignedMosaic`；先跑 `m5test` 自测，再跑 `m5` | 4 h | 每路“显示 / 重复 / 丢弃”和 m5 文档的预期表对得上（误差说得清原因）；`m5screen` 上屏速度正常 |
| 5 | 读 Day 8 `doc/0_架构设计.md` 第 2 节数据流、第 5.3 节音频时钟；读 `src/sync/media_clock.cpp`：`reset`、`updateFromAudio`、`getPositionUs` | 2 h | 能说清“时钟的位置是谁在更新、怎么往前走” |
| 6 | 读 Day 8 `src/pipeline/video_worker.cpp` 第 224～241 行和 `video_worker.h` 第 124～125 行的两个常量，和自己写的 `composeAlignedMosaic` 对照 | 1.5 h | 能回答本周问题 2；说清 Day 8 “晚 100 ms 才丢”和 M5 “同一拍有更新的帧就丢”的区别 |
| 7 | 填本周记录 | 1 h | — |

## 并行任务（E 原理和工具 / F 面试基本功）

| 线 | 任务 | 预计 |
|---|---|:---:|
| E | 问题案例库补 1～2 条（M4 播放速度、M5 实测和预期的差距） | 2 h |
| F | C++：继续 RAII 和移动语义；读 Day 8 的 `MediaClock`，看它怎么用 `mutex` 保护共享状态 | 3 h |

## 资料

- Day 6 M4 的命令（上一轮会话定的方案，未实测）：`gst-launch-1.0 filesrc location=FIFO ! h265parse ! mppvideodec ! waylandsink`，环境变量 `XDG_RUNTIME_DIR=/var/run`
- GStreamer 的 MPP 插件源码：`/Users/dev/Documents/AV/openedv/Tone_rk358/atk_dlrk3588_linux5.10/external/gstreamer-rockchip/gst/rockchipmpp/`
- Day 8 运行脚本连 weston 用的环境变量：`run_on_board_with_mac.sh` 第 22～27 行
- Day 8 同步代码（已核对）：
  - `src/sync/media_clock.h`（第 37 行 `updateFromAudio`，第 60 行 `getPositionUs`）
  - `src/pipeline/video_worker.cpp` 第 224～241 行：比时钟早就睡（每次最多 10 ms），晚了超过 `kMaxLateUs` 就丢帧
  - `src/pipeline/video_worker.h` 第 124 行 `kMaxLateUs = 100 * 1000`（100 ms），第 125 行 `kMaxWaitSliceUs = 10 * 1000`

## 验收

- [ ] 屏幕上能看到 2×2 拼图在动；记下用了哪种方式控制播放速度
- [ ] M5：`m5test` 自测通过；`m5` 每路数字和预期表对得上；`mosaic_m5.h265` ffprobe 帧数 = 打印的大图张数
- [ ] 不看代码，口头讲清：以音频时钟为准；视频早了等（每次最多睡 10 ms），晚了超过 100 ms 就丢
- [ ] `m4流程.md`、`m5流程.md` 已写（Claude）
- [ ] **N0 完成**：在 [2_nvr_work/README.md](../2_nvr_work/README.md) 第 8 节把 N0 标成 ✅

## 本周不学

- GStreamer 的插件机制、caps 协商：只要会用 `gst-launch-1.0` 拼一条管道。
- weston/Wayland 的协议：知道"图形程序要连上 weston 的 socket，socket 在 `XDG_RUNTIME_DIR` 下"就够。
- 播放器的 seek 实现（`0_架构设计.md` 第 5.5 节）：N4 回放时再看。

## 本周记录（周末填）

- 实际用时：
- 碰到的问题（写进问题案例库）：
- 下周要调整的：
