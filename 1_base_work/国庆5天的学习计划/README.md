# 国庆 5 天 · RK3588 编解码学习计划

> **目标**：5 天内用 MPP + V4L2 + RGA，从"编一个文件"做到"摄像头实时采集、编码、转码"
> **开发方式**：Mac 上用 CLion 写代码 → 交叉编译 → 一键上传到板子运行或调试（参考 `1_base_work/day_01/rk_test_env`）
> **参考源码**：`ATK-DLRK3588开发板网盘A盘_基础资料/11、AV_demo_源码/`（以下简称 `demo/`）
> **参考笔记**：`Tone_rk358/doc/MPP开发指南_学习笔记.md`（以下简称"MPP 笔记"）

---

## 总览

| 天 | 主题 | 工程名 | 输入 → 输出 | 用到的模块 |
|:---:|---|---|---|---|
| **Day 1** | MPP 硬件编码 | `day1_yuv2h264` | NV12 文件 → `.h264` / `.h265` 文件 | MPP 编码 |
| **Day 2** | MPP 硬件解码 | `day2_h2642yuv` | `.h264` 文件 → NV12 文件 | MPP 解码 |
| **Day 3** | 摄像头实时编码 | `day3_cam2h264` | IMX415 摄像头 → `.h264` 文件（零拷贝） | V4L2 + DMA-BUF + MPP 编码 |
| **Day 4** | RGA 图像处理 | `day4_rga` | 缩放、格式转换、旋转；摄像头双码流 | RGA + V4L2 + MPP |
| **Day 5** | 转码器 + RTSP 播放 | `day5_transcoder` | 1080p H.264 → 720p H.265；用 VLC 拉流观看 | MPP 解码 + RGA + MPP 编码 + 多线程 |

每一天都是"**上午读 demo 源码 + 文档 → 下午写自己的 C++ 程序 → 晚上做实验、写笔记**"。

---

## 开始之前：公共准备（Day 1 早上，约 30 分钟）

### 1. 新工程模板
每天的工程都从 `1_base_work/day_01/rk_test_env` 复制一份改名：
- 复制 `CMakeLists.txt`、`main.cpp`、`run_on_board_with_mac.sh`
- CLion 打开新工程 → 新建 `RK3588-Debug` CMake 配置（工具链文件）→ 新建 Remote GDB Server 配置和 Shell Script 运行配置（步骤见 `1_base_work/day_01/rk_test_env/doc/1_在mac上搭建调试环境.md`）
- 改脚本里的 `BIN_NAME` 和路径
- **板子 IP 不用改**：`run_on_board_with_mac.sh` 会调用 `common_shell/get_board_ip.sh`，通过 adb 自动获取（OTG 线要插着）
- ⚠️ 但 CLion 的 Remote GDB Server 配置里 IP 是写死的：板子 IP 变了，要手动改 **Credentials 的 Host** 和 **'target remote' args**（先运行 `run_get_board_ip.sh` 看当前 IP）

### 2. 板子上的工作目录
```bash
ssh root@<板子IP> 'mkdir -p /userdata/av && df -h /userdata'
```
- 测试文件、输出文件都放在 **`/userdata/av/`**（eMMC 上，重启不会丢）
- **不要放在 `/tmp`**：`/tmp` 在内存里，重启就清空了，而且大文件会占用内存

### 3. 测试素材
| 素材 | 来源 | 说明 |
|---|---|---|
| **`aaa.264`** | A 盘 `01、程序源码/03、live555测试文件/aaa.264` | H.264 Constrained Baseline，**1920×1080，29.97fps**，55.7MB |
| `in_1080p.nv12` | 在 Mac 上用 ffmpeg 从 `aaa.264` 解码生成（见 Day 1） | 原始 NV12 画面，用来测试编码 |
| 摄像头画面 | 板子上的 `/dev/video31`（IMX415 → rkisp1 mainpath） | NV12，1920×1080@30fps，**画面旋转了 90°** |

