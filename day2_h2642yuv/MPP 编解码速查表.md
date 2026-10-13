# MPP 编解码速查表

> RK3588 MPP 硬件编解码的一页速查：**两套骨架 + 一条 `&` 规律 + 常见坑**。
> 不用背参数顺序，记住骨架，细节回来查这一页；每一条都对应到 Day 1（编码）/ Day 2（解码）的代码和文档。
>
> 官方参考：`rk_code/external/mpp/doc/Rockchip_Developer_Guide_MPP_CN.md`（第三章 3.1～3.3 解码、3.4～3.6 编码）
>
> 文中的文件路径都相对 `rk_work/` 目录。这一页在 `rk_work/`、`day1_yuv2h264/`、`day2_h2642yuv/` 各放了一份，内容相同；改的时候以 `rk_work/` 这份为准，再复制过去。

---

## 1. 共同基础

### 1.1 七个对象（全是 `void*` 句柄）

| 类型 | 是什么 | 怎么来 | 怎么释放 |
|---|---|---|---|
| `MppCtx` | 一个编 / 解码器**实例** | `mpp_create(&ctx, &mpi)` | `mpp_destroy(ctx)` |
| `MppApi*` | 操作实例的**函数表**（`control`、`decode_put_packet`、`encode_put_frame`……） | 同上，和 ctx 一起来 | 不用释放（全局的） |
| `MppEncCfg` / `MppDecCfg` | 参数集合 | `mpp_enc_cfg_init(&cfg)` / `mpp_dec_cfg_init(&cfg)` | `mpp_enc_cfg_deinit(cfg)` / `mpp_dec_cfg_deinit(cfg)` |
| `MppPacket` | 一段**压缩码流**（外壳） | `mpp_packet_init(&pkt, 数据, 长度)`，或编码器给的 | `mpp_packet_deinit(&pkt)` |
| `MppFrame` | 一帧**图像**的外壳（宽高、stride、格式、标志位） | `mpp_frame_init(&frm)`，或解码器给的 | `mpp_frame_deinit(&frm)` |
| `MppBuffer` | 真正装像素的**硬件内存**（DMA，硬件能直接读写） | `mpp_buffer_get(group, &buf, size)`，或挂在解码器给的 frame 上 | `mpp_buffer_put(buf)`（引用计数 −1） |
| `MppBufferGroup` | **内存池**：一组 buffer | `mpp_buffer_group_get_internal(&grp, MPP_BUFFER_TYPE_DRM)` | `mpp_buffer_group_put(grp)` |

`MppFrame` 只是外壳，像素在它指向的 `MppBuffer` 里：`mpp_frame_get_buffer(frm)` → `mpp_buffer_get_ptr(buf)` 才拿到 CPU 地址。

### 1.2 一条规律：什么时候传 `&`

> **要把新对象写回给你的函数，传 `&`**：`xxx_init(&h)`、`mpp_create(&ctx, &mpi)`、`decode_get_frame(ctx, &frm)`、`encode_get_packet(ctx, &pkt)`、`mpp_buffer_get(grp, &buf, …)`
> **其他一律直接传句柄**：`control(ctx, 命令, cfg)`、`mpp_xxx_cfg_set_s32(cfg, …)`、`mpp_destroy(ctx)`、`mpp_xxx_cfg_deinit(cfg)`、`mpp_packet_set_eos(pkt)`
> **例外**：`mpp_packet_deinit(&pkt)`、`mpp_frame_deinit(&frm)` 也传 `&`——它们要把你的变量置成 NULL

原因：句柄本身就是"门牌号"。函数要**改你的变量**（写进新门牌号、或者清成 NULL）才要变量的地址；只是**用这个对象**，给门牌号就够了。
⚠️ `void**` 也能隐式转成 `void*`，传错了**编译器不报错**，见第 6 节。

### 1.3 配置一律"取 → 改 → 写回"

