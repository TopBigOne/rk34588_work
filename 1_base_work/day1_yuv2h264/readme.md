# Day 1 · MPP 硬件编码：`day1_yuv2h264`

> **一句话目标**：读一个 NV12 文件 → 用 RK3588 的 MPP 硬件编码成 H.264（改一个参数就能出 H.265）→ 在 Mac 上用 VLC 能流畅播放
> **总时间**：约 8～9 小时（准备 0.5h + 读代码 3h + 写代码 4h + 实验 1.5h）

---

## 1. 这个程序在做什么

```
 Mac                              RK3588 开发板
 ─────────────────────            ─────────────────────────────────────────────────────────
 aaa.264                          /userdata/av/in_1080p_60f.nv12        （60 帧原始图像，178MB）
   │ ffmpeg 解码成 NV12                    │
   ▼                                       │ ① 每次读一帧（3,110,400 字节），按 stride 逐行放进
 in_1080p_60f.nv12 ──adb push──→           ▼    硬件能访问的内存（DRM buffer）
                                   ┌──────────────────┐
                                   │ MPP 硬件编码器      │ ② 压缩：一帧 3MB → 几十 KB
                                   │ （RK3588 的 VPU）   │
                                   └──────────────────┘
                                           │ ③ 码流按顺序写进文件：SPS/PPS + 第 0 帧 + 第 1 帧 ...
                                           ▼
 VLC 播放 ←────────adb pull──────  /userdata/av/out.h264                 （H.264 裸流）
```

| | 输入 | 输出 |
|---|---|---|
| 文件 | `in_1080p_60f.nv12` | `out.h264` |
| 内容 | 没压缩的 YUV 像素（NV12） | 压缩后的 H.264 码流 |
| 大小 | 每帧 1920 × 1080 × 3/2 = 3,110,400 字节，固定 | 每帧几 KB ～ 几十 KB，I 帧大、P 帧小 |
| MPP 里叫 | `MppFrame`（装在 `MppBuffer` 里） | `MppPacket` |

---

## 2. `main.cpp` 只做了 6 件事

看代码时先认出"现在在第几件事"，就不会迷路：

| # | 在干什么 | 比喻 | 关键调用 | 里程碑 |
|:---:|---|---|---|:---:|
| ① | **开一台编码机** | 找一家印刷厂 | `mpp_create` → `mpp_init` | M1 |
| ② | **告诉它要什么效果** | 填订单：尺寸、码率、格式 | `mpp_enc_cfg_set_s32` × N → `MPP_ENC_SET_CFG` | M1 |
| ③ | **要一份"说明书"写在文件开头** | 先拿解码说明 | `MPP_ENC_GET_HDR_SYNC` → `fwrite`（SPS/PPS，40 字节） | M2 |
| ④ | **把照片放进机器能拿到的地方** | 放进专用托盘 | `mpp_buffer_get` → `ReadYUV::read_frame`（逐行）→ `sync_end` | M2 / M3 |
| ⑤ | **送进去，拿出来** | 交给机器，取回成品 | `mpp_frame_init` → `encode_put_frame` → `encode_get_packet` → `fwrite` | M3 / M4 |
| ⑥ | **收拾干净** | 关机、还托盘 | RAII：`MppEncoder` 析构 + `src/common/mpp_utils.h` 的 `XxxPtr` 句柄，离开作用域自动 deinit / destroy / put / fclose | 每一步 |

M4 就是把 ④⑤ 放进循环重复 60 次，最后送一个 EOS 告诉编码器"没有了"。

几个看着很怪的东西，各自解决什么问题：

| 东西 | 为什么非要它 |
|---|---|
| `hor_stride` / `ver_stride` | 硬件按 16 对齐：1080 要补成 1088 |
| `mpp_buffer_get`（不用 `malloc`） | 硬件通过 DMA 读内存，只能读 DRM 内存 |
| `ReadYUV::read_frame` 逐行读 | 文件里紧密排列，缓冲区里每行、每个平面都有填充，要一行一行摆到对的位置 |
| `sync_begin` / `sync_end` | CPU 写的数据可能还在缓存里，刷下去硬件才看得到 |
| RAII（`MppEncoder`、`FilePtr`、`MppBufferPtr` ……） | 任何一步失败直接 `return`，已经申请的资源自动释放，不会漏、不会重复 |

---

## 3. 进度

| 里程碑 | 内容 | 状态 |
|:---:|---|:---:|
| M1 | 编码器初始化 | ✅ |
| M2 | 取 SPS/PPS 写入文件 | ✅ |
| M3 | 编码 1 帧 | ✅ |
| M4 | 编完 60 帧，处理 EOS | ✅ |
| M5 | 命令行参数（`-t h265` 等） | ✅ |
| M6 | 统计汇总 | ✅ |
| — | 整理成 `MppEncoder` 类（`src/encoder/mpp_encoder.*`，RAII） | ✅ |
| — | 实验 | ⬜ |