### 4. Mac 上查看结果用的命令
```bash
FF=/usr/local/ffmpeg/4.4/bin
$FF/ffprobe -v error -show_streams out.h264                       # 码流信息
$FF/ffprobe -v error -show_frames out.h264 | grep pict_type | head -70 | sort | uniq -c   # I/P 帧统计
$FF/ffplay -f rawvideo -pixel_format nv12 -video_size 1920x1080 out.nv12                  # 播放 YUV
open -a VLC out.h264                                               # 播放码流
```

---

## Day 1 · MPP 硬件编码：`day1_yuv2h264`

### 🎯 目标
读一个 NV12 文件，用 MPP 硬件编码成 H.264（再改一个参数就能出 H.265），在 Mac 上用 VLC 能正常播放。

### 📖 上午：读源码和文档（约 3 小时）
| 读什么 | 重点 |
|---|---|
| MPP 笔记 **第 3、4、6 节** | 数据结构、MppApi、**编码主循环伪代码** |
| `demo/mpp/test/mpi_enc_test.c`（1095 行） | 按这个顺序读：`main`（第 1077 行）→ `enc_test_multi`（第 1020 行）→ `enc_test`（第 869 行）→ `test_ctx_init`（第 146 行，stride、frame_size 怎么算）→ `test_mpp_enc_cfg_setup`（第 290 行，所有 `mpp_enc_cfg_set_s32` 调用）→ `test_mpp_run`（第 547 行，循环里的 `encode_put_frame` / `encode_get_packet`） |
| `demo/mpp/utils/utils.c` | `read_image()`（第 480 行）：**怎么按 stride 逐行读 YUV** |
| `demo/mpp/utils/mpi_enc_utils.c` | 命令行参数的解析（`-w`、`-h`、`-t`、`-rc` 等），浏览一下就行 |
| 可以跳过 | OSD、ROI、SEI、user data、多实例相关的代码 |

带着问题读：
- [ ] `hor_stride`、`ver_stride` 是怎么算出来的？为什么 1080 要变成 1088？
- [ ] `frame_size` 为什么按 64 对齐？
- [ ] SPS/PPS 是怎么拿到的？`MPP_ENC_HEADER_MODE_EACH_IDR` 是做什么的？
- [ ] `encode_get_packet` 会不会取不到数据？`MPP_SET_OUTPUT_TIMEOUT` 起什么作用？

### 💻 下午：写代码（约 4 小时）
**准备输入文件**（在 Mac 上执行）：
```bash
FF=/usr/local/ffmpeg/4.4/bin
$FF/ffmpeg -i aaa.264 -frames:v 60 -pix_fmt nv12 -f rawvideo in_1080p_60f.nv12   # 60 帧（2 秒），约 180MB
scp in_1080p_60f.nv12 root@<板子IP>:/userdata/av/
```
> 先用 **60 帧**把功能调通：板子走 Wi-Fi，大文件传起来很慢。等编码结果正确了，晚上测性能时再生成 300 帧（约 890MB）的文件，传之前先用 `df -h /userdata` 确认空间够用。
>
> 一帧 1920×1080 NV12 = 1920 × 1080 × 1.5 = 3,110,400 字节，用来检查文件大小对不对。
```bash
# 晚上测性能时再执行：
$FF/ffmpeg -i aaa.264 -frames:v 300 -pix_fmt nv12 -f rawvideo in_1080p.nv12      # 300 帧，约 890MB
```
> 注意：ffmpeg 输出的 NV12 是**紧凑排列**的（高度 1080，没有填充行），而 MPP 要求 ver_stride = 1088，所以读文件时要**逐行拷贝到对齐后的缓冲区里**。这正是今天要掌握的 stride 处理。

