# 验证 M3 代码：零拷贝导入 + 编码 1 帧

> M3 在 M2 的基础上做的事：每块缓冲区 `VIDIOC_EXPBUF` 导出 dma_buf fd → `mpp_buffer_import` 包成 MppBuffer → 用摄像头的 stride 初始化编码器 → 取 SPS/PPS → DQBUF 一帧 → **编码器直接读这块缓冲区**（零拷贝）→ 写码流 → QBUF → 送 EOS。
> 代码：`src/main.cpp`；写法见 [具体编码day3_cam2h264的流程.md](../具体编码day3_cam2h264的流程.md) 第 4 节；M2 的验证见 [验证M2代码.md](验证M2代码.md)。
> 测四类：**正常流程**、**零拷贝本身**、**故意制造错误**、**资源泄漏**。

所有命令都在 Mac 的 day3 目录下执行：
```bash
cd /Users/dev/Documents/AV/rk_work/day3_cam2h264
```
摄像头装在 MIPI CSI3，节点 `/dev/video22`。

---

## 测试记录

| 日期 | 测了哪些 | 结果 |
|---|---|---|
| 2026-10-18 | 一、①～⑥ | ✅ 通过：stride 1920×1080、header 40 字节、1 帧 I 帧 142,280 字节、退出码 0、解码画面颜色正常（画面是倒的，见 ⑥） |
| 2026-10-18 | 三、A（stride 填 1088） | ✅ 复现：画面底部出现绿条、颜色偏 |
| 2026-10-18 | 三、B（编码完不 QBUF，`kFrames` 改成 10） | ❌ **程序卡死**（编完第 0 帧就不动了，8 秒后被 timeout 杀掉，退出码 124）→ 代码要改，见第五节 |
| 2026-10-18（改完第五节 1 之后） | 一、①～③；三、B 复测 | ✅ 一、①～③ 照常通过；**三、B 不再卡死**：10 帧副本编完 10 帧，退出码 0，ffprobe 数出 10 帧 |
| 2026-10-18（kFrames 改成 500，改完第五节 1～3） | 一、①～⑥；三、C、E；四、4.1 | ✅ 0 warning；500 帧 8,250,554 字节，**连续跑两次都成功**；ffprobe 500 帧 = 9 个 I 帧 + 491 个 P 帧（GOP 60），解码无错误；C、E 结果见第三节 |
| | 一、⑦；二；三、D | ⬜ 还没测 |

每次测完在这里加一行。

---

## 一、正常流程

### ① 运行，看日志

```bash
./run_on_board_with_mac.sh
```

应该依次看到（2026-10-18 实测）：
```
I ... format: 1920x1080 NV12, planes 1, bytesperline 1920, sizeimage 3110400
I ... buffer 0: length 3133440, dma_buf fd 12          ← ⭐ M3 新增：每块都有一个 dma_buf fd（数字不一定是 12，但 4 块各不相同）
I ... buffer 1: length 3133440, dma_buf fd 13
I ... buffer 2: length 3133440, dma_buf fd 14
I ... buffer 3: length 3133440, dma_buf fd 15
I ... encoder ready: 1920x1080 stride 1920x1080        ← ⭐ 必须是 1080，不是 1088
I ... header: 40 bytes                                 ← SPS + PPS，和 Day 1 一样
I ... frame   0: buffer 2, in 3110400 bytes, out 142280 bytes (I)    ← 第一帧一定是 I 帧，十几万字节
I ... Done: 1 frames, 142320 bytes -> ...              ← 142320 = 40 + 142280
```

| 看哪里 | 不对说明什么 |
|---|---|
| 没有 `dma_buf fd` | EXPBUF 那段没执行 |
| `stride 1920x1088` | `verStride` 没用 G_FMT 算出来的值（见三、A） |
| `out` 是几十字节或者 0 | 编码器没拿到图像，检查 `encode()` 传的是不是 `g_bufs[index].mppBuf` |
| 没有 `(I)` | 第一帧不是 I 帧，播放器没法从头解码 |

### ② 看退出码

```bash
adb shell '/tmp/CLion/run/day3_cam2h264 > /dev/null; echo "exit code: $?"' < /dev/null
```
应该是 `exit code: 0`。出错时 `run()` 返回 -1，`main` 把它当退出码返回；退出码只保留低 8 位，**-1 变成 255**（实测）。

