### todo
* 1. 把/Users/dev/Documents/AV/W_3/SimpleFFPlay6.0 项目移植到当前项目中 —— ✅ 代码已移植，交叉编译通过
* 2. 解码用mpp的硬解； —— ✅ H.264 / H.265 走 MPP，其他格式照旧软解；2026-11-19 上板播 1080p 测试视频通过（不出声）
* 3. 在开发板上可以播放视频，像day8_qt_player 一样， —— ✅ run 脚本上板通过（画面 + 声音）；✅ 桌面图标用户试过 OK（2026-11-19）
* 4. 播放器名称叫：MppPlayer —— ✅

---

# MppPlayer

SimpleFFPlay6.0（FFmpeg 6.0 的 ffplay 拆分版）移植到 RK3588 板子，视频改用 MPP 硬解。
界面仍是 ffplay 原来的 SDL2 窗口（板子上走 Wayland，显示在 weston 上）。

## 和 SimpleFFPlay6.0 的区别

| | SimpleFFPlay6.0（Mac） | MppPlayer（RK3588） |
|---|---|---|
| FFmpeg | 本机编的 6.0 | 交叉编译的 6.0，见 [doc/交叉编译FFmpeg6.0.md](doc/交叉编译FFmpeg6.0.md) |
| 视频解码 | avcodec 软解 | **H.264 / H.265 用 MPP 硬解**，其他格式照旧软解；`-nomppdec` 强制软解 |
| 显示格式 | YUV420P | NV12 直接上传 SDL 纹理（GPU 做颜色转换），不经过 swscale |
| 操作 | 键盘、鼠标 | 另加底部控制栏：进度条（拖动跳转）、播放时间 / 总时间、播放 / 暂停按钮、退出按钮；触摸和鼠标左键都能用 |
| 窗口标题 / 程序名 | ffplay | MppPlayer |

ffplay 原版的代码只改了下面这几处，每处都标了 `MppPlayer`（`grep -rn MppPlayer src` 能全部找出来）：

| 文件 | 改了什么 |
|---|---|
| [src/video/mpp_video_decoder.h/.c](src/video/mpp_video_decoder.h) | **新增**：MPP 硬解，接口仿照 avcodec 的 send / receive / flush |
| [src/pipeline/decoder.h/.c](src/pipeline/decoder.c) | `Decoder` 加 `mppDecoder`；视频的 receive / send / flush 换成 MPP 的 |
| [src/demux/stream.c](src/demux/stream.c) | 打开视频流时创建 MPP 解码器，不再 `avcodec_open2` |
| [src/video/sdl_texture_format.h](src/video/sdl_texture_format.h)、[video_render.c](src/video/video_render.c) | 支持 NV12 纹理，用 `SDL_UpdateNVTexture` 上传 |
| [src/ui/control_bar.h/.c](src/ui/control_bar.h)、[event_loop.c](src/ui/event_loop.c)、[video_render.c](src/video/video_render.c) | **新增**控制栏（用 SDL 矩形 + 内置 5×7 点阵字画，不依赖字体文件）；触摸和鼠标左键先交给控制栏；`video_display` 上屏前画控制栏；`MPPPLAYER_SCREENSHOT_PATH` 调试截图 |
| [src/common/player_options.*](src/common/player_options.h)、[src/app/ffplay_options.c](src/app/ffplay_options.c) | 新增 `-nomppdec` 选项；默认音量改成 10%（原版 100%，板载喇叭太响），`-volume N` 仍可改 |
| [src/app/main.c](src/app/main.c) | `program_name` 改成 `MppPlayer` |
| [src/filter/av_filters.c](src/filter/av_filters.c) | 像素格式列表去重：板子的 GLES2 渲染器把 IYUV、YV12、NV12、NV21 各报两次，原版会报 `Duplicated pixel format`，播放器直接退出 |
| [src/fftools/config.h](src/fftools/config.h)、`config_components.h` | 换成 aarch64 交叉编译时生成的那份 |