**程序结构**（C++，用 RAII 管理资源）：
```
class MppEncoder {
public:
    bool init(int w, int h, MppCodingType type, int bps, int fps, int gop);
    bool encode(MppBuffer frame_buf, bool eos, std::vector<uint8_t>& out);   // 送一帧，取码流
    bool get_header(std::vector<uint8_t>& sps_pps);
    ~MppEncoder();   // mpp_destroy、释放 cfg
};

main:
    解析参数：-i 输入 -o 输出 -w 1920 -h 1080 -t h264|h265 -bps 4000000 -rc cbr|vbr|avbr
    申请 MppBufferGroup（DRM）+ 一块 frame_size 大小的 MppBuffer
    循环：从文件读一帧（按 stride 逐行拷贝）→ encode → 写入输出文件
    打印：总帧数、总用时、平均 fps、实际码率
```

### 🧪 晚上：实验（约 1.5 小时）
| 实验 | 方法 | 记录 |
|---|---|---|
| 码率控制模式 | 同一个输入，分别用 CBR / VBR / AVBR 编码 4Mbps | 文件大小、ffprobe 看到的实际码率 |
| GOP | gop = 30 / 60 / 300 | 用 ffprobe 统计 I 帧数量，对比文件大小 |
| H.264 和 H.265 | `-t h265` | 同样码率下画质和文件大小的差别 |
| 速度 | 板子上执行 `top`，同时编码 | CPU 占用（应该很低）、编码 fps |
| 和官方工具对比 | `mpi_enc_test -w 1920 -h 1080 -t 7 -i in_1080p.nv12 -o ref.h264 -n 300` | 和你的程序比较 fps |

### ✅ 验收
- [ ] `out.h264` 在 Mac 上用 VLC 能流畅播放，画面没有花屏、没有绿边
- [ ] ffprobe 能看到正确的分辨率、帧率和 GOP
- [ ] 换成 `-t h265` 也能正常播放
- [ ] 能讲清楚 stride、SPS/PPS、GOP、CBR/VBR 分别是什么

### ⚠️ 容易踩的坑
- 用 `malloc` 的内存直接送给编码器 → 失败。**必须用 MppBuffer**
- 整块 `fread` 进缓冲区，不按 stride 逐行拷贝 → 画面斜着错位，底部出现绿条
- 忘记写 SPS/PPS → VLC 打不开文件
- `mpp_packet_deinit` 忘了调用 → 内存一直涨

---

## Day 2 · MPP 硬件解码：`day2_h2642yuv`

### 🎯 目标
把 `aaa.264` 硬件解码成 NV12 文件，在 Mac 上用 ffplay 能正常播放，并测出解码速度。

### 📖 上午：读源码和文档（约 3 小时）
| 读什么 | 重点 |
|---|---|
| MPP 笔记 **第 5 节** | 解码主循环、**info change**、三种内存模式、两种输入方式（内部分帧和外部分帧） |
| `demo/mpp/test/mpi_dec_test.c`（695 行） | `dec_simple()`：`decode_put_packet` → `decode_get_frame` 的循环，**info change 的处理（第 137～165 行左右）**，`dec_buf_mgr_setup` 分配缓存，`base:split_parse` 的设置 |
| `demo/mpp/utils/mpi_dec_utils.c` | 读文件的方式；`dump_mpp_frame_to_file()`：**怎么按 stride 把图像写出去** |
| 了解一下 | `demo/mpp/test/mpi_dec_mt_test.c`：多线程写法，一个线程送数据、一个线程取结果 |

带着问题读：
- [ ] 为什么第一次 `decode_get_frame` 拿到的不是图像？
- [ ] `buf_size` 是从哪里来的？要分配多少块缓存？
- [ ] `errinfo` 和 `discard` 的帧应该怎么处理？
- [ ] 内部分帧（`split_parse=1`）和外部分帧有什么区别？`aaa.264` 是裸码流文件，应该用哪种？

