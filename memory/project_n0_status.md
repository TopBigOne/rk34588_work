---
name: project-n0-status
description: 2026-11-20：一年计划 N0（n0_nvr_mosaic）和 N1～N14 准备工作的进度断点：N0 参考代码 M1～M5 已交叉编译未上板，m1～m5 文档都写了，N1～N14 计划齐了，代码还没写
metadata:
  type: project
---

## 目录和约定（2026-11-20 用户定）
- 一年计划的工程放 `3_one_year_work/NX_work/nX_xxx/`（N0～N14），周计划在 `一年学习计划/第XX周_*.md`。周计划、README 里写的 `2_nvr_work/nX_xxx/` 作废。
- 每个阶段工程下有 `claude_reference/`：Claude 的参考实现，**要提交**（2026-11-20 用户先说不提交，同日改成要提交；`.gitignore` 只忽略 `build/`、`build-*/`）。里面按周分子目录（名字 = 周计划文件名），每个周目录是能单独编译的完整快照。没有阶段的 12 周放 `99_other_work/claude_reference/`。
- 用户自己的代码写在工程根的 `src/`；用户 CLion 建的 `CMakeLists.txt`、`main.cpp`、`.clang-format` 不动。
- 分支 `n0_nvr_mosaic`（用户建，初始提交 ecea121），提交前缀 `[oy_n0_nvr_mosaic]`；后面阶段 `[oy_nX_xxx]`。
- 用户 2026-11-20 说：参考代码**先只要代码**，注释和技术方案等学到那个阶段再补。

## N0 n0_nvr_mosaic
- 设计 `doc/0_设计.md`（用户已批准）、计划 `doc/1_计划.md`、执行记录 `.superpowers/sdd/1_计划/progress.md`（含 Ruling）。
- 方案 B：Day 6 全部重写，Day 8 只读；新增 M5：4 路不同帧率（30/15/60/30+第 300 帧停 2 秒）按主时钟对齐拼图（每格：到点帧取最新、旧的丢弃，没到点重复）。
- 参考代码：`claude_reference/第01周_N0_Day6的M3上板/`（M1～M3，从 Day 6 复制）、`第02周_N0_Day6的M4和Day8上板/`（+M4 FIFO+gst 上屏、`run_m4_on_board_with_mac.sh`；+M5 `sync/master_clock`、`FrameQueue::tryPopIfDue`、`setPtsFrameRate`、`setDecodeStall`、`composeAlignedMosaic`、`m5test` 自测、`make_m5_inputs.sh`）。两份都交叉编译 0 警告，**都没上板**（编译目录 `build`）。
- M5 输入已生成在 `/Users/dev/Documents/AV/rk_test_data/`：n0_ch1_15fps.h264 902 帧、n0_ch2_60fps.h264 3606 帧（`-vf fps` 会因裸码流时间戳乱生成几百 MB，改用 setpts 按帧号）；还没 adb push。
- 文档：`doc/m1流程.md`、`m2流程.md`（Day 6 底稿迁移，实测数字是 Day 6 的）、`m3流程.md`（247 行，未上板）、`m4流程.md`、`m5流程.md`（2026-11-20 写完，未上板，数字都标“推断”，第 6 节留了实测记录模板；m5 配图 `doc/img/m5/m5_clock_timeline.svg`，Chrome 渲染检查过）。用户还没看过 m4、m5 文档，也没提交。
- 第 01、02 周计划已改成 n0 路径；第 02 周加了 M5，删了 Day 8 上板（11-19 已通过）。

## N1～N14
- 14 份计划都在各自 `claude_reference/0_计划.md`。跨阶段接口以 `3_one_year_work/0_跨阶段接口.md` 为准（N2 的 EncodedFrame/StreamInfo/RtspReceiver，N3 的 Mp4FileWriter 等）。Claude 代做的决定和要买的硬件在 `3_one_year_work/0_计划汇总和待确认.md`，**用户还没过目**。
- 代码一行都没写（N2 有 6 个被打断的半成品文件）。写代码的通用约定在 `3_one_year_work/0_参考代码编写约定.md`（但用户已改成“只要代码、不要注释和文档”，用前先改这份）。

