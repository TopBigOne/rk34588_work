# MPP 解码流程骨架（速查 + 默写用）

> 不用背 API 的参数顺序。要记的是**流程骨架**（4 段），外加**一条规律**（什么时候传 `&`）。
> 记不住细节就回来查这一页；隔几天拿白纸默写一遍第 1 节，默写不出来的那一步，就是还没真正理解的地方。

---

## 1. 解码骨架：4 段

```
① 开机        mpp_create(&ctx, &mpi)
              mpp_init(ctx, MPP_CTX_DEC, 编码格式)
              mpp_dec_cfg_init(&cfg) → control(GET_CFG) → set_u32("base:split_parse", 1) → control(SET_CFG)

② 送码流      mpp_packet_init(&pkt, 数据, 长度)   [最后一块：mpp_packet_set_eos]
              mpi->decode_put_packet(ctx, pkt)    → 队列满就等 1ms 重送
              mpp_packet_deinit(&pkt)

③ 取图像      mpi->decode_get_frame(ctx, &frm)    → nullptr 就回去送码流
              ├─ info change？→ 建池子 / clear → limit_config → SET_EXT_BUF_GROUP → SET_INFO_CHANGE_READY
              ├─ errinfo / discard？→ 跳过
              ├─ 有 buffer？→ mpp_buffer_get_ptr → 按 stride 逐行写
              └─ eos？→ 结束
              mpp_frame_deinit(&frm)              ← 每一帧都要

④ 收拾        mpp_dec_cfg_deinit(cfg) → mpp_destroy(ctx) → mpp_buffer_group_put(grp)
```

每一段为什么这么写：

| 段 | 关键点 | 详细文档 |
|---|---|---|
| ① | 开 `split_parse`：我们按固定 64KB 读文件，不按帧切，让 MPP 自己找帧边界。配置一律"取出 → 改 → 写回" | [具体写day2_h2642yuv的流程.md](../具体写day2_h2642yuv的流程.md) M1 |
| ② | `put_packet` 返回 `MPP_ERR_BUFFER_FULL` 不是错误，是流量控制：**数据留着原样重送**，不能去读下一块 | 同上 M2、M4 |
| ③ | `get_frame` 拿到的**不一定是图像**：先判 nullptr，再判 info change → 错误帧 → 有没有 buffer；**eos 单独判断**（可能贴在最后一帧图像上，也可能是空帧） | [decode_get_frame拿到的几种帧.md](decode_get_frame拿到的几种帧.md) |
| ③ | info change 时建内存池：`limit_config(buf_size, 24)` 只设上限不分配；**每帧都要 deinit**，否则 24 块被攥光，解码器卡住不报错 | [mpp_buffer_group_limit_config.md](mpp_buffer_group_limit_config.md) |
| ④ | 内存池**最后**释放：解码器销毁前，参考帧还在用池子里的 buffer | 同上 |

---

## 2. 一条规律：什么时候传 `&`

> **要把新对象写回给你的函数，传 `&`**：`xxx_init(&h)`、`mpp_create(&ctx, …)`、`decode_get_frame(ctx, &frm)`
> **其他一律直接传句柄**：`control(ctx, …, cfg)`、`mpp_dec_cfg_set_u32(cfg, …)`、`mpp_destroy(ctx)`、`mpp_dec_cfg_deinit(cfg)`
> **例外**：`mpp_packet_deinit(&pkt)`、`mpp_frame_deinit(&frm)` 也传 `&`——它们要把你的变量置成 NULL

原因：`MppCtx`、`MppDecCfg`、`MppPacket`、`MppFrame`、`MppBufferGroup` 本身都是 `void*`（"门牌号"）。函数要**改你的变量**（写进新门牌号、或者清成 NULL）才需要变量的地址；只是**用这个对象**，给门牌号就够了。

这条规律挡住的坑（Day 2 全踩过，编译器都不报错，因为 `void**` 也能隐式转成 `void*`）：

| 写错的 | 后果 |
|---|---|
| `control(ctx, MPP_DEC_GET_CFG, &cfg)` | MPP 往 8 字节的栈变量里 memcpy 几百字节配置 → `stack smashing detected` |
| `mpp_dec_cfg_deinit(&cfg)` | 拿栈上的地址去 free → `free(): invalid size` |
| `mpp_packet_set_eos(&pkt)` | 把栈变量当成 packet 改 → 改坏 main 的栈 |
| `decode_get_frame(mpi, &frm)`（ctx 和 api 传反） | 返回 `-2`（`MPP_ERR_UNKNOW`）：MPP 检查 ctx 发现无效（`mpi.cpp` 第 91–96 行 `_check_mpp_ctx`，日志里有 `found invalid context`） |

---

## 3. 编码和解码是对称的

Day 1 的编码流程和解码几乎一样，只是 packet / frame 的方向反过来。记住一套，另一套对着改：

| | 解码（Day 2） | 编码（Day 1） |
|---|---|---|
| 开机 | `mpp_init(ctx, MPP_CTX_DEC, …)` + `split_parse` | `mpp_init(ctx, MPP_CTX_ENC, …)` + 宽高、码率、GOP 一堆参数 |
| 送 | `decode_put_packet(packet)`：码流 | `encode_put_frame(frame)`：图像 |
| 取 | `decode_get_frame(frame)`：图像 | `encode_get_packet(packet)`：码流 |
| 特殊事件 | info change（要建内存池） | 先取 SPS/PPS（`MPP_ENC_GET_HDR_SYNC`）写在文件开头 |
| 内存池 | 解码器的**输出**要池子（info change 时按 `buf_size` 建） | 编码器的**输入**要池子（按 stride 自己申请，往里填 NV12） |
| 结束 | 送带 EOS 的 packet，取到带 EOS 的 frame | 送带 EOS 的 frame，取到带 EOS 的 packet |

---

## 4. 骨架在代码里的位置

| 骨架 | 面条版（`day2_h2642yuv_most_light` 分支的 M1～M6） | 重构版（`day2_h2642yuv_most_light_refactor`） |
|---|---|---|
| ① 开机 | `main()` 开头 | `MppDecoder::init` |
| ② 送码流 | 主循环 step 1、2 | `ReadStream::read` + `MppDecoder::feed` + `DecodePipeline::run` |
| ③ 取图像 | 内层 `while (true)` | `MppDecoder::get_frame`（info change 在 `handle_info_change`）+ `DecodePipeline::drain_frames` + `WriteYuv::write` |
| ④ 收拾 | `CLEANUP:` | `~MppDecoder` + RAII 句柄（不用手写） |

---

## 5. 默写练习

不看任何资料，在白纸或空文件上写出：

1. 第 1 节的 4 段骨架（函数名写对就行，参数可以省略）
2. info change 之后的 4 步
3. 哪些函数传 `&`，哪些不传
4. `get_frame` 拿到一帧后，判断的顺序

写完对照本页。错的地方回到第 1 节表格里的"详细文档"再看一遍。
