# mpi_enc_test.c 中 MPP 函数分类速查

> 📘 文中用到的 MppCtx、MppApi、MpiCmd、MppParam、MppEncCfg、MppBuffer、MppFrame、MppPacket、MppMeta 等类型是什么，见补充说明 [guide/07_MPP核心数据类型.md](guide/07_MPP核心数据类型.md)。

> 照着 [guide/06_本项目中用的mpp_函数.md](guide/06_本项目中用的mpp_函数.md) 的思路，把官方示例 `mpi_enc_test.c` 里用到的函数也分门别类整理一遍。
> 源码：`rk_code/external/mpp/test/mpi_enc_test.c`（1095 行，`demo_code_mmp/test/mpi_enc_test.c` 是同一份）。行号以当前这份为准。
> 标记：✅ = 我们的 `main.cpp` 也用了；➕ = `main.cpp` 没用，是官方示例多出来的。

---

## 0. 先看全貌

### 0.1 函数分三个"出处"，能不能用在自己的程序里不一样

这是读官方示例时**最容易踩的坑**：它里面很多函数，你自己的程序是用不了的。

| 出处 | 在哪 | 例子 | 自己的程序能用吗 |
|---|---|---|---|
| **① MPP 公开接口** | `inc/` 头文件，编进 `librockchip_mpp.so` | `mpp_create`、`mpp_buffer_get`、`mpp_frame_*`、`mpp_packet_*`、`mpp_meta_*`、`mpp_enc_cfg_*`、`mpp_enc_ref_cfg_*`、`mpp_log` / `mpp_err` | ✅ 能用，`#include <rockchip/rk_mpi.h>` 就有 |
| **② MPP 内部工具（osal）** | `osal/inc/` 头文件 | `mpp_env_get_u32`、`mpp_time`、`mpp_malloc` / `mpp_calloc` / `MPP_FREE`、`mpp_assert`、`MPP_ALIGN` | ❌ 不在公开头文件里（和 `MPP_ALIGN` 一样），自己写一个或用标准库代替 |
| **③ 测试辅助代码（utils）** | `utils/` 目录，编成静态库 `utils`，**只链接给 test 程序**（`test/CMakeLists.txt` 17 行） | `read_image`、`fill_image`、`camera_source_*`、`mpi_enc_gen_*`、`mpp_enc_roi_*`、`fps_calc_inc`、`calc_data_crc`、`mpp_log_q`、`mpi_enc_test_cmd_*` | ❌ 不在 `librockchip_mpp` 里。要用就把源码拷过来（比如我们的 `read_nv12_frame` 就是照 `read_image` 抄的） |

### 0.2 按功能分 11 类

| # | 类别 | 出处 | 主要函数 | main.cpp |
|:---:|---|:---:|---|:---:|
| 1 | 编码器实例 | ① | `mpp_create` / `mpp_init` / `mpi->reset` / `mpp_destroy` | 部分 |
| 2 | 控制命令 | ① | `mpi->control`（7 种命令） | 部分 |
| 3 | 编码参数 | ① | `mpp_enc_cfg_*`（`set_s32` 63 次、`set_u32` 3 次、`set_ptr` 1 次） | 部分 |
| 4 | 参考帧配置 | ①③ | `mpp_enc_ref_cfg_*` + `mpi_enc_gen_ref_cfg` | ➕ |
| 5 | 硬件内存 | ① | `mpp_buffer_*` | ✅ |
| 6 | 图像（输入） | ① | `mpp_frame_*` | ✅ |
| 7 | 码流（输出） | ① | `mpp_packet_*` | 部分 |
| 8 | 附加信息 meta | ① | `mpp_meta_set_*` / `mpp_meta_get_s32` | 部分 |
| 9 | 送帧 / 取包 | ① | `encode_put_frame` / `encode_get_packet` | ✅ |
| 10 | 进阶功能：ROI / OSD / 摄像头 | ③ | `mpp_enc_roi_*`、`mpi_enc_gen_osd_*`、`camera_source_*` | ➕ |
| 11 | 测试工具：读图、计时、日志、参数、线程 | ②③ | `read_image`、`mpp_time`、`mpp_env_get_u32`、`mpp_log_q`、`mpi_enc_test_cmd_*`… | ➕ |

---

## 1. 编码器实例

