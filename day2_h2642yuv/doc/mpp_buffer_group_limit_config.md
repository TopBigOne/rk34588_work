# mpp_buffer_group_limit_config：给解码器的内存池设上限

> 一句话：`mpp_buffer_group_limit_config(group, size, count)` **不分配任何内存**，它只是给内存池定两条规矩——**每块最大 `size` 字节、最多 `count` 块**。解码器按需一块一块地建 buffer、用完回收复用；建满 `count` 块、又没有空闲的，解码器就**停下来等你还帧**。

我们代码里的这一句（`src/mpi_dec_most_light.cpp`，info change 分支里）：

```cpp
mpp_buffer_group_get_internal(&bufferGroup, MPP_BUFFER_TYPE_DRM);   // 建一个"内部分配"的空池子
mpp_buffer_group_limit_config(bufferGroup, bufSize, 24);            // 每块最大 4,177,920 字节，最多 24 块
mpp_api->control(mpp_ctx, MPP_DEC_SET_EXT_BUF_GROUP, bufferGroup);  // 交给解码器用
```

---

## 1. 它解决什么问题：不加限制，解码器会"吃掉"所有内存

解码器每解一帧，都要一块 buffer 来装像素（1080p 一块 4MB）。这些 buffer 从哪来、最多能要多少，取决于**内存分配模式**。官方《MPP 开发参考》3.3.2 节（`rk_code/external/mpp/doc/Rockchip_Developer_Guide_MPP_CN.md` 第 455 行起）列了三种：

| 模式 | 怎么做 | 内存上限 | 我们用的？ |
|---|---|---|---|
| 一、纯内部分配 | 不调 `SET_EXT_BUF_GROUP`，info change 时只回 `SET_INFO_CHANGE_READY` | **没有上限** | |
| 二、**半内部分配** | 自己建 internal 池子 + `limit_config` + `SET_EXT_BUF_GROUP` | **由 `limit_config` 决定** | ✅ mpi_dec_test 的默认模式 |
| 三、纯外部分配 | 建空的 external 池子，把外部（显示系统）分配好的 dmabuf 一块块 `commit` 进去 | 你 commit 了多少就是多少 | |

模式一的缺点，官方原话：

> 无法控制解码器的内存使用量。解码器可以不受限制地使用内存，如果码流输入的速度很快，用户又没有及时释放内存，解码器会很快消耗掉全部的可用内存。

模式二就是为了解决这个问题：

> 用户可以通过mpp_buffer_group_limit_config接口来限制解码器的内存使用量。

**所以 `limit_config` 解决的问题是：给解码器的内存用量封顶。** 送码流的速度比你处理图像（写文件、显示、编码转发）的速度快时，解码器不会无限地往前解、无限地要内存，而是攒满 `count` 块就停下来，等你处理完、把帧还回来。这也是一种**流量控制（背压）**：下游慢，上游自动跟着慢。

---

## 2. 它内部做了什么（源码）

### 2.1 limit_config 本身：只记两个数

`rk_code/external/mpp/mpp/base/mpp_buffer.cpp` 第 438 行：

```cpp
MPP_RET mpp_buffer_group_limit_config(MppBufferGroup group, size_t size, RK_S32 count)
{
    ...
    MppBufferGroupImpl *p = (MppBufferGroupImpl *)group;
    mpp_assert(p->mode == MPP_BUFFER_INTERNAL);   // 只对 internal 池子有意义
    p->limit_size     = size;                     // 每块最大多少字节
    p->limit_count    = count;                    // 最多多少块
    return MPP_OK;
}
```

注意：**没有任何分配动作**。调完这一句，池子里还是 0 块 buffer，一个字节都没占。

### 2.2 解码器要 buffer 时：先复用，没有再新建

`mpp/base/mpp_buffer.cpp` 第 80–92 行（`mpp_buffer_get` 的实现）：

```cpp
// try unused buffer first
MppBufferImpl *buf = mpp_buffer_get_unused(p, size, caller);   // ① 先找池子里空闲的、够大的
if (NULL == buf && MPP_BUFFER_INTERNAL == p->mode) {
    ...
    // if failed try init a new buffer
    mpp_buffer_create(tag, caller, p, &info, &buf);            // ② 没有空闲的，才新建一块
}
```

所以 buffer 是**按需创建、用完回收**的：你 `mpp_frame_deinit` 一帧，这帧的 buffer 就回到池子的空闲列表，下一帧直接复用，不用重新分配。