`origin/ffplay.c` 是 FFmpeg 6.0 原版，不参与编译，对照用。

## MPP 解码怎么接进 ffplay

```
read_thread ──AVPacket（avcC）──▶ videoq ──▶ video_thread
                                              │ decoder_decode_frame（pipeline/decoder.c）
                                              ▼
                      mppVideoDecoderSendPacket：h264_mp4toannexb → decode_put_packet
                      mppVideoDecoderReceiveFrame：decode_get_frame → MppFrame（NV12）
                                              │ 包成 AVFrame（不复制像素，buf[0] 引用 MppFrame）
                                              ▼
                      滤镜（NV12 原样通过）→ pictq → video_refresh → SDL_UpdateNVTexture → 屏幕
```

要点：
- **帧的生命周期**：解出来的帧会在 pictq 里留一会儿，而 ffplay 关闭流时是先销毁解码器、后清帧队列。所以解码器带引用计数，每个没释放的 AVFrame 都持有它的一份引用，最后一帧释放时才真正 `mpp_destroy`
- **MPP 输入队列满了**（`MPP_ERR_BUFFER_FULL`）：包先留着，`decoder_decode_frame` 每 1 ms 再取一次帧，直到包送进去；期间照常检查退出和 seek
- **seek / 播完**：`mppVideoDecoderFlush` → `MppApi::reset`
- **cache**：内存池用 `MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE`，CPU 读之前 `mpp_buffer_sync_ro_begin`，读完 `sync_ro_end`
- 只支持 8 bit 4:2:0（NV12）；10 bit 片源 MPP 输出 NV15，SDL 不认，会报错

## 控制栏

```
  ━━━━━━━━━━━━━━━●─────────────────────────   进度条：按住拖动，松手跳过去
  [▶/❚❚]  01:23 / 04:56                [✕]   播放/暂停、播放时间/总时间、退出
```

- 启动时显示；播放中 4 秒没操作自动隐藏；暂停时一直显示
- 点画面：显示 / 隐藏控制栏。隐藏时第一下点击只把它叫出来，不会误触按钮
- 按钮要在同一个按钮上按下、抬起才算点击（按下后滑走就当没点）
- 尺寸按屏幕短边缩放（竖屏 1080×1920 时按钮约 130 像素）

调试截图：`MPPPLAYER_SCREENSHOT_PATH=/tmp/shot.bmp ./MppPlayer ...`，第一次显示画面 3 秒后把整个画面（视频 + 控制栏）存成 BMP，`adb pull` 回来看。

## 怎么用

第一次先编 FFmpeg 6.0（约 5 分钟，只要编一次）：

```bash
./build_ffmpeg6_for_board.sh
```

板子用 OTG 线连 TypeC0 口，`adb devices` 能看到板子后：

```bash
./run_on_board_with_mac.sh                                  # 编译 → 推 FFmpeg 库和程序 → 播 sync_test_1080p.mp4（有声音）
./run_on_board_with_mac.sh -an /userdata/day8_media/sync_test_1080p.mp4   # 不出声
./run_on_board_with_mac.sh -nomppdec -an /userdata/day8_media/sync_test_1080p.mp4   # 软解，对比用
./install_app_on_board_with_mac.sh                          # 装成桌面第 2 页的 MppPlayer 图标
```

同时连着手机等别的 adb 设备时，用 `ADB_SERIAL=板子序列号 ./run_on_board_with_mac.sh`。

参数和 ffplay 一样（`-fs` 全屏、`-x/-y` 窗口大小、`-an/-vn`、`-ss` 起始时间、`-autoexit`、`-loop`……）。

从桌面图标启动时：全屏播 `/userdata/day8_media/yequ_640x480.mp4`，播完自动退出。播放期间会先停掉 systemui（SDL 程序没法像 day8 的 Qt 程序那样跟 systemui 通信，不停的话桌面会把播放器盖住），退出后再启动它。日志：`adb shell cat /userdata/apps/mppplayer/mppplayer.log`。