### ③ 看输出文件

```bash
adb shell 'ls -l /userdata/av/cam_m3.h264' < /dev/null
```
大小 ≈ 日志里 `Done` 那一行的字节数（每次拍的画面不同，大小会变，十几万字节都正常）。

> 注意：M3 输出的是 **H.264 码流**，文件名要用 `.h264` 结尾。2026-10-18 的代码里写成了 `cam_m3.nv12`（见第五节）。
> 实测 ffmpeg / ffplay 会读文件内容判断格式，`.nv12` 结尾也能认出是 H.264；但**名字会误导人**：按 M2 的习惯用 `-f rawvideo -pixel_format nv12` 去播，就会解成一片乱码。

### ④ 看文件开头：SPS / PPS

```bash
KEEP=1 ./run_move_file_from_board_via_usb.sh /userdata/av/cam_m3.h264
xxd result_from_board/cam_m3.h264 | head -3
```

实测（2026-10-18）：
```
00000000: 0000 0001 6764 1028 ac1b 1aa0 7802 25e5  ....gd.(....x.%.
00000010: 8400 0003 0004 0000 0300 f23c 2211 a800  ...........<"...
00000020: 0000 0168 ee31 b01b 0000 0001 0605 383d  ...h.1........8=
```

| 字节 | 意思 |
|---|---|
| `00 00 00 01` | 起始码：一个 NAL 单元开始了 |
| `67` | NAL 类型 7 = **SPS**（序列参数集） |
| `64` | profile_idc = 100 = **High** profile |
| `28` | level_idc = 40 = **Level 4.0** |
| `00 00 00 01 68` | NAL 类型 8 = **PPS**（图像参数集） |
| `00 00 00 01 06` | NAL 类型 6 = SEI（编码器附带的信息） |

和 Day 1 编文件时的开头一样（Day 1 笔记：profile 100、level 40）。

### ⑤ ffprobe 看码流信息和帧类型

```bash
ffprobe -v error -show_entries stream=codec_name,profile,width,height,pix_fmt -of default=nw=1 result_from_board/cam_m3.h264
ffprobe -v error -show_frames -show_entries frame=pict_type -of csv result_from_board/cam_m3.h264
```
实测：
```
codec_name=h264
profile=High
width=1920
height=1080
pix_fmt=yuv420p
frame,I
```
→ H.264、High profile、1920×1080，**1 帧，是 I 帧**。

### ⑥-0 用 ffplay 播放 H.264（拉回来就看）

```bash
cd /Users/dev/Documents/AV/rk_work/day3_cam2h264

# 拉回来（KEEP=1：板子上的保留）+ 播放，两行可以一起粘贴（脚本里的 adb 已经不会吃掉第二行了）
KEEP=1 ./run_move_file_from_board_via_usb.sh /userdata/av/cam_m3.h264
ffplay -f h264 -framerate 30 result_from_board/cam_m3.h264
```

| 参数 | 意思 |
|---|---|
| `-f h264` | 告诉它这是 **H.264 裸流**（不加也行：实测 ffmpeg 会读内容自己认出来；加上更稳，文件名写错扩展名时也不会出问题） |
| `-framerate 30` | 裸流里没有时间信息，告诉它按 **30fps** 放（摄像头就是 30fps）；不加的话 ffplay 默认按 25fps 放，会慢一点 |
| `-autoexit` | 放完自动关窗口（可选）。M3 只有 1 帧，加了它窗口会一闪就关；**不加就停在最后一帧**，看清楚了按 `q` 关 |
| `-loop 0` | 一直循环播放（可选，M4 的 10 秒视频想反复看时用） |

播放时的按键：`q` / `Esc` 退出，空格暂停，`s` 一帧一帧往前走，`f` 全屏，`←` `→` 后退 / 前进 10 秒。

**和 M2 的区别**：M2 存的是原始图像 NV12，要告诉 ffplay 宽高和像素格式；M3 存的是 H.264 码流，宽高都在 SPS 里，**不用写**：