## token 教训（2026-11-20）
- 一晚上同时开 5 个计划子助手 + 7 个代码/文档子助手，每个约 20 万 token，撞了组织月度消费上限，全部中断。用户明确说太费。
- 以后：一次只开一个子助手、说明压短、先报估算再做；主会话上下文太长时换新会话。实测：一个写文档的子助手约 9.4 万 token，周额度约涨 1 个百分点。

## N3 n3_recorder
- 2026-11-20 第 10 周 M1 参考代码写完：交叉编译 0 警告、Mac 单测和 Mac 端到端通过（aaa.264 → 60 秒 MP4，1800 帧），未上板，RTSP 输入没跑（SRS 没编译）。第 11 周 M2 分段也写完（Mac 端到端：10 分钟 → 10 个 60.1 秒的文件，帧数合计 18000）。细节和接口在 `claude_reference/1_实现记录.md`。第 13 周 M3 SQLite 索引也写完（Mac：10 条记录、大小和首尾相接都对、查询走索引、Ctrl+C 补结束时间、kill -9 留 NULL）。第 14 周 M4 也写完，**N3 参考代码全部完成**（Mac：配额 200 MB 剩 3 段 166 MB；kill -9 只丢内存里那一个片段，靠 AVFMT_FLAG_FLUSH_PACKETS；启动修复后 NULL 为 0；故障注入测了目录被删、数据库损坏）。N3 全部没上板。下一个阶段按顺序是 N10。

## N10 n10_ai_event
- 2026-11-20 第 15 周 M0 参考代码写完：`RknnModel`（RAII）、`detection_types.h`、`m0` 单张图检测（CPU letterbox + SDK demo 原样复制的 postprocess）、`check_rknn_env_on_board.sh`、`tools/convert/convert_yolov5.sh`（rk-dev 上跑）。交叉编译 0 警告；未上板；转换脚本没跑（要在 rk-dev 装几 GB 依赖，等用户点头）。模型不进仓库，run 脚本从 SDK 直接 push。细节在 `N10_work/n10_ai_event/claude_reference/1_实现记录.md`。同日第 16 周 M1 也写完：RGA 前处理、自写 YOLOv5 后处理 + 按类别 NMS、DetectStage、`m1`（视频）和 `m1cmp`（和 demo 后处理对比）；交叉编译 0 警告，Mac 单测 33 项全过（和 demo 对比最大误差 2 像素；发现 demo 的 NMS 会跨类别抑制）；未上板，缺有人的测试视频。同日第 17 周 M2 也写完：NV12 画框、预录缓冲、事件状态机、EventRecorder（N3 的 FfmpegFrameSource + Mp4FileWriter）、SQLite events 表、LatestSlot、`m2`；交叉编译 0 警告，Mac 单测 84 项全过（aaa.264 端到端：20～25 秒有人 → 25.4 秒事件文件，0 解码错误）；未上板。同日第 18 周 M3 也写完，**N10 参考代码全部完成**：YOLOv8 后处理（DFL，和 model_zoo process_i8 对比一致）、`RknnModel::duplicateFrom/setCoreMask`、`DetectWorkerPool`（多 context 多线程，结果按顺序回调）、`m3bench`/`m3models`/`m3cmp`/`m3fault`、`convert_yolov8.sh`（没跑）；交叉编译 0 警告，Mac 单测 106 项全过；未上板。YOLOv8 模型还没有：正点原子 zip 自带的是 toolkit 2.3.2 转的，和板子 runtime 2.0.0b0 推断不配。下一个阶段 N5。
- 注意：2026-11-20 `adb devices` 里的 R5CWA1RSV3T 是三星手机 SM-A5460，不是板子。

## 下一步
1. 用户看 m4、m5 文档；要提交时 md 一批、memory 一批。问题案例库文件还不存在，m4（上屏快放）、m5（实测和推断的差距）的候选条目上板后再写。
2. N1～N14 代码：只写代码 + 交叉编译通过，一次一个阶段。**2026-11-20 用户定跳过 N1、N2**（见 [[feedback-rtsp-use-only]]），从 N3 开始：N3 → N10 → N5 → N6 → N11 → N7 → N12 → N4 → N9 → N8 → N13 → N14；估计共 120 万～200 万 token。
3. 板子回来后：按 `doc/1_计划.md` 的 Task 1、2、5～8 上板验证（m1～m5、m5test）。

相关：[[project-day6-status]]、[[feedback-architecture-first]]