```
mpp_xxx_cfg_init(&cfg)                      创建空的参数对象
control(ctx, MPP_XXX_GET_CFG, cfg)          把当前（默认）参数读进来
mpp_xxx_cfg_set_s32 / set_u32(cfg, "key", 值) × N
control(ctx, MPP_XXX_SET_CFG, cfg)          写回，才真正生效（不调这一句，前面全部白设）
```

key 是字符串，拼错时 `set_xxx` 返回 `MPP_NOK`，所以每一句都要检查返回值。

### 1.4 返回值

`MPP_RET`：**0 = 成功（`MPP_OK`），负数 = 失败**。判断写 `ret != MPP_OK`，别写 `!ret`（容易读反）。

| 值 | 名字 | 常见原因 |
|---|---|---|
| 0 | `MPP_OK` | |
| −1 | `MPP_NOK` | 通用失败；cfg 的 key 拼错 |
| −2 | `MPP_ERR_UNKNOW` | **ctx 无效**：比如把 `mpi` 当成 `ctx` 传了（`mpi.cpp` 的 `_check_mpp_ctx`，日志 `found invalid context`） |
| −3 | `MPP_ERR_NULL_PTR` | 传了空指针 |
| −6 | `MPP_ERR_VALUE` | 参数值不合法 |
| −8 | `MPP_ERR_TIMEOUT` | 超时 |
| −1012 | `MPP_ERR_BUFFER_FULL` | **解码输入队列满**：不是错误，等一下原样重送 |

完整列表：`sysroot/usr/include/rockchip/mpp_err.h`。

---

## 2. 编码骨架（Day 1：NV12 → H.264 / H.265）

```
① 开机        mpp_create(&ctx, &mpi)
              control(ctx, MPP_SET_OUTPUT_TIMEOUT, &MPP_POLL_BLOCK)    ← mpp_init 之前
              mpp_init(ctx, MPP_CTX_ENC, MPP_VIDEO_CodingAVC / HEVC)

② 设参数      mpp_enc_cfg_init(&cfg) → control(MPP_ENC_GET_CFG, cfg)
              set_s32 × N：prep:width/height/hor_stride/ver_stride/format（输入图像长什么样）
                           rc:mode/bps_target/bps_max/bps_min/fps_*/gop（码率控制）
                           codec:type、h264:profile/cabac_en/…
              control(MPP_ENC_SET_CFG, cfg)
              control(MPP_ENC_SET_HEADER_MODE, &MPP_ENC_HEADER_MODE_EACH_IDR)

③ 文件头      mpp_packet_init(&hdr, 64KB 容器) → mpp_packet_set_length(hdr, 0)   ← 一定要清零
              control(MPP_ENC_GET_HDR_SYNC, hdr) → 按 pos + length 拷出 SPS/PPS(/VPS)，写在文件最开头

④ 输入内存    mpp_buffer_group_get_internal(&grp, MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE)
              mpp_buffer_get(grp, &buf, ALIGN(hor_stride, 64) × ALIGN(ver_stride, 64) × 3/2)   ← Day 1 多按 64 对齐留余量
              每帧：sync_begin → 按 stride 把 NV12 写进 mpp_buffer_get_ptr(buf) → sync_end

⑤ 每一帧      mpp_frame_init(&frm) → set width/height/hor_stride/ver_stride/fmt（和 prep:* 一致）
              mpp_frame_set_buffer(frm, buf)        [最后一次：buffer = NULL + mpp_frame_set_eos]
              mpi->encode_put_frame(ctx, frm) → mpp_frame_deinit(&frm)
              mpi->encode_get_packet(ctx, &pkt)
              ├─ pkt 为 NULL？→ 这次没有，下一轮
              ├─ 按 pos + length 拷出码流写文件（不是 data + size）
              ├─ I 帧？→ meta 里 KEY_OUTPUT_INTRA
              └─ eos？→ 结束
              mpp_packet_deinit(&pkt)               ← 每个包都要

⑥ 收拾        mpp_enc_cfg_deinit(cfg) → mpp_destroy(ctx) → mpp_buffer_put(buf) → mpp_buffer_group_put(grp)
```