### 💻 下午：写代码（约 4 小时）
```
class MppDecoder {
public:
    bool init(MppCodingType type);                                   // 打开 split_parse
    bool feed(const uint8_t* data, size_t len, bool eos);             // decode_put_packet（队列满时等待重试）
    bool get_frame(MppFrame& frame);                                  // decode_get_frame，内部处理 info change
    ~MppDecoder();
};

main:
    按固定大小（比如 64KB）读 aaa.264 → feed
    get_frame 拿到图像后：
        跳过 errinfo / discard 的帧
        按 width × height（不是 stride！）逐行写出 Y 和 UV，这样 ffplay 能直接播放
        mpp_frame_deinit
    打印：解码帧数、用时、fps、第一次 info change 时的宽、高、stride、buf_size
```

### 🧪 晚上：实验
| 实验 | 记录 |
|---|---|
| 解码 300 帧的 fps（不写文件，只解码） | 和 `mpi_dec_test -t 7 -i aaa.264 -n 300` 对比 |
| 写文件和不写文件的 fps 差别 | 体会 I/O 成为瓶颈的情况 |
| 解码 Day 1 编出来的 `.h265` | `-t` 换成 HEVC |
| 打印每一帧的 `pts`、`width`、`hor_stride`、`ver_stride` | 理解 1920×1080 和 1920×1088 的关系 |
| **编解码串起来**：Day 1 编出来的文件 → Day 2 解码 → 和原始 NV12 逐字节比较 | 理解有损压缩（数据不会完全一样） |

### ✅ 验收
- [ ] `ffplay -f rawvideo -pixel_format nv12 -video_size 1920x1080 out.nv12` 画面正常
- [ ] 正确处理了 info change（日志里打印出 `1920x1080 stride 1920x1088`）
- [ ] 解码 fps 明显高于 30（RK3588 解码 1080p 应该能达到几百 fps）
- [ ] 文件解码完能正常退出（正确处理了 EOS）

### ⚠️ 容易踩的坑
- 不处理 info change → 一直拿不到图像，程序卡住
- `mpp_frame_deinit` 忘了调用 → 缓存池被占满，解码器卡死
- 按 `hor_stride × ver_stride` 写文件 → ffplay 播放时画面底部有 8 行垃圾数据
- `decode_put_packet` 返回失败就直接丢掉这包数据 → 漏帧。**正确做法是等一下再送同一包**

---

## Day 3 · 摄像头实时编码：`day3_cam2h264`

### 🎯 目标
IMX415 摄像头实时采集，**零拷贝**交给 MPP 编码，录 10 秒 H.264，在 Mac 上播放。

### 📖 上午：读源码和文档（约 3 小时）
| 读什么 | 重点 |
|---|---|
| ⭐ `demo/mpp/utils/camera_source.c`（441 行） | V4L2 采集的完整流程：`open` → `VIDIOC_S_FMT` → `VIDIOC_REQBUFS`（MMAP 方式）→ `VIDIOC_QUERYBUF` + `mmap` → **`VIDIOC_EXPBUF` 导出 DMA-BUF fd（第 266～284 行）** → `VIDIOC_QBUF` / `VIDIOC_STREAMON` → `VIDIOC_DQBUF` 取一帧 / `QBUF` 还回去 |
| ⭐ `camera_frame_to_buf()` | **把 DMA-BUF fd 包装成 MppBuffer**，这就是零拷贝的关键 |
| `demo/mpp/test/mpi_enc_test.c` 第 595～660 行 | 摄像头作为输入时的编码循环：`camera_source_get_frame` → `camera_frame_to_buf` → `mpp_frame_set_buffer` → 编码 → `camera_source_put_frame` |
| A 盘《Camera应用开发手册》第 1、5 章 | `v4l2-ctl` 的用法，mainpath/selfpath |
| SDK 文档 `docs/cn/Common/ISP/ISP30/Rockchip_Driver_Guide_VI_CN_v1.1.3.pdf` | 浏览一下采集链路那部分：sensor → CSI → ISP → video 节点 |

带着问题读：
- [ ] MMAP 和 DMA-BUF 有什么区别？为什么 `EXPBUF` 能做到零拷贝？
- [ ] 为什么要申请多个缓冲区（比如 4 个）轮流使用？
- [ ] 一帧编码完之前，能不能把缓冲区还给摄像头（QBUF）？
- [ ] 这个过程和 Android Camera2 + MediaCodec 的 Surface 输入有什么相似之处？