| 函数 | 作用 | 行号 | main.cpp |
|---|---|:---:|:---:|
| `mpp_create(&ctx, &mpi)` | 创建实例，拿到 `ctx` 和函数表 `mpi` | 914 | ✅ |
| `mpp_init(ctx, MPP_CTX_ENC, type)` | 初始化成编码器，**这一步定死 H.264 / H.265** | 929 | ✅ |
| `mpi->reset(ctx)` | ➕ 清空编码器里所有还没处理的帧和包，回到刚初始化的状态 | 961 | ➕ |
| `mpp_destroy(ctx)` | 销毁实例 | 976 | ✅ |

`reset` 在编码结束后、`destroy` 之前调用（961 行）。我们的 `main.cpp` 是等到 `pkt_eos`、所有包都取完才退出，编码器里已经没有残留，所以不调 `reset` 也没问题。以后要"中途停下、换参数重新编"时才用得上。

---

## 2. 控制命令 `mpi->control`

| 命令 | 作用 | 行号 | main.cpp |
|---|---|:---:|:---:|
| `MPP_SET_OUTPUT_TIMEOUT` | `encode_get_packet` 阻塞等待 | 923 | ✅ |
| `MPP_ENC_GET_CFG` | 读默认参数 | 941 | ✅ |
| `MPP_ENC_SET_CFG` | 提交参数 | 501 | ✅ |
| `MPP_ENC_SET_HEADER_MODE` | 每个 IDR 前带 SPS/PPS（只对 H.264 / H.265，523 行有判断） | 525 | ✅ |
| `MPP_ENC_GET_HDR_SYNC` | 取 SPS/PPS 写到文件开头 | 574 | ✅ |
| `MPP_ENC_SET_SEI_CFG` | ➕ 设置 SEI（附加信息）模式，默认 `MPP_ENC_SEI_MODE_ONE_FRAME` | 516 | ➕ |
| `MPP_ENC_SET_OSD_PLT_CFG` | ➕ 设置 OSD（叠加文字/图标）调色板 | 708 | ➕ |

**为什么我们的 `out.h264` 里也有 SEI？** 之前用 `xxd` 看到 IDR 前面有 `06`（SEI），内容是 `unknown mpp version...`。`main.cpp` 没设 SEI，那是编码器的默认行为；`mpi_enc_test` 是显式设了一次。

---

## 3. 编码参数 `mpp_enc_cfg_*`

| 函数 | 作用 | 行号 | main.cpp |
|---|---|:---:|:---:|
| `mpp_enc_cfg_init(&cfg)` | 创建参数对象 | 935 | ✅ |
| `mpp_enc_cfg_set_s32(cfg, key, val)` | 设整数参数（**63 次**） | 318～485 | ✅（21 次） |
| `mpp_enc_cfg_set_u32(cfg, key, val)` | ➕ 设无符号整数参数（3 次，丢帧相关） | 337～339 | ➕ |
| `mpp_enc_cfg_set_ptr(cfg, key, ptr)` | ➕ 设指针参数（参考帧配置） | 498 | ➕ |
| `mpp_enc_cfg_deinit(cfg)` | 释放参数对象 | 981 | ✅ |

### 3.1 用到的全部 key（按模块分）

| 模块 | key | main.cpp | 说明 |
|---|---|:---:|---|
| `prep:` 输入图像 | `width` `height` `hor_stride` `ver_stride` `format` | ✅ | 320～324 |
| | `mirroring` `rotation` `flip` | ➕ | 镜像、旋转、翻转（481～483，值来自环境变量） |
| `rc:` 码率控制 | `mode` | ✅ | 326 |
| | `fps_in_flex/num/denom` `fps_out_flex/num/denom` | ✅ | 329～334 |
| | `bps_target` `bps_max` `bps_min` | ✅ | 341～361，按 CBR / VBR / AVBR 分别算上下限 |
| | `gop` | ✅ | 485 |
| | `drop_mode` `drop_thd` `drop_gap`（**u32**） | ➕ | 码率超限时要不要丢帧（337～339，设成不丢） |
| | `qp_init` `qp_max` `qp_min` `qp_max_i` `qp_min_i` `qp_ip` | ➕ | 量化参数范围（365～410，按编码格式和码率模式分别设） |
| | `fqp_min_i` `fqp_max_i` `fqp_min_p` `fqp_max_p` | ➕ | 帧级 QP 范围 |
| | `ref_cfg`（**ptr**） | ➕ | 参考帧结构，见第 4 节 |
| `codec:` | `type` | ✅ | 423 |
| `h264:` | `profile` `level` `cabac_en` `cabac_idc` | ✅ | 434～444 |
| | `trans8x8` | ➕ | 8×8 变换（High Profile 才有），446 |
| | `constraint_set` | ➕ | 约束标志，值来自环境变量，448～451 |
| `jpeg:` | `q_factor` `qf_max` `qf_min` | ➕ | JPEG 质量因子（MJPEG 才用），414～416 |
| `split:` | `mode` `arg` `out` | ➕ | slice 切分（低延迟），472～474 |
| `tune:` | `scene_mode` | ➕ | 场景模式调优，318 |

