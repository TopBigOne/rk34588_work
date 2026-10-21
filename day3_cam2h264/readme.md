# Day 3 · 摄像头实时编码：`day3_cam2h264`

> **一句话目标**：IMX415 摄像头实时采集 → **不拷贝数据**，把摄像头的缓冲区直接交给 MPP 硬件编码 → 录 10 秒 H.264 → 在 Mac 上能正常播放，方向是正的
> **总时间**：约 8 小时（读代码 3h + 写代码 4h + 实验 1h）
> **前提**：摄像头已经装好并通过 [guide/01_环境准备.md](guide/01_环境准备.md) 第 2 节的 3 条检查（2026-10-15 查过，板子上还没有检测到摄像头）

---

## 1. 这个程序在做什么

```
 RK3588 开发板
 ──────────────────────────────────────────────────────────────────────────────────────
 IMX415 ──MIPI──→ CSI ──→ ISP（rkisp，3A 服务调曝光、白平衡）──→ /dev/video22（ISP0 的 rkisp_mainpath，摄像头装在 MIPI CSI3）
                                                                      │
             启动时一次：REQBUFS 要 4 块缓冲区 → EXPBUF 拿到每块的 dma_buf fd   │
                        → mpp_buffer_import 把 fd 包装成 MppBuffer           │
                                                                      ▼
   ┌─────────── 循环 300 次 ───────────────────────────────────────────────────────┐
   │ ① VIDIOC_DQBUF：取出一块装好画面的缓冲区，得到序号 idx                           │
   │ ② encoder.encode(mppBuf[idx])：硬件编码器直接读这块内存（零拷贝）                 │
   │ ③ fwrite 码流 → /userdata/av/out.h264                                         │
   │ ④ VIDIOC_QBUF(idx)：编码完了，把缓冲区还给摄像头，让它接着往里写下一帧              │
   └──────────────────────────────────────────────────────────────────────────────┘
                                                                      │ adb pull
 Mac：ffplay / VLC 播放 out.h264  ←────────────────────────────────────┘
```

和前两天比：

| | Day 1 | **Day 3** |
|---|---|---|
| 输入 | NV12 文件，CPU 用 fread **逐行拷进** MppBuffer | 摄像头缓冲区，**一个字节都不拷**，fd 直接导入成 MppBuffer |
| 输入缓冲区谁申请 | MPP（`mpp_buffer_get`） | 摄像头驱动（`VIDIOC_REQBUFS`），MPP 只是借用 |
| stride 谁定 | 自己算（按 16 对齐：1080 → 1088） | **摄像头驱动定**（`bytesperline`，UV 从第几行开始），编码器必须跟着它 |
| 编码器 | `MppEncoder` | **直接复用 Day 1 的 `MppEncoder`**，只改 stride 的来源 |

---

## 2. `main.cpp` 要做的 6 件事

| # | 在干什么 | 比喻 | 关键调用 | 里程碑 |
|:---:|---|---|---|:---:|
| ① | **打开摄像头，说好要什么格式** | 跟照相馆说：1920×1080、NV12 | `open("/dev/video22")` → `VIDIOC_QUERYCAP` → `VIDIOC_S_FMT` → `VIDIOC_G_FMT`（读回 `bytesperline`） | M2 |
| ② | **要几块缓冲区，拿到 CPU 地址和 fd** | 租 4 个托盘，每个托盘有地址也有钥匙 | `VIDIOC_REQBUFS(4, MMAP)` → `VIDIOC_QUERYBUF` + `mmap` → `VIDIOC_EXPBUF` | M2 / M3 |
| ③ | **开始拍** | 托盘全交给照相馆，开机 | `VIDIOC_QBUF` × 4 → `VIDIOC_STREAMON` | M2 |
| ④ | **把摄像头的缓冲区交给编码器** | 把托盘钥匙复印一把给印刷厂 | `mpp_buffer_import(info.fd = expbuf.fd)` → Day 1 的 `MppEncoder::init`（stride 用 ① 读回来的） | M3 |
| ⑤ | **取一帧 → 编码 → 还回去** | 拿托盘 → 印 → 还托盘 | `VIDIOC_DQBUF` → `encoder.encode` → `fwrite` → `VIDIOC_QBUF` | M3 / M4 |
| ⑥ | **收拾干净** | 关机、还托盘、还钥匙 | `VIDIOC_STREAMOFF` → `mpp_buffer_put` → `munmap` → `close(dma_buf fd)` → `close(video fd)` | 每一步 |

几个看着很怪的东西，各自解决什么问题：