### 💻 下午：写代码（约 4 小时）
```
class V4l2Camera {           // 可以直接参考 camera_source.c 用 C++ 重写，或者直接链接它
public:
    bool open(const char* dev, int w, int h, uint32_t fourcc, int buf_cnt);   // /dev/video31, 1920x1080, NV12, 4
    int  dequeue();          // 返回 buffer 序号
    int  dmabuf_fd(int idx);
    void enqueue(int idx);
};

main:
    camera.open → encoder.init（Day 1 的 MppEncoder 直接复用）
    用 mpp_buffer_import 把每个 dmabuf fd 导入成 MppBuffer（只在启动时做一次）
    循环 300 帧（10 秒）：
        idx = dequeue()
        encode(mppbuf[idx])       ← 零拷贝
        enqueue(idx)
        写入 .h264
    打印：实际帧率、每帧编码耗时
```

### 🧪 晚上：实验
| 实验 | 记录 |
|---|---|
| 零拷贝和拷贝的对比 | 另写一个版本：把 mmap 出来的数据 memcpy 到 MppBuffer 再编码，对比 CPU 占用（`top`） |
| 画面旋转 | 编码参数里设置 `prep:rotation = 1`（逆时针 90°）或 3，把画面转正 |
| 分辨率 | 1280×720 / 3840×2160（IMX415 原生是 4K），观察帧率 |
| 端到端延迟（选做） | 对着秒表拍，录下来以后比较时间差 |

### ✅ 验收
- [ ] 录下来的 10 秒视频在 Mac 上能正常播放，画面清晰，**方向是正的**
- [ ] 帧率稳定在 30fps 左右
- [ ] 零拷贝版本的 CPU 占用明显比拷贝版本低
- [ ] 能讲清楚 V4L2 的 buffer 轮转流程和 DMA-BUF 零拷贝的原理

### ⚠️ 容易踩的坑
- 用错节点：IMX415 一般是 `/dev/video31`（rkisp1 的 mainpath），不是手册里写的 `video62`；插在另一个摄像头接口上时是 `/dev/video22`（rkisp0）。设备节点编号是开机时按顺序分配的，插拔摄像头、更新系统后可能会变。⚠️ **`v4l2-ctl --list-devices` 只能看到 ISP 的节点，没接摄像头这些节点也在**，判断摄像头在不在要看 `dmesg | grep imx415` 没有 `Unexpected sensor id`、`media-ctl -p` 里有 imx415 实体（2026-10-15 实测，见 `1_base_work/day3_cam2h264/guide/01_环境准备.md`）
- `rkaiq_3A_server` 没有运行 → 画面很暗或颜色不对（出厂系统开机时会自动启动它，但**找不到摄像头时它会自己退出**，日志里是 `Bad media topology`）
- 前几帧曝光还没稳定 → 跳过前 30 帧
- 摄像头的 `bytesperline` 不一定等于 width，**要按 V4L2 返回的实际值设置 stride**。还有 ver_stride：1080p NV12 的 UV 从第 1080 行开始（`sizeimage` = 3,110,400，2026-10-15 用 `--try-fmt-video` 实测），Day 1 的编码器默认按 1088 算，直接复用会颜色错位
- 编码器还在读这块缓冲区时就把它还给了摄像头 → 画面撕裂。`encode_put_frame` 返回以后再 QBUF

---

## Day 4 · RGA 图像处理：`day4_rga`

### 🎯 目标
掌握 RGA 的缩放、格式转换、旋转，并接到摄像头流水线里，做出**主码流 1080p + 子码流 720p** 的双码流。