| | 文件里是什么 | 播放命令 |
|---|---|---|
| M2 | 原始图像（NV12） | `ffplay -f rawvideo -pixel_format nv12 -video_size 1920x1080 cam_m2.nv12` |
| M3 / M4 | H.264 码流 | `ffplay -f h264 -framerate 30 cam_m3.h264` |

> ⚠️ 反过来用错了就是乱码：用 M2 那条命令去播 H.264，等于把压缩数据当像素去显示。

**2026-10-18 实测**（用 ffmpeg 以同样参数解码代替 ffplay 弹窗）：`-f h264 -framerate 30` 读出 10 帧、帧率 30/1；`.nv12` 结尾不加 `-f` 也能认出是 H.264。

### ⑥ 解码成图片看画面（最重要）

```bash
ffmpeg -y -i result_from_board/cam_m3.h264 -frames:v 1 result_from_board/cam_m3.png && open result_from_board/cam_m3.png
```

| 看到的 | 说明 |
|---|---|
| 颜色、亮度正常，和 M2 抓的一样 | ✅ 零拷贝编码成功，stride 对了（2026-10-18 实测就是这样） |
| **底部一条绿色横条，颜色偏** | stride 错了（见三、A） |
| 上下两半不是同一时刻 | 编码器还在读就把缓冲区还给了摄像头 |
| 解码报错、打不开 | 码流不完整，检查有没有先写 header、有没有送 EOS |

> 2026-10-18 实测画面是**倒着的**（摄像头装反了），不是代码问题。M5 用 `-r 2`（编码器旋转 180°），或者摄像头的 `vertical_flip` / `horizontal_flip`（见 doc/FindCamera输出的每个字段是什么.md 第 1 节）转正。

### ⑦ 动起来再测

一边在镜头前晃手，一边连续跑 3～5 次 ① + ⑥。每一张都应该完整、没有撕裂。

---

## 二、零拷贝本身怎么确认

"零拷贝"是看不见的，用这几点确认：

| 检查 | 怎么做 | 应该是 |
|---|---|---|
| 代码里没有拷贝帧数据 | 在 `src/main.cpp` 里搜 `memcpy`、`fwrite(g_bufs` | M3 里一个都没有：送给编码器的是 `g_bufs[index].mppBuf`，它和摄像头写的是同一块内存 |
| 每块缓冲区都有自己的 dma_buf fd | 看 ① 的日志 | 4 个 fd 各不相同（实测 12、13、14、15） |
| 导入的类型 | 看 `bufInfo.type` | `MPP_BUFFER_TYPE_EXT_DMA`（外部来的 dma_buf），不是 `MPP_BUFFER_TYPE_DRM` |
| CPU 占用（M6 再做） | M6 写一个 memcpy 版本，`adb shell top -d 1` 对比 | 零拷贝版本明显更低 |

> 原理见 guide/04 第 6 节（MMAP 和 DMABUF）：`mmap` 拿到的地址只有本程序的 CPU 能用，`EXPBUF` 拿到的 fd 是这块内存的"钥匙"，交给 MPP 以后编码器直接读同一块物理内存。

---

## 三、故意制造错误

**在副本上改，不要改 `src/main.cpp`**（2026-10-18 的做法：把 `src/` 复制到别的目录，用 sed 改一处，单独编译、推到板子 `/tmp` 下跑，输出也写到 `/tmp`，不覆盖正式的结果文件）。

| | 怎么制造 | 应该看到 | 实测 |
|---|---|---|---|
| **A** | `cfg.verStride = 1088;`（Day 1 编码器默认的值） | 日志 `stride 1920x1088`；解码画面**底部绿条、颜色偏** | ✅ 2026-10-18 复现 |
| **B** | `kFrames` 改成 10（编码完**不** QBUF 的代码） | 编完前 4 帧后，第 5 次 DQBUF 永远等不到，**程序卡死** | ❌ 2026-10-18：卡死，`timeout 8` 杀掉，退出码 124 |
| **C** | `bufInfo.type = MPP_BUFFER_TYPE_DRM;` | ~~import 失败或者编码失败~~ **实测照样能编码**：dma_buf fd 是通用的，用 DRM 类型的分配器去 import 也认。但语义上摄像头的内存是"外部来的"，还是应该写 `EXT_DMA` | ✅ 2026-10-18：编码成功，1 帧 I 帧 |
| **D** | 先在板子屏幕上打开相机 App，再运行 | `VIDIOC_S_FMT` 或 `REQBUFS`：`Device or resource busy` | ⬜ |
| **E** | `kDevice` 改成 `"/dev/video31"` | `VIDIOC_STREAMON: Operation not permitted`，编码器根本不会初始化 | ✅ 2026-10-18：`[FAIL] VIDIOC_STREAMON: Operation not permitted (line 227)`，退出码 255 |