| 关键点 | 为什么 | 文档 |
|---|---|---|
| stride 按 16 对齐 | 硬件要求：1920x1080 → 1920x1088 | `day1_yuv2h264/doc/stride.md` |
| SPS/PPS 写在最前面 | 解码器先拿到它们才能解码；`EACH_IDR` 让每个 IDR 前都带一份，从中间开始播也能解 | `day1_yuv2h264/doc/sps_pps.md` |
| 读码流用 `pos + length` | `data + size` 是整个容器，不是有效数据 | `day1_yuv2h264/MppPacket和MppFrame的用法和区别.md` |
| CPU 写完要 `sync_end` | 用了 CACHABLE，CPU 写的数据还在缓存里，硬件读不到 | `day1_yuv2h264/doc/DRM缓冲区.md` |
| 最后送一个空帧带 EOS | 告诉编码器"没有了"，它才吐出最后的包并带上 eos | `day1_yuv2h264/guide/03_写代码_M1到M6.md` |

---

## 3. 解码骨架（Day 2：H.264 / H.265 → NV12）

```
① 开机        mpp_create(&ctx, &mpi)
              mpp_init(ctx, MPP_CTX_DEC, MPP_VIDEO_CodingAVC / HEVC)
              mpp_dec_cfg_init(&cfg) → control(MPP_DEC_GET_CFG, cfg)
              → set_u32(cfg, "base:split_parse", 1) → control(MPP_DEC_SET_CFG, cfg)

② 送码流      mpp_packet_init(&pkt, 数据, 长度)     [最后一块：mpp_packet_set_eos(pkt)]
              mpi->decode_put_packet(ctx, pkt)      → MPP_ERR_BUFFER_FULL：数据留着，等 1ms 原样重送
              mpp_packet_deinit(&pkt)

③ 取图像      mpi->decode_get_frame(ctx, &frm)      → nullptr：现在没有，回去送码流（非阻塞）
              ├─ info change？→ 建池子 / clear → limit_config(buf_size, 24)
              │                → SET_EXT_BUF_GROUP → SET_INFO_CHANGE_READY
              ├─ errinfo / discard？→ 跳过
              ├─ 有 buffer？→ mpp_buffer_get_ptr → 按 stride 逐行写 width × height
              └─ eos？→ 结束（单独判断，不放进上面的 if / else 链）
              mpp_frame_deinit(&frm)                ← 每一帧都要

④ 收拾        mpp_dec_cfg_deinit(cfg) → mpp_destroy(ctx) → mpp_buffer_group_put(grp)
```

| 关键点 | 为什么 | 文档 |
|---|---|---|
| 开 `split_parse` | 按固定 64KB 读文件、不按帧切，让 MPP 自己找帧边界 | `day2_h2642yuv/具体写day2_h2642yuv的流程.md` M1 |
| `get_frame` 拿到的不一定是图像 | 格式通知（info change）、错误帧、EOS 都借 MppFrame 带出来 | `day2_h2642yuv/doc/decode_get_frame拿到的几种帧.md` |
| `limit_config` 只设上限不分配 | count = DPB + 1 + 应用攥着的 + 余量；H.264/H.265 给 24 | `day2_h2642yuv/doc/mpp_buffer_group_limit_config.md` |
| 每帧都要 deinit | 不还帧，24 块被攥光，解码器**卡住不报错** | 同上 |
| 第二次 info change 用 `clear` | 码流中途换分辨率：池子复用，旧尺寸 buffer 用完销毁 | 同上 |
| 内存池最后释放 | 解码器销毁前，参考帧还在用池子里的 buffer | 同上 |

---

## 4. 编码 vs 解码：对称表