**QP 是什么**：量化参数，越大压得越狠、画质越差。我们的 `main.cpp` 没设 QP，用的是 `GET_CFG` 拿到的默认值，CBR 下由码率控制自动调。`mpi_enc_test` 的 FIXQP 模式（370～383）把所有 QP 设成同一个值，就是"固定画质、不管码率"。

---

## 4. 参考帧配置 ➕

```c
// mpi_enc_test.c 488～508
mpp_env_get_u32("gop_mode", &gop_mode, gop_mode);   // 默认 0：不设，用普通的 IPPP...
if (gop_mode) {
    mpp_enc_ref_cfg_init(&ref);                     // ① 公开接口：创建参考帧配置
    if (p->gop_mode < 4)
        mpi_enc_gen_ref_cfg(ref, gop_mode);         // ③ utils：生成 TSVC 等分层结构
    else
        mpi_enc_gen_smart_gop_ref_cfg(ref, ...);    // ③ utils：智能 GOP
    mpp_enc_cfg_set_ptr(cfg, "rc:ref_cfg", ref);    // 挂到参数上
}
mpi->control(ctx, MPP_ENC_SET_CFG, cfg);
if (ref) mpp_enc_ref_cfg_deinit(&ref);              // SET_CFG 之后就能释放
```
| 函数 | 出处 | 行号 |
|---|:---:|:---:|
| `mpp_enc_ref_cfg_init` / `mpp_enc_ref_cfg_deinit` | ① `inc/rk_venc_ref.h` | 491 / 508 |
| `mpi_enc_gen_ref_cfg` / `mpi_enc_gen_smart_gop_ref_cfg` | ③ `utils/mpi_enc_utils.c` | 494 / 496 |

默认不开（`gop_mode = 0`），出来就是我们看到的 `I P P P ...`。这是进阶内容（时域分层、长期参考帧），Day 1 不用管。

---

## 5. 硬件内存 `mpp_buffer_*`

| 函数 | 作用 | 行号 | main.cpp |
|---|---|:---:|:---:|
| `mpp_buffer_group_get_internal` | 建 DRM 内存池 | 889 | ✅ |
| `mpp_buffer_get` | 拿一块内存，**用了 3 次** | 895 / 901 / 907 | ✅（2 次） |
| `mpp_buffer_get_ptr` | CPU 地址 | 594 | ✅ |
| `mpp_buffer_sync_begin` / `sync_end` | CPU 写之前 / 写完之后（文件输入、生成测试图两处都有） | 600 / 616，619 / 624 | ✅ |
| `mpp_buffer_put` | 还回内存，**4 次** | 986 / 991 / 996 / 1001 | ✅（2 次） |
| `mpp_buffer_group_put` | 销毁内存池 | 1006 | ✅ |

**比我们多申请的内存**：

| buffer | 大小 | 用途 |
|---|---|---|
| `frm_buf` | `frame_size + header_size` | 输入图像。多出的 `header_size` 是给 FBC 压缩格式的头留的，NV12 时为 0 |
| `pkt_buf` | `frame_size` | 输出码流。**每一帧都用它**（见第 7、8 节），我们只在取 SPS/PPS 时用了一次 |
| `md_info` ➕ | `mdinfo_size`（181～185 行按格式算） | 运动检测信息，编码器顺便输出的"哪里在动" |
| `osd_data.buf` ➕ | — | OSD 叠加数据，在 `mpi_enc_gen_osd_data` 里申请 |

---

## 6. 图像（输入）`mpp_frame_*`

