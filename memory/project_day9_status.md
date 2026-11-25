---
name: project-day9-status
description: Day 9 MppPlayer（SimpleFFPlay6.0 移植到板子 + MPP 硬解）进度断点；2026-11-19 上板通过、加了控制栏，都已提交
metadata:
  type: project
---

## 用户定的事（2026-11-19，day9_mpp_ffplayer/README.md 的 todo）
- 把 /Users/dev/Documents/AV/W_3/SimpleFFPlay6.0（FFmpeg 6.0 ffplay 拆分版，C + SDL2）移植到 1_base_work/day9_mpp_ffplayer，视频用 MPP 硬解，板子上能播（像 day8），名字 MppPlayer。
- 用户选：**交叉编译 FFmpeg 6.0**（不改回 4.4 API）、**保留 SDL 界面**（不换 Qt）、**NV12 直接上传 SDL**（不走 RGA）、**代码全部由 Claude 写**。
- 用户中途要求：做完写一份交叉编译 FFmpeg 6.0 的文档 → doc/交叉编译FFmpeg6.0.md（已写）。

## 现状（2026-11-19）
- FFmpeg 6.0 已编好：build_ffmpeg6_for_board.sh，git archive 出 ~/rk-toolchain/ffmpeg6-src，在 rk-dev 里编，装到 ~/rk-toolchain/ffmpeg6-rk3588；板子放 /userdata/ffmpeg6/lib（push_ffmpeg6_to_board.sh，md5 一样就跳过）。
- MppPlayer 交叉编译通过，新文件 0 警告；rpath = /userdata/ffmpeg6/lib:/userdata/mpp_build/lib。
- 改动点都标了 "MppPlayer"：video/mpp_video_decoder.*（新增，引用计数让帧比解码器晚释放）、pipeline/decoder.*、demux/stream.c、NV12 纹理（SDL_UpdateNVTexture）、ui/touch_control.*（新增触摸：右上角退出/底部跳转/其他暂停）、-nomppdec 选项。
- 桌面图标：install_app_on_board_with_mac.sh + board/start_mppplayer.sh（setsid 脱离 systemui → 停 systemui/sysvolume → 播 → 重启 systemui）。
- 同时连着手机等别的 adb 设备时要 ADB_SERIAL。

## 上板（2026-11-19 晚，板子 adb 序列号 3a6aff9a20ba7de4，adb 显示成 Nexus_4；4ecdd6fc unauthorized 不是板子）
- 坑 1：FFmpeg 库要 fmod/fmodf@GLIBC_2.38，板子 glibc 2.37 → glibc237_fmod_compat.h（.symver 绑 GLIBC_2.17）+ --extra-cflags=-include，重编；脚本最后 objdump 检查。
- 坑 2：板子 SDL（2.0.22，sysroot 是 2.28.5）GLES2 渲染器把 IYUV/YV12/NV12/NV21 各报两次 → av_filters.c 去重，否则 Duplicated pixel format 直接退出。
- sync_test 1080p：NV12 纹理直传；60 s 播完退出码 0，fd=4～5 都在开头 0.3 s；带声音 A-V -0.035 s；用户听到声音。
- 默认音量改 10%（用户定，startup_volume）。桌面图标用户试过 OK。
- 以上已提交。

## 控制栏（2026-11-19 晚，已提交）
- src/ui/control_bar.*：进度条（拖动、松手 seek）、时间（内置 5×7 点阵字）、播放/暂停、退出；4 秒自动隐藏、暂停常显；替换掉 touch_control。
- MPPPLAYER_SCREENSHOT_PATH 调试截图（SDL_RenderReadPixels → BMP）：截图确认布局和颜色都对。
- 触摸交互：用户说“可以了”，但桌面日志里装新版后没有启动记录；seek 路径在旧版（底部触摸条）里跳转 7 次通过。
- yequ_640x480.mp4 片长 3:53.97（之前估的 43 秒是错的）。

## 文档和格式化（2026-11-25，已提交并 push）
- doc/3_播放器怎么管理AVPacket和AVFrame.md + doc/img/player_pipeline/player_pipeline.svg：PacketQueue / MyAVPacketList、FrameQueue 环形数组、引用怎么转手、谁等谁、释放帧 = buffer 还给 MPP、seek 的 serial。
- doc/我不理解的代码.md：第 1 条不透明指针（为什么 struct MppVideoDecoder 定义放 .c）、第 2 条 AVBSFContext（avcC → Annex B）。
- 用户换了新 .clang-format（InsertBraces、AlignConsecutiveAssignments 等），7 个源文件按它重排，单独一个提交 d951f24。
- audio_visualizer.c 算 rdft_bits 的空循环 for 改成 while + 注释（8e2144a），文件头注明了这个例外。
- 用户问过能不能播 flv：板子 FFmpeg 6.0 有 flv demuxer 和 h264_mp4toannexb，H.264 的 flv 推断能走 MPP；FLV 里的 H.265 推断不行（6.0 不支持 Enhanced FLV）。**没上板实测**：当时 adb 只连着三星手机 R5CWA1RSV3T，板子没插。
