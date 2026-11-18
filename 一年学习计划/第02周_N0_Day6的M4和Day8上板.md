# 第 02 周（2026-11-30 ～ 12-06）：N0 · Day 6 M4 + Day 8 上板

> **所属**：Q1 · [N0 收尾 Day 6、Day 8](../2_nvr_work/README.md) ｜ [← 第 01 周](第01周_N0_Day6的M3上板.md) ｜ [第 03 周 →](第03周_N1_M1_RTSP信令.md) ｜ [概览](../一年学习计划.md)
> **投入**：主线约 15 小时，并行约 5 小时（20 小时是上限，不是指标）
> **细化程度**：详细
> **状态**：⬜

## 本周目标

把 Day 6 拼好的 2×2 画面显示到板子屏幕上（M4），Day 8 播放器上板跑通，并能讲清播放器"视频追音频"是怎么做的。

## 学完能回答的问题

1. 播放器以哪个时钟为准？为什么通常选音频，而不是视频？
2. 一帧视频解出来时，如果比时钟"早"，播放器怎么做？如果"晚"了 200 ms，又怎么做？阈值是多少？
3. 裸的 H.265 码流（没有时间戳）交给播放器，播放速度由什么决定？为什么可能播得飞快？
4. 板子上运行图形程序，为什么要设置 `XDG_RUNTIME_DIR`？不设会怎样？

## 开始前要准备好

- 第 01 周的 M3 已跑通，`mosaic.h265` 验收通过。
- 板子接好屏幕，weston 桌面能显示（Day 7 已经在板子屏幕上跑过 Qt）。
- Day 8 测试视频：`1_base_work/day8_qt_player/test_media/sync_test_1080p.mp4`（已核对存在；生成脚本 `make_test_media.sh`）。

## 主线任务

> 代码由谁写：Day 6 M4 和 Day 8 播放器都由 Claude 写（Day 8 当时就定的"全部由 Claude 写"）。本周你的重点是**跑通 + 读懂同步逻辑**，不手敲。

| # | 任务 | 预计 | 做完得到什么 |
|:---:|---|:---:|---|
| 1 | Day 6 M4 第一版：板子上 `mkfifo`，M3 的编码输出写进 FIFO，另一边 `gst-launch-1.0 filesrc location=<FIFO> ! h265parse ! mppvideodec ! waylandsink`（要 `XDG_RUNTIME_DIR=/var/run`） | 3 h | 屏幕上 2×2 画面在动 |
| 2 | 观察播放速度。裸码流没有时间戳，可能播得太快或太慢；试 `waylandsink sync=false`，或在编码端按 30 fps 节流 | 2 h | 记下哪种做法速度正常、为什么（**这是推断要验证的点**，结果写进问题案例库） |
| 3 | Claude 写 `m4流程.md`；你读一遍 | 1.5 h | — |
| 4 | Day 8 上板：`1_base_work/day8_qt_player/run_on_board_with_mac.sh`，用 `sync_test_1080p.mp4` 播放 | 2 h | 画面和声音都正常，口型（或测试图案）对得上 |
| 5 | 读 `doc/0_架构设计.md` 第 2 节数据流、第 5.3 节音频时钟 | 1.5 h | 能画出 demux → 视频线程 / 音频线程 → 时钟 的图 |
| 6 | 读 `src/sync/media_clock.cpp`：`reset`、`updateFromAudio`、`getPositionUs` | 2 h | 能说清"时钟的位置是谁在更新、怎么往前走" |
| 7 | 读 `src/pipeline/video_worker.cpp` 第 224～241 行（第 3 步：等到该显示的时间）和 `video_worker.h` 第 124～125 行的两个常量 | 2 h | 能回答本周问题 2 |
| 8 | 填本周记录 | 1 h | — |

## 并行任务（E 原理和工具 / F 面试基本功）

| 线 | 任务 | 预计 |
|---|---|:---:|
| E | 问题案例库补 1～2 条（M4 播放速度、Day 8 上板碰到的问题） | 2 h |
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
- [ ] Day 8 在板子上播放 `sync_test_1080p.mp4`，音画同步
- [ ] 不看代码，口头讲清：以音频时钟为准；视频早了等（每次最多睡 10 ms），晚了超过 100 ms 就丢
- [ ] `m4流程.md` 已写（Claude）
- [ ] **N0 完成**：在 [2_nvr_work/README.md](../2_nvr_work/README.md) 第 8 节把 N0 标成 ✅

## 本周不学

- GStreamer 的插件机制、caps 协商：只要会用 `gst-launch-1.0` 拼一条管道。
- weston/Wayland 的协议：知道"图形程序要连上 weston 的 socket，socket 在 `XDG_RUNTIME_DIR` 下"就够。
- 播放器的 seek 实现（`0_架构设计.md` 第 5.5 节）：N4 回放时再看。

## 本周记录（周末填）

- 实际用时：
- 碰到的问题（写进问题案例库）：
- 下周要调整的：