| 函数 | 作用 | 行号 | main.cpp |
|---|---|:---:|:---:|
| `mpp_frame_init` | 创建 frame | 640 | ✅ |
| `mpp_frame_set_width/height/hor_stride/ver_stride/fmt` | 描述图像 | 646～650 | ✅ |
| `mpp_frame_set_eos` | 最后一帧标志 | 651 | ✅ |
| `mpp_frame_set_buffer` | 挂图像数据，**3 种情况** | 654 / 656 / 658 | ✅ |
| `mpp_frame_get_meta` | ➕ 拿到 frame 的 meta，往里挂附加信息 | 660 | ➕ |
| `mpp_frame_deinit` | 释放（put 失败时 761，正常 765） | 761 / 765 | ✅ |

`set_buffer` 的 3 种情况（653～658）：
```c
if (p->fp_input && feof(p->fp_input))
    mpp_frame_set_buffer(frame, NULL);      // 文件读完：空帧，只带 EOS（我们也是这么做的）
else if (cam_buf)
    mpp_frame_set_buffer(frame, cam_buf);   // 摄像头输入：直接用摄像头的 buffer，不拷贝（Day 3）
else
    mpp_frame_set_buffer(frame, p->frm_buf);// 文件输入 / 生成的测试图
```

---

## 7. 码流（输出）`mpp_packet_*`

| 函数 | 作用 | 行号 | main.cpp |
|---|---|:---:|:---:|
| `mpp_packet_init_with_buffer` | 用 `pkt_buf` 包一个 packet：取 SPS/PPS 一次，**每帧又一次** | 570 / 661 | ✅（只有第一次） |
| `mpp_packet_set_length(pkt, 0)` | 清零（源码注释："It is important to clear output packet length!!"） | 572 / 663 | ✅ |
| `mpp_packet_get_pos` / `get_length` | 有效数据的起点 / 长度 | 581～582，778～779 | ✅ |
| `mpp_packet_get_eos` | 最后一个包 | 787 | ✅ |
| `mpp_packet_is_partition` | ➕ 这个包是不是"一帧的一部分"（slice 分片输出） | 802 | ➕ |
| `mpp_packet_is_eoi` | ➕ 是不是一帧的最后一片（End Of Image） | 803 | ➕ |
| `mpp_packet_has_meta` / `get_meta` | 取附加信息 | 813 / 814 | ✅ |
| `mpp_packet_deinit` | 释放 | 588 / 838 | ✅ |

**和 `main.cpp` 最大的区别：输出 packet 是谁的内存**

| | mpi_enc_test.c | main.cpp |
|---|---|---|
| 做法 | 每帧都用自己的 `pkt_buf` 包一个 packet，通过 frame 的 meta（`KEY_OUTPUT_PACKET`，664 行）告诉编码器"码流写到这里" | 不给，编码器自己分配（`mpp_enc_impl.cpp` 1400～1443 行） |
| 好处 | 码流写在哪由自己控制，内存可以复用、可以预先分配 | 写法简单 |

**取包为什么套了一层 `do { ... } while (!eoi)`**（767～849）：开了 slice 分片（`split:mode`）时，一帧会分成好几个 packet 输出，要一直取到 `eoi` 才算这一帧取完。没开分片时 `eoi` 一直是 1，循环只走一次。我们没开分片，所以 `main.cpp` 不需要这层。

---

## 8. 附加信息 `mpp_meta_*`

meta 是挂在 frame / packet 上的"附加信息口袋"，按 key 存取。

### 8.1 往 frame 里放（送给编码器的）➕
| 函数 | key | 放了什么 | 行号 |
|---|---|---|:---:|
| `mpp_meta_set_packet` | `KEY_OUTPUT_PACKET` | 码流写到哪个 packet | 664 |
| `mpp_meta_set_buffer` | `KEY_MOTION_INFO` | 运动信息写到哪个 buffer（`md_info`） | 665 |
| `mpp_meta_set_ptr` | `KEY_USER_DATA` / `KEY_USER_DATAS` | 用户自定义数据（会以 SEI 的形式写进码流） | 675 / 697 |
| `mpp_meta_set_ptr` | `KEY_OSD_DATA` | OSD 叠加数据 | 717 |

### 8.2 从 packet 里取（编码器告诉我们的）
| key | 意思 | 行号 | main.cpp |
|---|---|:---:|:---:|
| `KEY_TEMPORAL_ID` | 时域层级（分层参考时用） | 820 | ➕ |
| `KEY_LONG_REF_IDX` | 长期参考帧编号 | 824 | ➕ |
| `KEY_ENC_AVERAGE_QP` | **这一帧的平均 QP**（可以看画质/码率控制情况） | 828 | ➕ |
| `KEY_ENC_USE_LTR` | 是否用了长期参考帧 | 832 | ➕ |
| `KEY_OUTPUT_INTRA` | 是不是 I 帧 | — | ✅（**mpi_enc_test 反而没用**） |