### 📖 上午：读源码和文档（约 3 小时）
| 读什么 | 重点 |
|---|---|
| SDK 文档 `docs/cn/Common/RGA/Rockchip_Developer_Guide_RGA_CN.pdf`（64 页） | im2d API 的使用方法、支持的格式、对齐要求 |
| ⭐ `demo/linux-rga/samples/resize_demo/src/rga_resize_demo.cpp` | `importbuffer_virtualaddr` / `importbuffer_fd` → `wrapbuffer_handle` → `imcheck` → **`imresize`** → `releasebuffer_handle` |
| ⭐ `demo/linux-rga/samples/cvtcolor_demo/src/rga_cvtcolor_demo.cpp` | **`imcvtcolor`**：NV12 ↔ RGB |
| `demo/linux-rga/samples/transform_demo/` | `imrotate`、`imflip` |
| `demo/linux-rga/samples/allocator_demo/` | 用 DMA-BUF 分配内存（零拷贝要用） |
| `demo/linux-rga/docs/` | RGA 自带的文档（可选） |
| 可选 | `demo/linux-rga/samples/alpha_demo/`（OSD 叠加）、`mosaic_demo/`（马赛克） |

### 💻 下午：写代码（约 4 小时）
**第一部分：单独测试（读文件 → 处理 → 写文件）**
| 测试 | 输入 | 输出 | 在 Mac 上怎么验证 |
|---|---|---|---|
| 缩放 | 1920×1080 NV12 | 1280×720 NV12 | ffplay |
| 格式转换 | 1920×1080 NV12 | 1920×1080 RGB888 | `ffmpeg -f rawvideo -pixel_format rgb24 -video_size 1920x1080 -i out.rgb out.png` |
| 旋转 | 1920×1080 NV12（摄像头画面） | 1080×1920 NV12（转正后） | ffplay |

**第二部分：双码流**
```
摄像头 /dev/video31（1920x1080 NV12，dmabuf）
   ├──────────────────────────► MppEncoder #1（1080p H.264，主码流）→ main.h264
   └─► RGA imresize（dmabuf → dmabuf）→ 1280x720 ─► MppEncoder #2（720p H.264，子码流）→ sub.h264
```
- 用 `importbuffer_fd` 导入摄像头的 dmabuf 和 RGA 输出的 dmabuf，**整个过程不经过 CPU 拷贝**
- 两个编码器各自独立运行，可以同时工作（RK3588 有两个编码核）

### 🧪 晚上：实验
| 实验 | 记录 |
|---|---|
| RGA 和 CPU 缩放的速度对比 | 用 CPU 写一个最简单的缩放（或者用 OpenCV）对比耗时 |
| `importbuffer_virtualaddr` 和 `importbuffer_fd` 的对比 | 耗时差别，理解为什么要用 fd |
| 双码流时的 CPU 占用 | `top` |

### ✅ 验收
- [ ] 缩放、格式转换、旋转三个测试的输出在 Mac 上查看都正常
- [ ] `main.h264`（1080p）和 `sub.h264`（720p）都能播放，内容一致
- [ ] 能讲清楚 RGA 的 handle、im2d API 的使用流程，以及 RGA 有哪些对齐要求

### ⚠️ 容易踩的坑
- RGA 对宽高、stride 有**对齐要求**（一般是 16 或者 4 的倍数，具体看文档），不满足就会报错，或者输出错位
- 用完没有调用 `releasebuffer_handle` → handle 泄漏
- NV12 的 UV 分量也要正确处理，宽高要是偶数

---

## Day 5 · 转码器 + RTSP 播放：`day5_transcoder`

### 🎯 目标
做出一个完整的**转码器**：1080p H.264 → 解码 → RGA 缩放 → 编码 720p H.265。用多线程流水线实现，最后用 RTSP 在 Mac 上用 VLC 观看。

### 📖 上午：设计 + 读源码（约 2 小时）
| 读什么 | 重点 |
|---|---|
| `demo/mpp/test/mpi_dec_mt_test.c` | 多线程解码：送数据线程和取结果线程是怎么配合的 |
| `demo/mpp/test/mpi_dec_multi_test.c` | 多个解码实例同时运行（为以后做多路 NVR 打基础） |
| 回顾 Day 1、2、4 的代码 | 把 `MppEncoder`、`MppDecoder`、RGA 处理整理成可以复用的类 |