### 2.3 新建时检查上限

`mpp/base/mpp_buffer_impl.cpp` 第 397–408 行（`mpp_buffer_create` 开头）：

```cpp
if (group->limit_count && group->buffer_count >= group->limit_count) {
    ...                                   // 已经建满 count 块了
    ret = MPP_NOK;                        // 不再新建
    goto RET;
}

if (group->limit_size && info->size > group->limit_size) {
    mpp_err_f("required size %d reach group size limit %d\n", info->size, group->limit_size);
    ret = MPP_NOK;                        // 要的这块比 size 上限还大，拒绝
    goto RET;
}
```

### 2.4 解码器开工前：没有空闲 buffer 就等

`mpp/codec/mpp_dec_normal.cpp` 第 580–586 行，解码器每解一帧之前先问一句"池子里还有空位吗"：

```cpp
RK_S32 unused = mpp_buffer_group_unused(mpp->mFrameGroup);

// NOTE: When dec post-process is enabled reserve 2 buffer for it.
task->wait.dec_pic_unusd = (dec->vproc) ? (unused < 3) : (unused < 1);
if (task->wait.dec_pic_unusd)
    return MPP_ERR_BUFFER_FULL;           // 没空位：这一帧先不解，等
```

`mpp_buffer_group_unused` 怎么算空位（`mpp/base/mpp_buffer.cpp` 第 383–399 行）：

```cpp
if (p->mode == MPP_BUFFER_INTERNAL) {
    if (p->limit_count)
        unused = p->limit_count - p->count_used;   // 设了上限：上限 - 正在被用的块数
    else
        unused = 3;                                // 没设上限：永远说"还有 3 个空位"
}
```

两种情况对比：

| | 设了 `limit_count = 24` | 没调 `limit_config` |
|---|---|---|
| 空位怎么算 | `24 - 正在用的块数` | 永远是 3 |
| 解码器什么时候停 | 24 块全被占着（参考帧 + 你还没 deinit 的帧） | **永远不停**，一直新建 |
| 内存最多用多少 | 24 × 4,177,920 = **100,270,080 字节（约 95.6 MB）** | 没有上限 |

### 2.5 "正在被用"的块，都是谁在用

一块 buffer 被算作"正在用"，是因为有人还持有它：

1. **解码器自己**：H.264 的 P 帧要参考前面的帧，这些参考帧（DPB，解码图像缓冲）必须留在内存里，不能复用。
2. **在路上的帧**：已经解好、在解码器输出队列里还没被你 `get_frame` 取走的帧。
3. **你手里的帧**：`get_frame` 拿到、还没 `mpp_frame_deinit` 的帧。

`count` 要够这三方加起来用，解码器才不会停。

---

## 3. 两个参数怎么填

### 3.1 size：填 info change 给的 `buf_size`

```cpp
size_t bufSize = mpp_frame_get_buf_size(outputFrame);   // aaa.264 实测 4,177,920 = 1920 × 1088 × 2
```

`buf_size` 是解码器按芯片和视频格式算好的"每块至少要多大"（像素 + 额外信息，见 [具体写day2_h2642yuv的流程.md](../具体写day2_h2642yuv的流程.md) M2）。直接用它，不要自己算。

### 3.2 count：H.264 / H.265 给 20 以上

官方 demo 的说明（`rk_code/external/mpp/utils/mpi_dec_utils.h` 第 79–85 行）：

```
 * The required buffer size caculation:
 * hor_stride * ver_stride * 3 / 2 for pixel data
 * hor_stride * ver_stride / 2 for extra info
 * Total hor_stride * ver_stride * 2 will be enough.
 *
 * For H.264/H.265 20+ buffers will be enough.
 * For other codec 10 buffers will be enough.
```

`mpi_dec_test` 用的就是 24（`dec_buf_mgr_setup(..., buf_size, 24, ...)`）。为什么要这么多：H.264 标准里参考帧最多 16 帧，再加上解码器正在写的、输出队列里排着的、你手里还没还的，20 多块才稳。

---

## 4. 风险