## 文件

| 文件 | 作用 |
|---|---|
| [build_ffmpeg6_for_board.sh](build_ffmpeg6_for_board.sh) | 交叉编译 FFmpeg 6.0 |
| [push_ffmpeg6_to_board.sh](push_ffmpeg6_to_board.sh) | 把 FFmpeg 6.0 的库推到板子 `/userdata/ffmpeg6/lib`（一样就跳过） |
| [run_on_board_with_mac.sh](run_on_board_with_mac.sh) | 编译、推送、在板子上运行 |
| [install_app_on_board_with_mac.sh](install_app_on_board_with_mac.sh) | 装成桌面图标 |
| [board/start_mppplayer.sh](board/start_mppplayer.sh) | 板子上点图标时执行的启动脚本 |
| [assets/mppplayer_icon.png](assets/mppplayer_icon.png) | 桌面图标 |
| [doc/交叉编译FFmpeg6.0.md](doc/交叉编译FFmpeg6.0.md) | 交叉编译 FFmpeg 6.0 的完整说明 |

## 上板结果（2026-11-19，板子 adb 序列号 3a6aff9a20ba7de4）

`-an -autoexit sync_test_1080p.mp4`（H.264 High 1920×1080 30 fps，60 秒）：
- 日志：`使用 MPP 硬解 h264`；info change `1920x1080，stride 1920x1088，buf_size 4177920`
- 显示路径：滤镜输出 `pixfmt:nv12`，`Created 1920x1080 texture with SDL_PIXELFORMAT_NV12`（没经过 swscale）
- 播到 59.94 秒，退出码 0；退出时打印 `[MppVideoDecoder] 已销毁`（帧比解码器晚释放的路径没崩）
- 丢帧 `fd=5`，都在开头 0.3 秒内，之后不再丢；结束时 `M-V -0.016`
- 板子的 SDL 是 2.0.22（sysroot 是 2.28.5，soname 一样）；MppPlayer 用到的 55 个 SDL 函数板子上都有

带声音（`-volume 20 -autoexit`，同一个视频）：声音正常（用户听过）；音频 AAC 软解 → aresample（fltp → s16，48 kHz）→ SDL → ALSA；
`A-V` 全程 -0.034～-0.038 秒（画面比声音早约 35 ms），丢帧 4 帧都在开头 0.3 秒内；退出码 0

上板踩的坑：
1. FFmpeg 库报 `GLIBC_2.38 not found` → 见 [doc/交叉编译FFmpeg6.0.md](doc/交叉编译FFmpeg6.0.md) 第 9 节
2. `Duplicated pixel format`，播放器一启动就退出 → av_filters.c 去重（上表）

桌面图标（2026-11-19 用户在板子上点开试过，说“可以了”）：日志 `参数=[-fs -autoexit .../yequ_640x480.mp4]`，
MPP 硬解 640x480（stride 640x480），片长 3:53.97，23:23:48 开始 → 23:25:26 结束，退出码 0。
那次（旧版：底部触摸条跳转）用户跳转了 7 次（83%、77%、85%、79%、81%、84%、0%），每次都接着播 → seek + MPP reset 路径上板通过

控制栏截图（2026-11-19，`-an -fs -t 6`，1080×1920 竖屏）：进度条、`00:03 / 01:00`（和测试视频画面里的数字 3 对得上）、暂停 / 退出按钮位置都对；
测试图彩条顺序（青、品红、蓝、黄、绿、红）正常 → NV12 的 UV 平面位置对，颜色没问题

## 还没验证

1. 控制栏的触摸交互：拖进度条、播放 / 暂停、退出、点画面显示 / 隐藏、4 秒自动隐藏。
   用户 2026-11-19 说“可以了”，但桌面日志里装控制栏版之后没有新的启动记录，以日志为准算“没验”