| | 编码（Day 1） | 解码（Day 2） |
|---|---|---|
| `mpp_init` 第二个参数 | `MPP_CTX_ENC` | `MPP_CTX_DEC` |
| 要设的参数 | 一大堆：宽高 stride 格式、码率、帧率、GOP、profile | 基本只有 `base:split_parse` |
| 送 | `encode_put_frame(frame)`：**图像** | `decode_put_packet(packet)`：**码流** |
| 取 | `encode_get_packet(&packet)`：**码流** | `decode_get_frame(&frame)`：**图像** |
| 输出超时 | 设成阻塞（`MPP_SET_OUTPUT_TIMEOUT`），get 会一直等 | 默认非阻塞，get 没有就返回 nullptr |
| 特殊事件 | 开头先取 SPS/PPS（`MPP_ENC_GET_HDR_SYNC`） | info change（要建内存池） |
| 内存池给谁用 | 编码器的**输入**：自己按 stride 申请，往里填 NV12 | 解码器的**输出**：info change 时按 `buf_size` 建，解码器往里写 |
| stride 谁决定 | 自己算（按 16 对齐），告诉编码器 | 解码器告诉你（info change 帧里） |
| 结束 | 送带 EOS 的空 frame → 取到带 EOS 的 packet | 送带 EOS 的 packet → 取到带 EOS 的 frame |
| 输出要拷吗 | packet 是编码器的，按 `pos + length` 拷出来再 deinit | frame 的 buffer 是池子里的，写完文件马上 deinit 还回去 |

---

## 5. 数据长什么样

**NV12 一帧（1920x1080，stride 1920x1088）**

```
内存里（MppBuffer）                         文件里（按 width × height 写）
┌──────────── 1920 ────────────┐           Y：1080 行 × 1920 字节
│ Y  第 0 ～ 1079 行            │           UV：540 行 × 1920 字节（U V 交错）
│    第 1080 ～ 1087 行：填充   │  ← 不写    一帧 = 1920 × 1080 × 3/2 = 3,110,400 字节
├──────────────────────────────┤ ← UV 从 hor_stride × ver_stride 开始（第 1088 行）
│ UV 第 0 ～ 539 行（UVUV…）    │           不是从 width × height 开始！
└──────────────────────────────┘
```

**解码器的 `buf_size`**（info change 帧里给）：`hor_stride × ver_stride × 2` = 4,177,920。比 NV12 像素（× 3/2）多出的 1/3 是解码器存额外信息（运动矢量等）用的。

**MppPacket 的四个量**：`data / size` = 整个容器；`pos / length` = 有效数据从哪开始、有多长。**读码流永远用 `pos + length`**。

---

## 6. 常见坑

标了"Day 2 Mx"的是这次写代码时实际踩到的；标了"踩坑清单 / 文档"的是 Day 1 整理的常见问题。

| 写法 | 后果 | 出处 |
|---|---|---|
| `control(ctx, MPP_DEC_GET_CFG, &cfg)` | MPP 往 8 字节的栈变量 memcpy 几百字节 → `stack smashing detected` | Day 2 M1 |
| `mpp_dec_cfg_deinit(&cfg)` | 拿栈地址去 free → `free(): invalid size` | Day 2 M5 |
| `mpp_packet_set_eos(&pkt)` | 把栈变量当 packet 改，改坏栈 | Day 2 M3 |
| `decode_get_frame(mpi, &frm)`（ctx、api 传反） | `-2`（ctx 无效） | Day 2 M5 |
| `unique_ptr` 包了 `mpi` 而不是 `ctx` | `mpp_init` 失败；析构时 `mpp_destroy(mpi)` 未定义行为 | Day 2 M1 |
| 先 `CFG_SET`，忘了 `SET_CFG` | 参数全部没生效，不报错 | Day 1 踩坑清单（`guide/05`） |
| `GET_HDR_SYNC` 前没把 length 清零 | 编码器以为容器已满，取不到头 | Day 1 踩坑清单（`guide/05`） |
| 用 `data + size` 读码流 | 读到容器里的垃圾 | `day1_yuv2h264/MppPacket和MppFrame的用法和区别.md` |
| `put_packet` 返回 Full 就去读下一块 | 丢数据，画面花 | Day 2 M2 |
| `get_frame` 后不判 nullptr 就用 | 内层循环空转、拿 NULL 调一堆函数 | Day 2 M3 |
| 不看 `info_change` 直接当图像处理 | 第一帧没有 buffer；没回 `READY`，解码器停住 | Day 2 M2 |
| 用 `get_external` 建池子却不 commit | 解码器没有内存可写，一帧都出不来 | Day 2 M2 |
| 拿了帧不 `deinit` / `limit_config` 的 count 太小 | 解码器卡住、**不报错**（两者现象一模一样） | Day 2 limit 实验 |
| `limit_config` 的 size 小于 `buf_size` | 一帧都解不出来 | Day 2 limit 实验 |
| 每次 info change 都 `get_internal` | 换分辨率时旧池子泄漏 | Day 2 M4 |
| UV 从 `width × height` 开始读 / 写 | 颜色不对、底部绿条 | Day 1 踩坑清单（`guide/05`） |
| CPU 写完输入 buffer 没 `mpp_buffer_sync_end` | 花屏、有上一帧的残影（硬件读到缓存刷新前的旧数据） | Day 1 踩坑清单（`guide/05`） |
| 循环结束条件用 `inputEos`（不是 `outputEos`） | 最后几帧丢了 | Day 2 流程文档 M4 |
| `goto CLEANUP` 跨过带初始化的变量声明 | 编译错误 `jump to label crosses initialization` | Day 2 M2 |