| 风险 | 什么时候发生 | 现象 | 怎么避免 |
|---|---|---|---|
| **count 给小了** | count 不够"参考帧 + 在路上的 + 你手里的" | 解码器一直等空位，`get_frame` 再也拿不到新帧，**程序看起来卡死**（不报错） | H.264/H.265 至少 20，照 demo 用 24 |
| **拿了帧不还**（最常见） | 漏写 `mpp_frame_deinit`，或者把帧存起来慢慢处理 | 同上：24 块被你攥光，解码器停住。**和 count 给小的现象一模一样** | 每个 `get_frame` 拿到的帧，处理完立刻 `mpp_frame_deinit`；要缓存就拷出来再还 |
| **size 给小了** | 自己算 size、算错了；或者换了分辨率还用旧的 size | 新建 buffer 被拒，日志里有 `required size ... reach group size limit ...`，解不出帧 | 永远用 info change 里的 `buf_size` |
| **换分辨率没重新配置** | 码流中途 info change（比如 720p → 1080p），没对池子重新 `limit_config` | 同上，新分辨率要的 size 比旧上限大 | 每次 info change：已有池子就 `mpp_buffer_group_clear` 再 `limit_config(新 buf_size, 24)` |
| **内存占用比想象的大** | 多路解码（NVR）时每路一个池子 | 每路最多约 96 MB（1080p），16 路就是 1.5 GB 的 DMA 内存 | 多路时根据实际需要调小 count，或者降低分辨率 |
| **上限"不准"** | 官方说的缺点："内存空间的限制并不准确，内存的使用量不是100%固定的，会有波动" | 实际用量在 0 到 count 块之间变化，不是一开始就占满 | 这是按需分配的正常现象，不是 bug；规划内存时按 count 块算最坏情况 |
| **对 external 池子调用** | `get_external` 建的池子 | `mpp_assert(p->mode == MPP_BUFFER_INTERNAL)` 断言；限制不起作用 | 只对 `get_internal` 的池子用 |

最要记住的一条：**"解码卡住、不报错"时，先查是不是有帧没 `mpp_frame_deinit`**。count 给小和帧不还，表现完全一样：解码器在等一个永远不会出现的空位。

---

## 5. 怎么自己验证（待板子上实测）

下面是按第 2 节源码推断的**预期结果**，还没在板子上跑过；跑完把实际数字补进表格。

改 `src/mpi_dec_most_light.cpp` 里 `limit_config` 的参数，每次只改一处，解完整个 `aaa.264`（不写文件）：

| 改法 | 预期（源码推断，未实测） | 实测 |
|---|---|---|
| `limit_config(bufSize, 24)`（现在的写法） | 1803 帧全部解完，正常走到 EOS | |
| 不调 `limit_config` | 也能解完；池子没有上限，峰值用量取决于解码和取帧的速度差 | |
| `limit_config(bufSize, 2)` | count 太小，可能在前几帧就卡住 | |
| `limit_config(bufSize / 2, 24)` | 新建 buffer 被拒，日志有 `reach group size limit`，解不出帧 | |
| 24 块，但前 30 帧拿到后**不 deinit** | 攥到第 20 多块时卡住，后面的帧出不来 | |

查看池子当前用了多少字节：`mpp_buffer_group_usage(bufferGroup)`，除以 `buf_size` 就是块数。

---

## 6. 对照我们的代码

| 写法 | 是否正确 | 说明 |
|---|---|---|
| `get_internal(&bufferGroup, MPP_BUFFER_TYPE_DRM)` | ✅ | 半内部模式必须是 internal 池子（M2 一开始误写成 `get_external`，那样 limit 不起作用，解码器也没有 buffer 可用） |
| `limit_config(bufferGroup, bufSize, 24)` | ✅ | size 用 info change 的 `buf_size`，count 照 demo 用 24 |
| `SET_EXT_BUF_GROUP` 在 `limit_config` 之后 | ✅ | 先定规矩再交给解码器 |
| 每帧处理完 `mpp_frame_deinit` | ✅ | buffer 回到池子复用，24 块不会被攥光 |
| 第二次 info change 时 | ⚠️ M4 待补 | 现在会再 `get_internal` 一个新池子（旧的泄漏）。应该：已有池子就 `mpp_buffer_group_clear` 再 `limit_config` |
| `mpp_buffer_group_put` 在 `mpp_destroy` 之后 | ✅ | 解码器销毁前，参考帧还在用池子里的 buffer |

相关文档：

- info change 帧本身：[decode_get_frame拿到的几种帧.md](decode_get_frame拿到的几种帧.md)
- DRM / DMA-HEAP 缓冲区是什么：[../../day1_yuv2h264/doc/DRM缓冲区.md](../../day1_yuv2h264/doc/DRM缓冲区.md)