写法上它比我们多了一层判断：`if (MPP_OK == mpp_meta_get_s32(...))`，取成功了才用。key 不存在时返回非 0，不会拿到垃圾值。

---

## 9. 送帧 / 取包

| 函数 | 行号 | main.cpp |
|---|:---:|:---:|
| `mpi->encode_put_frame(ctx, frame)` | 758 | ✅ |
| `mpi->encode_get_packet(ctx, &packet)` | 768 | ✅ |

和我们一样，put 完马上 `frame_deinit`（765）。注释（751～757）说明了原因：默认是阻塞模式，"谁创建的资源谁释放"。

---

## 10. 进阶功能（③ utils）➕

### 10.1 ROI：指定区域编得更清楚
| 函数 | 作用 | 行号 |
|---|---|:---:|
| `mpp_enc_roi_init` | 创建 ROI 上下文（环境变量 `roi_enable=1` 才开） | 539 |
| `mpp_enc_roi_add_region` | 添加一个区域（比如画面中间的人脸） | 732 / 742 |
| `mpp_enc_roi_setup_meta` | 把 ROI 信息放进 frame 的 meta | 745 |
| `mpp_enc_roi_deinit` | 释放 | 1011 |

### 10.2 OSD：在画面上叠加文字/图标
| 函数 | 作用 | 行号 |
|---|---|:---:|
| `mpi_enc_gen_osd_plt` | 生成调色板，配合 `MPP_ENC_SET_OSD_PLT_CFG` | 702 |
| `mpi_enc_gen_osd_data` | 生成叠加数据，配合 `KEY_OSD_DATA` | 715 |

### 10.3 摄像头输入（Day 3 会用到）
| 函数 | 作用 | 行号 |
|---|---|:---:|
| `camera_source_init` | 打开 `/dev/videoX`（`-i` 以 `/dev/video` 开头时） | 190 |
| `camera_source_get_frame` | 取一帧，返回帧编号 | 626 |
| `camera_frame_to_buf` | 把摄像头的帧变成 `MppBuffer`，**直接交给编码器，不拷贝** | 635 |
| `camera_source_put_frame` | 用完还回去（前 50 帧不稳定直接丢掉：629～632） | 631 / 852 |
| `camera_source_deinit` | 关闭摄像头 | 271 |

---

## 11. 测试工具（②③）➕

| 用途 | 函数 | 出处 | 行号 | 我们怎么替代 |
|---|---|:---:|:---:|---|
| 读一帧 | `read_image` | ③ utils.c | 601 | `read_nv12_frame`（照抄 NV12 分支） |
| 没有输入文件时生成测试图 | `fill_image` | ③ utils.c | 620 | — |
| 统计 fps | `fps_calc_inc` | ③ utils.c | 839 | M6 的 `std::chrono` |
| 校验码流 CRC | `calc_data_crc` / `write_data_crc` | ③ utils.c | 793 / 795 | — |
| 取时间（微秒） | `mpp_time` | ② osal | 750 / 785 / 953 / 955 | `std::chrono::steady_clock` |
| 读环境变量 | `mpp_env_get_u32` | ② osal | 13 次，见下表 | `getenv` + `atoi` |
| 日志 | `mpp_log` / `mpp_err` / `mpp_err_f` | ① `inc/mpp_log.h` | 多处 | `printf` |
| 可静音的日志 | `mpp_log_q(quiet, ...)` | ③ utils.h 48 行 | 多处 | — |
| 断言 | `mpp_assert` | ② osal | 5 处 | `assert` |
| 内存 | `mpp_malloc` / `mpp_calloc` / `MPP_FREE` | ② osal | 560 / 1027 / 864、1069 | `malloc` / `std::vector` |
| 命令行参数 | `mpi_enc_test_cmd_get` / `update_by_arg` / `show_opt` / `put` | ③ mpi_enc_utils.c | 1080～1092 | M5 的 `parse_args` |
| 多路并发 | `pthread_create` / `pthread_join` | 标准库 | 1038 / 1057 | 只跑一路 |

### 11.1 用环境变量打开的隐藏功能（`mpp_env_get_u32`，13 处）