| 东西 | 为什么非要它 |
|---|---|
| 4 块缓冲区轮流用 | 只有 1 块的话，编码器在读的时候摄像头没地方写，就会丢帧；多块缓冲区让"采集"和"编码"同时进行 |
| `VIDIOC_EXPBUF` | MMAP 方式拿到的只是 CPU 地址，编码器（另一个硬件）用不了；导出成 dma_buf fd，别的硬件才能直接访问同一块内存，这就是零拷贝 |
| `mpp_buffer_import`（不是 `mpp_buffer_get`） | 内存是摄像头驱动的，MPP 只是借用：import 只包装 fd，不分配内存 |
| 按 `bytesperline` 设置 stride | 摄像头缓冲区的排布由驱动决定，编码器按自己算的 stride 去读会花屏（见 [guide/03](guide/03_写代码_M1到M6.md) 第 3 节，板子实测） |
| `encode()` 返回以后才 QBUF | QBUF 以后摄像头马上会往这块内存写下一帧；编码器还在读的时候就还回去，画面会撕裂 |
| 跳过前几十帧 | 刚开机时自动曝光还没稳定，前几帧很暗或者颜色不对 |

---

## 3. 进度

| 里程碑 | 内容 | 状态 |
|:---:|---|:---:|
| M0 | 装摄像头，通过 3 条检查；官方 `mpi_enc_test -i /dev/video22` 能录出画面 | ⬜ |
| M1 | 不写代码：`v4l2-ctl` 抓一帧 NV12，在 Mac 上看画面 | ⬜ |
| M2 | 自己写 V4L2：打开 → S_FMT → REQBUFS → mmap → STREAMON → DQBUF，存一帧 NV12 | ⬜ |
| M3 | EXPBUF → `mpp_buffer_import` → 编码 1 帧（stride 跟摄像头一致） | ⬜ |
| M4 | 循环 300 帧，跳过前 30 帧，写出 10 秒 `.h264` | ⬜ |
| M5 | 命令行参数 + 统计（实际帧率、每帧编码耗时）；`prep:rotation` 把画面转正 | ⬜ |
| M6 | 拷贝版本对比（memcpy 到 MppBuffer 再编码），`top` 看 CPU 占用 | ⬜ |
| — | 整理成 `V4l2Camera` 类（`src/source/`），和 Day 1 的结构一样 | ⬜ |
| — | 实验 | ⬜ |

---

## 4. 文档导航

| 顺序 | 文档 | 什么时候看 |
|:---:|---|---|
| 01 | [环境准备](guide/01_环境准备.md) | **装摄像头**（周六）、3 条检查、3A 服务、用 `v4l2-ctl` 和官方 `mpi_enc_test` 先跑通 |
| 02 | [读源码](guide/02_读源码.md) | 写代码前：按顺序读 `camera_source.c` 和 `mpi_enc_test.c` 的摄像头部分，读完自测 |
| 03 | [写代码 M1～M6](guide/03_写代码_M1到M6.md) | 写代码时：里程碑、每一步怎么验证、**stride 的坑**、怎么复用 Day 1 的编码器 |
| 04 | [摄像头和 V4L2 知识](guide/04_摄像头和V4L2知识.md) | 读源码、写 M2 遇到概念时查：采集链路、**`/dev/video31` 是什么（mainpath / selfpath、两个 ISP）**、**单平面和多平面**、像素格式、MMAP / DMABUF、缓冲区轮转、3A |

### 验证代码运行结果（`验证代码运行结果/`，每个里程碑一篇）

| 文档 | 内容 |
|---|---|
| [验证M2代码.md](验证代码运行结果/验证M2代码.md) | M2 怎么自己测：正常流程 6 项（日志、退出码、文件大小、看画面、和 v4l2-ctl 对比、动起来测）、故意制造错误 4 项、资源泄漏；测试记录 |
| [验证M3代码_零copy导入.md](验证代码运行结果/验证M3代码_零copy导入.md) | M3 怎么测：日志（dma_buf fd、stride 1080、header 40 字节、I 帧）、xxd 看 SPS/PPS、ffprobe、解码看画面；零拷贝怎么确认；故意制造错误（stride 1088 → 绿条、不 QBUF → 卡死，都实测）；资源释放；review 发现的问题 |

### 知识点（`doc/`，一个技术点一篇）

