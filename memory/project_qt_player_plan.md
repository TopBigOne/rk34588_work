---
name: project-qt-player-plan
description: 2026-11-18 用户定：做 Qt mp4 播放器，视频 MPP 硬解，AAC 软解 + ALSA 输出，全部由 Claude 写；分支 day8_qt_player（worktree 在 ../rk_work_day8_qt_player）
metadata:
  type: project
---

## 用户定的事（2026-11-18）
- 做一个 Qt 播放器，播 mp4：libavformat 只拆封装 → `h264_mp4toannexb` / `hevc_mp4toannexb` → MPP 硬解 → RGA NV12 转 RGBA → Qt `QImage` 显示。
- 声音必须有：AAC 用 ffmpeg 软解（RK3588 没有音频解码硬件，MPP 只管视频）→ libswresample → **ALSA**（`snd_pcm_writei`）。Oboe / AAudio / OpenSL ES 是 Android 的输出接口，板子是 buildroot Linux，用不了。音视频同步以音频时钟为准。
- **代码全部由 Claude 写**（和以往"用户写 MPP 核心代码"的分工不一样，是用户这次明确要求的）。

## 实测（2026-11-18，板子上）
- 板子 ffmpeg 4.4.1，没有 rkmpp 编解码器；有 `mov,mp4` 拆封装器、`h264_mp4toannexb`、`hevc_mp4toannexb`。
- GStreamer 有 rockchipmpp 插件（mppvideodec、mpph264enc、mpph265enc……）。
- sysroot 有 libasound、libswresample、libpulse。aac 解码器、`aplay -l` 还没在板子上确认（查的时候板子断开了）。

## 估算（给用户的，非实测）
- C++ 约 1800～2000 行有效代码，带注释约 3500 行；输出 token 约 25～35 万，输入约 1 千万。
- 最难的是 seek（清队列、重置 MPP、从关键帧重解）。

## 可以复用
- Qt 开发环境已在分支 `day7_qt_env` 搭好（Qt 5.15.8、moc 等转发到 rk-dev、run 脚本、wayland 环境变量），详见那个分支的 memory/project_day7_qt_env.md。屏幕是 DSI 竖屏 1080×1920，桌面是正点原子 systemui 盖在 weston 上。
- Day 5 的 frame_queue、Day 6 claude_reference 里的 decoder / rga。

## 分支
- 2026-11-18 用户选：从 day7_qt_env 新开分支 `day8_qt_player`，一开始放在单独的 worktree，用户要求后改为在原目录 rk_work 切到这个分支（worktree 已删）。用户 Day 6 的暂存文件随工作区带过来了，仍是暂存状态，提交 day8 时要用 `git commit -- 路径` 只提 day8 的；Day 6 分支上 memory/MEMORY.md 的改动在 stash "day6-memory-index-before-day8-switch-20261118"，切回 day6_nvr_mosaic 后 `git stash apply` 那一条。Day 6 以后再收尾。 那条 stash 里的改动其实只有 MEMORY.md 多出的两行索引（Day 6 进度断点、Qt 播放器计划），stash 记录的暂存区里也带着 day6 文件，apply 冲突的话直接手动把这两行加回去，再 drop 掉这条 stash。

## 进度（2026-11-18）
- 用户说"直接在 day8_qt_player 里实现"，代码全部写完，CLion 自带 cmake + 工具链交叉编译通过，-Wall -Wextra -Wshadow 0 警告，**还没上板**（写代码期间板子一直没连上）。
- 用户之前在 CLion 建过 Hello World 工程：1_base_work/day8_qt_player/CMakeLists.txt 被我覆盖成播放器的（原来是 CLion 默认模板）；根目录 main.cpp（Hello World）没动、也没编进程序，可以删。CLion 里要加 RK3588 的 CMake profile（同 day7），编译目录 cmake-build-rk3588-debug（命令行已经配置过）。
- 名字和设计稿的差别：线程类叫 DemuxWorker / VideoWorker / AudioWorker，音频解码器叫 SoftwareAudioDecoder，RGA 转换是函数 convertNv12FrameToRgbxByRga，共用状态 pipeline/playback_state.h。架构文档已同步。
- 测试视频 test_media/sync_test_1080p.mp4（make_test_media.sh，Mac 的 ffmpeg 没有 libx264 和 drawtext，用 h264_videotoolbox + testsrc + drawbox 白条 + sine beep_factor）：H.264 High、has_b_frames=2、1800 帧，AAC LC 48 kHz 双声道 2814 包。test_media/、*.pcm 已加进 .gitignore。
- 上板验收用的 md5（Mac ffmpeg 4.4 算的）：
  - M1 前 300 帧：`m1 … m1.nv12 300` 应为 3d75e4ad8e7e4276081c013fc1b2447e（全部 1800 帧 5.6 GB 太大，别全解）
  - M3 PCM：11522048 字节，739c1bf50809b1ab7abfb381aa33b6cc
- 下一步：插板子 → 确认 aac 解码器和 aplay -l → M1 md5 → M2 `--no-audio` 看画面 → M3 听 + md5 → 完整播放看白条和"嘀"是否同步 → 暂停 / 拖进度条。然后写 m1～m5 文档。