`mpi_enc_test` 很多功能**不走命令行参数，而是读环境变量**，默认都是关的：

| 环境变量 | 默认 | 作用 | 行号 |
|---|---|---|:---:|
| `constraint_set` | 0 | H.264 约束标志 | 448 |
| `split_mode` / `split_arg` / `split_out` | 0 | slice 分片输出 | 465～467 |
| `mirroring` / `rotation` / `flip` | 0 | 镜像 / 旋转 / 翻转 | 477～479 |
| `gop_mode` | 0 | 参考帧结构 | 488 |
| `sei_mode` | `ONE_FRAME` | SEI 模式 | 514 |
| `osd_enable` / `osd_mode` | 0 | OSD 叠加 | 533 / 534 |
| `roi_enable` | 0 | ROI | 535 |
| `user_data_enable` | 0 | 往码流里塞自定义数据 | 536 |

在板子上试（⚠️ 这条命令还没实测过；旋转 90° 后输出应该是 1080×1920，用 ffprobe 看宽高能确认）：
```bash
LD_LIBRARY_PATH=/userdata/mpp_build/lib rotation=90 /userdata/mpp_build/bin/mpi_enc_test \
    -w 1920 -h 1080 -t 7 -i /userdata/av/in_1080p_60f.nv12 -o /userdata/av/rot.h264 -n 60
```

---

## 12. 申请和释放对照

| 申请 | 释放 | 行号 | main.cpp |
|---|---|:---:|:---:|
| `mpp_create` | `mpp_destroy` | 914 → 976 | ✅ |
| `mpp_enc_cfg_init` | `mpp_enc_cfg_deinit` | 935 → 981 | ✅ |
| `mpp_enc_ref_cfg_init` | `mpp_enc_ref_cfg_deinit` | 491 → 508 | ➕ |
| `mpp_buffer_group_get_internal` | `mpp_buffer_group_put` | 889 → 1006 | ✅ |
| `mpp_buffer_get` × 3 | `mpp_buffer_put` × 4（多一个 OSD 的） | 895～907 → 986～1001 | ✅ |
| `mpp_frame_init` | `mpp_frame_deinit` | 640 → 765 | ✅ |
| `mpp_packet_init_with_buffer` / `encode_get_packet` | `mpp_packet_deinit` | 570、661、768 → 588、838 | ✅ |
| `mpp_enc_roi_init` | `mpp_enc_roi_deinit` | 539 → 1011 | ➕ |
| `camera_source_init` | `camera_source_deinit` | 190 → 271 | ➕ |
| `mpp_malloc` / `mpp_calloc` | `MPP_FREE` | 560、1027 → 864、1069 | ➕ |

释放的写法和我们一样：`enc_test` 里出错就 `goto MPP_TEST_OUT`（类似我们的 `goto CLEANUP`），在 974～1015 行统一释放。

---

## 13. 和 main.cpp 对比：官方示例多做了什么

| 方面 | mpi_enc_test.c | main.cpp | 要不要学 |
|---|---|---|---|
| 主线流程 | create → init → cfg → 头 → 循环 put/get → destroy | 一样 | 已经会了 ✅ |
| 输出 packet | 每帧用自己的 `pkt_buf` + `KEY_OUTPUT_PACKET` | 编码器自己分配 | 了解即可 |
| 码率控制 | 还设了 QP 范围、丢帧策略 | 只设码率，QP 用默认 | 做实验时可以加 |
| 取包 | `do...while(!eoi)` 支持分片 | 一帧一个包 | 低延迟场景再学 |
| 结束 | 多调一次 `mpi->reset` | 不调 | 可以加，没坏处 |
| 统计 | 平均 QP、时域层级等 | I 帧、码率、fps | 可以加 `KEY_ENC_AVERAGE_QP` |
| 输入来源 | 文件 / 摄像头 / 自动生成测试图 | 文件 | Day 3 学摄像头 |
| 进阶功能 | ROI、OSD、SEI 用户数据、参考帧、旋转镜像（环境变量打开） | 无 | 以后按需 |
| 参数检查 | 每个 `control` 都检查；`cfg_set_*` 也**没检查**返回值 | `control` 用 `CHECK`；`cfg_set` 只检查了 1 处 | 两边都可以改进 |
| 多路 | `pthread` 开多个线程，每个线程一路编码 | 一路 | 了解即可 |