B 的命令（在副本上跑，8 秒没结束就杀掉）：
```bash
adb shell 'NO_COLOR=1 timeout 8 /tmp/m3_10frames > /tmp/m3_10.log 2>&1; echo "exit code: $?"'    # 124 = 被杀掉 = 卡住了
```

---

## 四、资源泄漏

### 4.1 连续运行两次

```bash
./run_on_board_with_mac.sh && ./run_on_board_with_mac.sh
```
第二次也必须成功。

### 4.2 检查 cleanup 有没有释放 M3 新增的资源

M3 比 M2 多了两样要释放的东西，`clear_up()` 里要有：

| 资源 | 谁申请的 | 释放 | 顺序 |
|---|---|---|---|
| `g_bufs[i].mppBuf` | `mpp_buffer_import` | `mpp_buffer_put` | STREAMOFF 之后 |
| `g_bufs[i].dmaFd` | `VIDIOC_EXPBUF` | `close` | munmap 之后 |

完整顺序（对照 camera_source.c 第 329～364 行）：**STREAMOFF → mpp_buffer_put → munmap → close(dma_buf fd) → close(video fd)**。

> 程序退出时内核会自动关掉所有 fd，所以"跑一次"看不出泄漏；但以后整理成类、反复打开关闭摄像头时，不释放就会越积越多。

---

## 五、2026-10-18 review 发现的问题

| # | 问题 | 后果 | 怎么改 |
|:---:|---|---|---|
| ✅ 1（已改） | 编码循环里 `encode()` 之后**没有 `queue_buffer(index)`** | M3 只编 1 帧看不出来；M4 改成多帧时**第 5 帧卡死**（三、B 实测） | `encode()` 返回之后、写码流之前或之后加 `if (!queue_buffer(index)) return -1;`（一定在 encode **之后**） |
| ✅ 2（已改） | `clear_up()` 没有 `mpp_buffer_put(mppBuf)` 和 `close(dmaFd)` | 资源泄漏（见四、4.2） | munmap 前 put、munmap 后 close dmaFd。复查：close 加了，但判断写成了 `if (g_buf.dmaFd)`（-1 也为真，会 `close(-1)`；fd 0 反而不关）→ 要 `>= 0`，关完置 -1；`mpp_buffer_put` 还没加 |
| ✅ 3（已改） | `kOutputFilePath` 是 `cam_m3.nv12` | 内容是 H.264，扩展名误导人（ffmpeg 看内容能认出来，实测；但按 NV12 的方式 `-f rawvideo` 去播就是乱码） | 改成 `cam_m3.h264` |
| ✅ 4（已删） | `src/encoder/mpp_encoder.cpp` 第 192～193 行加了 `printf("    packetSize : ...")` | 每编一帧打印一次，M4 有 300 行 | 调试完删掉，或者改成 `LOGD` |
| ✅ 5（已改） | 好几处 `LOGx(... "\n")`、`[FAIL]` 前缀、`V4L2_CHECK` 里的 `printf` 没换行 | 日志多空行、报错不显示红色 | 同 M2 的建议 |

改完 1～3 后，重跑一、①～⑥ 和三、B（应该不再卡死，10 帧都能编完）。

**2026-10-18 最终复查**：上面 1～5 全部改完；另外 `V4L2_CHECK` 里的 `LOGE` 一度漏了 kTag（7 个 `-Wformat-extra-args` 警告，报错打印成 `E     [FAIL] %s: %s (line %d): VIDIOC_STREAMON`，原因和行号都丢了），已修。`fwrite` 加了返回值检查，删了没用的 `<linux/media.h>`。结果：**0 warning**；500 帧 8,311,053 字节，退出码 0；打开 video31 时报 `E day3_main: VIDIOC_STREAMON: Operation not permitted (line 246)`。