---

## 4. 文档导航

> 🗺️ **先看这张总览**：[doc/技术点地图.md](doc/技术点地图.md) —— 这个项目涉及的所有技术点（视频基础、MPP、C/C++、设计、IO、工具链、调试），每个点链接到对应文档和代码，附 14 道自测题。

### 4.1 按学习顺序（`guide/`）

| 顺序 | 文档 | 什么时候看 |
|:---:|---|---|
| 01 | [环境准备](guide/01_环境准备.md) | 第一次搭环境：软链接、CLion、测试素材、**板子上的 MPP 库**、运行脚本 |
| 02 | [读源码](guide/02_读源码.md) | 写代码前：按顺序读 `mpi_enc_test.c` 和 `read_image`，读完自测 7 个问题 |
| 03 | [写代码 M1～M6](guide/03_写代码_M1到M6.md) | 写代码时：里程碑、运行方法、怎么验证、之后怎么整理成类 |
| 04 | [实验](guide/04_实验.md) | 功能调通后：码率模式、GOP、H.264 vs H.265 |
| 05 | [验收清单和踩坑](guide/05_验收清单和踩坑.md) | 出问题时查、收尾时对照 |
| 06 | [本项目中用的 MPP 函数](guide/06_本项目中用的mpp_函数.md) | 查 main.cpp 里某个 MPP 函数的用法 |
| 07 | [MPP 核心数据类型](guide/07_MPP核心数据类型.md) | 弄清 MppCtx、MppApi、MpiCmd、MppParam、MppBuffer、MppMeta 等是什么 |

### 4.2 写代码时对照着看

| 文档 | 内容 |
|---|---|
| [具体写day1_yuv2h264的流程.md](具体写day1_yuv2h264的流程.md) | M1～M6 每一步的调用顺序、对照行号、**完整代码** |

### 4.3 知识点

| 文档 | 内容 |
|---|---|
| [nv12中yuv分布效果图和读取方式.md](nv12中yuv分布效果图和读取方式.md) | NV12 在文件和硬件缓冲区里的布局，为什么要逐行读 |
| [fread和fwrite 的详细用法.md](doc/fread和fwrite%20的详细用法.md) | fread / fwrite / fseek / feof，以及项目里的用法 |
| [MppPacket和MppFrame的用法和区别.md](MppPacket和MppFrame的用法和区别.md) | 两个核心对象：字段、创建释放、引用计数、编码/解码时的角色 |
| [doc/void* 能转成任何对象指针.md](doc/%20void*%20能转成任何对象指针.md) | `void*` 为什么能转成 `uint8_t*`、指针运算、码流怎么拷出来 |
| [doc/vector的assign() 的用法.md](<doc/vector的assign() 的用法.md>) | `assign(first, last)`、左闭右开区间、size 和 capacity、每帧复用内存 |
| [doc/DRM缓冲区.md](doc/DRM缓冲区.md) | 硬件能 DMA 访问的内存、为什么不能 malloc、CACHABLE 和 sync、板子上实际走 DMA-HEAP（strace 实测）、零拷贝 |
| [doc/fwrite和write的区别.md](doc/fwrite和write的区别.md) | C 标准库和系统调用、用户态缓冲、板子实测、短写、落盘 |

### 4.4 笔记

| 文档 | 内容 |
|---|---|
| [doc/笔记_10_02.md](doc/笔记_10_02.md) | Day 1 当天的笔记：进度、自测、下一步 |
| [doc/stride.md](doc/stride.md) | stride 是什么、为什么对齐、16 和 64 两次对齐、代码里在哪用 |
| [doc/sps_pps.md](doc/sps_pps.md) | SPS / PPS 是什么、文件头 40 字节逐字节拆解、NAL 类型、`GET_HDR_SYNC` |
| [doc/h265_vps.md](doc/h265_vps.md) | H.265 多出来的 VPS |
| [doc/gop.md](doc/gop.md) | GOP、I/P/B/IDR、实测没有 B 帧、GOP 大小怎么选 |
| [doc/unique_ptr自定义删除器_RAII句柄.md](doc/unique_ptr自定义删除器_RAII句柄.md) | `mpp_utils.h` 里 `XxxDeleter` + `unique_ptr` 的写法：函数对象、自定义删除器、RAII |

### 4.5 外部资料

- MPP 笔记：`/Users/dev/Documents/AV/openedv/Tone_rk358/doc/MPP开发指南_学习笔记.md`
- 总计划：`/Users/dev/Documents/AV/rk_work/国庆5天的学习计划/README.md`
- 参考源码（软链接，见 [01_环境准备.md](guide/01_环境准备.md)）：
  - `demo_code_mmp/` → `rk_code/external/mpp`（MPP 官方源码，读源码以它为准）
  - `demo_code/` → 正点原子 A 盘里的 demo 源码
