---
name: project-day6-status
description: 2026-11-17：Day 6 day6_nvr_mosaic 设计已定（4 路解码 → RGA 拼 2×2 → H.265），参考代码 M1、M2 上板通过，M3 代码写完未编译，M4 未写，m1～m4 文档还没开始写
metadata:
  type: project
---

## 用户定的事（2026-11-17）
- 名字 `day6_nvr_mosaic`（目录、分支都用它，提交前缀 `[day6_nvr_mosaic]`）。2026-11-18 用户建好分支，从 day5_transcoder 的 3be7d7b 分出。
- Day 7（Qt 开发环境 + NVR 预览界面）在另一个分支 `day7_qt_env` 上做过（7f87eb2 等 5 个提交），它的记忆也只在那个分支上；本分支的 MEMORY.md 看不到 Day 7 那一行，是正常的。
- 用户在 1_base_work/day6_nvr_mosaic/ 下用 CLion 建了 Hello World 工程（CMakeLists.txt、main.cpp、.clang-format）和空的 m1流程.md，不要动。
- 方向选 A：输出编码成文件（围绕 MPP 做功能），显示到屏幕放到 M4 并且可选。M1 多解码器同时跑 / M2 RGA 拼 2×2 / M3 编码成 mosaic.h265 / M4（可选）显示到 HDMI。
- 文档路径：`1_base_work/day6_nvr_mosaic/day6编码流程/m1流程.md`～`m4流程.md`，每篇都包含完整实现代码，架构参考 Day 5。**文档还一篇都没写**。

## 参考代码（Claude 写的，用户之后照文档手敲）
- 放在 `1_base_work/day6_nvr_mosaic/claude_reference/`（未跟踪，别提交；原来在 scratchpad，怕被清掉挪过来的）。编译：toolchain `/Users/dev/rk-toolchain/rk3588-toolchain.cmake` + CLion 自带 cmake/ninja，`-B build`；运行：`BUILD_DIR=build ./run_on_board_with_mac.sh m1|m2|m3`。
- 打算写文档时直接从这些文件把代码贴进 md，保证文档代码 = 实测过的代码。
- 架构：common（log、mpp_utils、time_utils、nv12_picture + PictureRect + fillNv12PictureBlack）/ source / decoder（init 多了 requestedLogName）/ rga/rga_compositor（resizeNv12IntoRectByRga，用 improcess 缩放进目标矩形）/ encoder / sink（stream_file_writer、nv12_file_writer）/ pipeline（decode_stage、frame_queue、decode_channel = DecodeStage + FrameQueue + runDecodeLoop、mosaic_compose_stage = composeNextMosaic 每路 pop 一帧贴进格子、encode_stage）/ app（app_config、channel_helpers、m1_decode_channels、m2_compose_mosaic、m3_encode_mosaic，runMosaicEncodePipeline(outputPath) 给 M4 复用）。
- 从 Day 5 复制时顺手按规范改了：logPrint 驼峰、writtenBytes、filePtr、requestedChunkSizeBytes、Doxygen 补全。

## 4 路输入（板子上 ffprobe 实测）
ch0 aaa.264 H.264 1920×1080 1803 帧；ch1 out_720p.h265 H.265 1280×720 1803 帧；ch2 main_1080p.h264 H.264 1080p 60 帧；ch3 out.h265 H.265 1080p 60 帧。4 个文件内容来自同一段视频。

## 实测结果
- **M1 通过**：4 路帧数 1803/1803/60/60，错误帧 0，读入字节 = 文件大小，总用时 2850 ms。
- **stride 发现（重点讲）**：ch0/ch2 H.264 1080p stride 1920×1088；ch3 H.265 1080p **2304×1080**；ch1 H.265 720p 1280×720。来源：RK3588 解码硬件是 vdpu38x（osal/mpp_soc.cpp:917），走 vdpu34x HAL；H.265 init 时设 hevc_hor_align = ALIGN(宽,256)|256（hal_h265d_com.c:88，hal_h265d_vdpu34x.c:102），ver ALIGN 8；H.264 只有宽>1920 或高>1088 才用 256 奇数倍（hal_h264d_vdpu34x.c:1209）。
- /proc/mpp_service 有 rkvdec-core0、core1；板子 16GB 内存。
- **M2 通过**：每路贴进大图帧数 = 解出帧数，1803 张大图，7164 ms，251.7 张/秒，队列都排满过 4 帧；ch2/ch3 解完后格子停在最后一帧。抓图（第 30、300 张）转 PNG 看过，画面、颜色、2304 stride 都对，PNG 在 claude_reference/snapshots/。
- **RGA imfill 在板子上失败**（dmesg：rga_policy: invalid function policy）：RK3588 填色只有 RGA2 能做，RGA2 只能访问 4GB 内存（Rockchip_FAQ_RGA_CN.md:1442）。改成 CPU memset 涂黑（Y=16、UV=128，带 sync_begin/end），文档里一句带过。
- **M3**：mpp_encoder、stream_file_writer、encode_stage、m3_encode_mosaic 已写；2026-11-18 加进 CMake/main，交叉编译 0 警告，**还没上板**（板子没连）。
- **文档（2026-11-18）**：m1流程.md、m2流程.md 已写好（脚本 scratchpad/docgen/gen.py 从 claude_reference 生成代码块和行号，模板 m1.tpl.md、m2.tpl.md 也在 scratchpad，可能被清掉；被清掉就直接改 md）。图：doc/img/arch/day6_dataflow.svg（渲染检查过）、doc/img/m2/mosaic_index30.jpg、mosaic_index300.jpg。m3、m4 文档等上板后写。

## 下一步
1. M3 上板，验收：ffprobe hevc 1920×1080 1803 帧、ffmpeg 解码 0 报错，跑两次 md5 是否一致。
2. M4：mkfifo + runMosaicEncodePipeline(fifo 路径)，板子上 `gst-launch-1.0 filesrc location=FIFO ! h265parse ! mppvideodec ! waylandsink`（要 `XDG_RUNTIME_DIR=/var/run`）。播放速度会不会太快（没有时间戳）还没测，可能要 sync=false 或者自己按 30 fps 节流。
3. 写 m1～m4 文档（结构按 [[feedback-milestone-doc-structure]]，图放 doc/img/，SVG 要渲染检查）。

相关：[[project-day5-status]]
