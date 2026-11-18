---
name: project-qt-player-plan
description: 2026-11-18 用户定：做 Qt mp4 播放器（界面名 TOnePlayer），MPP 硬解 + AAC 软解走 ALSA，全部由 Claude 写；分支 day8_qt_player；2026-11-19 上板全部通过并提交
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
- 2026-11-19 上板验收（会话"QT播放器"）：
  - 板子 ffmpeg 有 aac 解码器；声卡 card 3 = es8388（板载喇叭），另有 dp0/dp1/hdmi0/hdmi1。
  - M1 前 300 帧 md5 = f95d78c5…，和 3d75e4ad… 不同，但逐帧 framemd5 比对：300 帧内容全部一致，只有第 1～4 帧输出顺序是 2,4,1,3。原因：videotoolbox 的流 SPS 没有 VUI（不知道重排深度），POC 每帧 +1（0,4,2,1,3…）；MPP output_dpb_normal（h264d_dpb.c:1308）开头假定 poc_interval=2，看到奇数差值才改成 1 → 头几帧提前输出。是测试流 + MPP 启发式的问题，不是我们代码的错；seek 后（MPP reset）可能同样乱序几帧。
  - M3 PCM 11522048 字节，md5 9e4c4aa2… ≠ 739c…；逐采样比：576 万个采样只有 4 个差 1（ARM 和 Mac 浮点舍入），算通过。
  - 完整播放还没验完：同一时间另一个会话（rk-work-f3）也在做 day8（桌面图标 TOnePlayer、install_app_on_board_with_mac.sh、默认播 yequ_640x480.mp4、软件音量），两边在同一块板子上互相杀进程，先停下等用户协调。
- 2026-11-19 上板结果（会话 rk-work-f3，也就是上面说的"另一个会话"；两个会话同时用一块板子，"滴滴"声多半是对方在跑 M3）：
  - 播放器全部通过：sync_test 60 秒丢 1 帧（开头乱序那帧），音画结束差 18 ms；夜曲 640×480 / 44.1 kHz 43 秒丢 0 帧。
  - 修了：run 脚本不靠 adb 退出码（板子 adbd 不回传，`adb shell 'test -f 不存在'` 也是 0）；M1 帧数上限；ALSA 走 PulseAudio 时 XRUN 后 snd_pcm_delay 仍返回 0、delay 卡在 1644 → 先看 snd_pcm_state；RGA3 输出格式没有 RGBX（rga_hw_config.c:37），RGBX 只能给 RGA2，而 RGA2 只能访问 4G 以内内存，16G 板子上每帧失败 → 改 RGBA；按钮黑底黑字；退出时 SystemUiClient 析构崩溃（139，remoteNode 先析构，动态副本的元对象没了）；退出顺序改成先停播放。
  - 新功能：TOnePlayer（桌面第 2 页图标，install_app_on_board_with_mac.sh，默认播 /userdata/day8_media/yequ_640x480.mp4）；音量键（正点原子 sysvolume 只把 0～100 写进 /opt/ui/src/apps/resource/volume，各程序自己读 → DesktopVolumeWatcher + 软件音量）；对接 systemui（Qt Remote Objects，local:interfaces，SystemUICommonApi.onServerVariant(程序名, 1 ActivateControl, 命令, 数据)，Hide=5 Show=1 Quit=2 IsActive=4；systemui 约每 2 秒问 IsActive，不回答约 3 秒后把桌面盖回来）；TouchSeekSlider（按哪跳哪 + 拖动时每 250 ms seek）；PLAYER_SCREENSHOT_PATH 截图（weston 没开 --debug，系统截屏 unauthorized）。
  - 退出后喇叭有几秒闷响：正点原子自带「音乐」也有，是板子本身（推断 PulseAudio module-suspend-on-idle 默认 5 秒才挂起声卡），用户定先不处理。
  - moc 转发到 rk-dev 偶尔读到旧文件（"No relevant classes found" / "AutoMoc subprocess error"），隔两秒 touch 头文件再编就好。
  - 39b0df0、8ea268e 两个文档提交顺带提交了 Day 8 的部分修复和 Day 6 暂存的 8 个文件（已推送，没改）。
  - 下一步：用户 11-19 白天自己看 Day 8 代码学习。
- 下一步：插板子 → 确认 aac 解码器和 aplay -l → M1 md5 → M2 `--no-audio` 看画面 → M3 听 + md5 → 完整播放看白条和"嘀"是否同步 → 暂停 / 拖进度条。然后写 m1～m5 文档。
