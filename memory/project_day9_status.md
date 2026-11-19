---
name: project-day9-status
description: Day 9 MppPlayer（SimpleFFPlay6.0 移植到板子 + MPP 硬解）进度断点；2026-11-19 代码写完、交叉编译通过，还没上板
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
- **还没上板**：写代码时 adb 只连着一部三星手机（SM_A5460，序列号 R5CWA1RSV3T），板子没连。同时连着时要 ADB_SERIAL。
- 都还没提交（用户没要求）。

## 下一步
- 板子连上后先跑 `-an`（不出声；带声音播放前先问用户，见 feedback_ask_before_sound.md），按 README「还没验证」清单逐条验。
