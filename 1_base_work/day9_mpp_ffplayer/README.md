### todo
* 1. 把/Users/dev/Documents/AV/W_3/SimpleFFPlay6.0 项目移植到当前项目中 —— ✅ 代码已移植，交叉编译通过
* 2. 解码用mpp的硬解； —— ✅ 代码已写（H.264 / H.265 走 MPP，其他格式照旧软解），⏳ 还没上板验证
* 3. 在开发板上可以播放视频，像day8_qt_player 一样， —— ✅ run 脚本、桌面图标安装脚本已写，⏳ 还没上板验证
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
| 操作 | 键盘、鼠标 | 另加触摸：点右上角退出、点底部一条按位置跳转、点其他地方暂停 / 继续 |
| 窗口标题 / 程序名 | ffplay | MppPlayer |

ffplay 原版的代码只改了下面这几处，每处都标了 `MppPlayer`（`grep -rn MppPlayer src` 能全部找出来）：

| 文件 | 改了什么 |
|---|---|
| [src/video/mpp_video_decoder.h/.c](src/video/mpp_video_decoder.h) | **新增**：MPP 硬解，接口仿照 avcodec 的 send / receive / flush |
| [src/pipeline/decoder.h/.c](src/pipeline/decoder.c) | `Decoder` 加 `mppDecoder`；视频的 receive / send / flush 换成 MPP 的 |
| [src/demux/stream.c](src/demux/stream.c) | 打开视频流时创建 MPP 解码器，不再 `avcodec_open2` |
| [src/video/sdl_texture_format.h](src/video/sdl_texture_format.h)、[video_render.c](src/video/video_render.c) | 支持 NV12 纹理，用 `SDL_UpdateNVTexture` 上传 |
| [src/ui/touch_control.h/.c](src/ui/touch_control.h)、[event_loop.c](src/ui/event_loop.c) | **新增**触摸操作；鼠标处理忽略触摸模拟出来的鼠标事件 |
| [src/common/player_options.*](src/common/player_options.h)、[src/app/ffplay_options.c](src/app/ffplay_options.c) | 新增 `-nomppdec` 选项 |
| [src/app/main.c](src/app/main.c) | `program_name` 改成 `MppPlayer` |
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

## 还没验证（等板子连上）

1. `./run_on_board_with_mac.sh -an ...`：画面正常、颜色对（NV12 的 UV 平面位置）、日志里有 `[MppVideoDecoder] 使用 MPP 硬解`
2. 带声音播放，音画同步
3. 触摸：暂停、底部跳转（seek 后 MPP reset）、右上角退出
4. 播完自动退出没有崩溃（帧比解码器晚释放的那条路径）
5. 桌面图标：启动、退出后桌面回来