**流水线设计**：
```
线程 1：读文件 → MppDecoder ──► 帧队列（最多 N 帧）──► 线程 2：RGA 缩放 → MppEncoder(H.265) → 写文件
                         decoded MppFrame            （生产者-消费者，std::mutex + std::condition_variable）
```
要想清楚的问题：
- [ ] 解码出来的 MppFrame 什么时候 deinit？（编码完成以后才能还给解码器）
- [ ] 队列满了怎么办？（解码线程等待，这叫背压）
- [ ] 怎么优雅地结束？（EOS 从解码器一路传到编码器）

### 💻 下午：写代码（约 4 小时）
```
transcoder -i aaa.264 -o out_720p.h265 -W 1280 -H 720 -t h265 -bps 2000000
```
1. 先写单线程版本：解码一帧 → 缩放 → 编码一帧，确认结果正确
2. 再改成双线程流水线，对比 fps
3. 打印：总帧数、总用时、fps、输入和输出的文件大小

### 🌐 晚上：用 RTSP 观看（约 1.5 小时）
用 A 盘里现成的 live555 服务器，把转码结果通过 RTSP 发出去：
```bash
# 1. 用 Day 1 的编码器（或者转码器）输出一个 H.264 文件，放到 live555 所在的目录
scp "A盘/01、程序源码/03、live555测试文件/live555MediaServer" root@<板子IP>:/userdata/av/
ssh root@<板子IP> 'cd /userdata/av && chmod +x live555MediaServer && ./live555MediaServer'

# 2. Mac 上用 VLC 打开（菜单：文件 → 打开网络）
rtsp://<板子IP>:8554/out.h264
```
> live555MediaServer 会把它所在目录下的文件当作点播源。它默认用 554 端口，没有权限时会改用 8554，**以启动时打印的地址为准**。

再用 Wireshark 在 Mac 上抓包，看一下 RTSP 的 OPTIONS / DESCRIBE / SETUP / PLAY 信令，以及 RTP 包。这就是阶段 2 `cam_rtsp` 的预习。

### ✅ 验收
- [ ] `out_720p.h265` 能播放，分辨率是 1280×720，文件明显变小
- [ ] 双线程版本比单线程版本 fps 更高
- [ ] Mac 上的 VLC 能通过 RTSP 看到画面
- [ ] 在 Wireshark 里找到了 RTSP 的四个信令

---

## 每天的笔记（写在 `Tone_rk358/doc/` 或各天的工程里）
每天结束时花 20 分钟，写下：
1. **今天学会了什么**（用自己的话写，比如"stride 就是……"）
2. **踩了哪些坑，怎么解决的**
3. **测出来的数据**（fps、CPU 占用、文件大小）
4. **明天要带着的问题**

---

## 5 天后能掌握的东西

| 能力 | 对应学习路线 |
|---|---|
| MPP 编码和解码，熟练处理 stride、info change、码率控制 | 阶段 1 ✅ |
| V4L2 采集 + DMA-BUF 零拷贝 | 阶段 2 的一半 |
| RGA 缩放、转换、旋转，双码流 | 阶段 2 的进阶部分 |
| 转码流水线、多线程、背压 | 阶段 3 ✅ |
| RTSP 的基本概念 | 阶段 2 的预习 |

**下一步**（国庆以后）：阶段 2 `cam_rtsp`，把 Day 3 的摄像头编码接上 RTSP 服务器（live555 库或 gst-rtsp-server），做出一个真正的**网络摄像头**。

---

## 时间不够时的取舍
- **必须完成**：Day 1、Day 2、Day 3。这三天是基础
- **可以压缩**：Day 4 只做缩放和格式转换，双码流放到以后再做
- **可以推迟**：Day 5 晚上的 RTSP 部分
- 每天卡住超过 1 小时：先跑官方的 `mpi_enc_test` / `mpi_dec_test`，确认硬件和环境没有问题，再对照 demo 源码一行一行找差别