| 文档 | 内容 |
|---|---|
| [v4l2-ctl和media-ctl用法.md](doc/v4l2-ctl和media-ctl用法.md) | 两个摄像头调试命令：查能力、设格式、抓帧、测帧率、看链路、按名字找节点；参数和 ioctl / M2 代码对照；常见报错。全部板子实测 |
| [程序里怎么找到摄像头节点.md](doc/程序里怎么找到摄像头节点.md) | 自己写相机程序时不写死 `/dev/videoN`：video32 是怎么知道的、为什么光看名字不够、用 Media Controller API 从摄像头顺着连线找到 ISP 的 mainpath（完整代码，板子实测找到 /dev/video22） |
| [FindCamera输出的每个字段是什么.md](doc/FindCamera输出的每个字段是什么.md) | 子设备 `/dev/v4l-subdev2`（摄像头的旋钮）、链路 `media1 → media2`、video22 / video23 一个摄像头同时出两路（实测都是 30fps）：是什么、解决什么问题、怎么用 |
| [链接错误undefined_reference.md](doc/链接错误undefined_reference.md) | 写 FindCamera 时遇到的真实报错：编译和链接两步、为什么声明了构造函数不写实现会报错、三种改法、怎么区分编译错误和链接错误 |
| [终端彩色输出_ANSI颜色码.md](doc/终端彩色输出_ANSI颜色码.md) | 像 logcat 一样分级别着色：ANSI 颜色码原理、`src/common/log.h` 的 `LOGV/D/I/W/E` 用法、`NO_COLOR` 关颜色、为什么要 fflush、为什么不用 isatty |
| [videodev2.h是从哪来的_内核uapi头文件.md](doc/videodev2.h是从哪来的_内核uapi头文件.md) | `<linux/videodev2.h>` 是 Linux 内核自带的（不是正点原子的）：文件开头的证据、uapi 头文件怎么到 sysroot、Rockchip 私有的 `rk-*.h`、头文件和内核版本不一致有没有关系 |
| [V4L2这个名字是什么意思.md](doc/V4L2这个名字是什么意思.md) | V4L2 = Video for Linux 2（4 是 for 的谐音）、为什么有个 2、`VIDIOC_` / `v4l2_` / `V4L2_` 这些名字怎么拆、V4L2 还管哪些设备 |
| [C++小坑和常见写法.md](doc/C++小坑和常见写法.md) | 写 M2 时真实踩过的 9 个坑（`if (fd)`、宏分号、CAP_/BUF_TYPE_、`g_bufs->start`、range-for 拷贝、mmap 参数放错、没 return、reinterpret_cast、`vector{0}`）+ 输出参数的写法，都配当时的代码 |
| [重定向和_dev_null.md](doc/重定向和_dev_null.md) | `>/dev/null 2>&1` 是什么意思：程序的 0/1/2 号口、`>` `2>` `2>&1` `<`、/dev/null 黑洞、为什么 `2>&1` 要写在后面（实测）、脚本里每种写法的含义 |
| [adb_shell会吃掉终端输入.md](doc/adb_shell会吃掉终端输入.md) | 一次粘贴多行命令时后面的行消失了：现象、原因、复现方法、`< /dev/null` 修法和验证、什么时候不能加 |
| [板载麦克风录音_ALSA.md](doc/板载麦克风录音_ALSA.md) | 板子能录音：ES8388 + 板载麦克风（card 3）、ALSA 和 V4L2 对照、arecord / aplay / amixer 用法、"自己放自己录"证明麦克风在收声（实测 +43 dB）、和音视频录像的关系 |
| [画面旋转_prep_rotation和sensor翻转.md](doc/画面旋转_prep_rotation和sensor翻转.md) | 把画面转正的两种方法：编码器 `prep:rotation`（0～3 实测，90°/270° 时 MPP 自动输出 1080×1920）和摄像头 flip（只能镜像 / 180°，影响所有出口）；这块板子摄像头是侧着装的，要用 `rotation = 3` |

### 写代码时对照着看

| 文档 | 内容 |
|---|---|
| [具体编码day3_cam2h264的流程.md](具体编码day3_cam2h264的流程.md) | M2～M6 每一步的调用顺序、对照行号、**完整代码（可以直接抄）**，代码都交叉编译过、在板子上跑到了 STREAMON |

### 外部资料

- 总计划：`/Users/dev/Documents/AV/rk_work/国庆5天的学习计划/README.md` 的 Day 3 一节
- 参考源码（MPP 官方源码，读源码以它为准）：
  - `rk_code/external/mpp/utils/camera_source.c`（V4L2 采集，441 行）
  - `rk_code/external/mpp/test/mpi_enc_test.c`（摄像头作为编码输入）
- 复用的代码：`day1_yuv2h264/src/encoder/mpp_encoder.*`、`day1_yuv2h264/src/common/mpp_utils.h`
- 相关知识点：`day1_yuv2h264/doc/DRM缓冲区.md`（dma_buf、CACHABLE 和 sync、零拷贝）、`day1_yuv2h264/doc/stride.md`