---

## 7. 板子上常用命令（都走 USB / adb）

```bash
# 运行（编译 → push → 运行，参数原样传给程序）
./run_on_board_with_mac.sh -n 60 -o /userdata/av/out.nv12

# 和官方程序逐字节对比（最可靠的验证）
adb shell "cd /userdata/av && LD_LIBRARY_PATH=/userdata/mpp_build/lib \
           /userdata/mpp_build/bin/mpi_dec_test -i aaa.264 -t 7 -n 60 -o ref.yuv; md5sum ref.yuv out.nv12"
#   -t：7 = H.264，16777220 = H.265；编码对比用 mpi_enc_test

# 拉回 Mac 看画面
./run_get_file_from_board_via_usb.sh /userdata/av/out.nv12
ffplay -f rawvideo -pixel_format nv12 -video_size 1920x1080 -framerate 30 result_from_board/out.nv12

# 看码流头（level、参考帧数、DPB 大小……）
ffmpeg -i aaa.264 -c copy -bsf:v trace_headers -frames:v 1 -f null - 2>&1 \
  | grep -E "level_idc|max_num_ref_frames|max_dec_frame_buffering"

# 板子内存、DMA heap
adb shell "grep -E 'MemAvailable|CmaTotal' /proc/meminfo; ls /dev/dma_heap/"
```

**实测基准**（`aaa.264`，1920x1080 Baseline，1803 帧）：不写文件 ≈ 900 fps；前 60 帧 NV12 的 md5 = `5aea639a616d92bb8ef8af809eee5218`。

---

## 8. 文档索引

| 想查 | 看哪里 |
|---|---|
| MPP 核心数据类型的细节 | `day1_yuv2h264/guide/07_MPP核心数据类型.md` |
| Day 1 用到的每个 MPP 函数 | `day1_yuv2h264/guide/06_本项目中用的mpp_函数.md` |
| stride / SPS PPS / GOP / DRM 缓冲区 | `day1_yuv2h264/doc/` 下同名文档 |
| `void*` 转 `uint8_t*`、`unique_ptr` 删除器 | `day1_yuv2h264/doc/` |
| 解码 M1～M6 每一步 | `day2_h2642yuv/具体写day2_h2642yuv的流程.md` |
| 解码的几种帧 | `day2_h2642yuv/doc/decode_get_frame拿到的几种帧.md` |
| 内存池和 count 怎么配 | `day2_h2642yuv/doc/mpp_buffer_group_limit_config.md` |
| 解码骨架默写练习 | `day2_h2642yuv/doc/MPP解码流程骨架.md` |
| 重构成类 | `day2_h2642yuv/guide/03_重构成类.md` |
| 板子连接、调试 | `day1_yuv2h264/doc/用usb连接并调试板子.md`、`day2_h2642yuv/guide/01_环境准备.md` |
