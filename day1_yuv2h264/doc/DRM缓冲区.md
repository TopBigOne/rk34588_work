# DRM 缓冲区：硬件能直接 DMA 访问的内存

> 出处：[src/source/read_yuv.cpp](../src/source/read_yuv.cpp) `ReadYUV::prepare` 里申请输入帧缓冲：
> ```cpp
> CHECK(mpp_buffer_group_get_internal(&rawGroup, MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE));
> CHECK(mpp_buffer_get(drmBufferGroup_.get(), &rawBuffer, mppFrameBufSize));
> ```
> 相关文档：[stride.md](stride.md)（缓冲区多大）、[guide/07_MPP核心数据类型.md](../guide/07_MPP核心数据类型.md) 第 8、9 节（MppBufferGroup / MppBuffer）

---

## 1. 一句话

**DRM 缓冲区 = 硬件（编码器、RGA、显示）能直接通过 DMA 读写的内存。**

名字里的 DRM 是 Linux 内核里管显卡和显示的子系统 **Direct Rendering Manager**，和版权保护的 DRM（Digital Rights Management）没有关系。

> ⚠️ 在本项目的板子上（内核 5.10），代码里写的是 `DRM`，但 MPP 实际是从 **DMA-HEAP** 分配的 —— 见第 5 节的实测。"DRM 缓冲区"在这里只是个历史名称，真正的意思是"硬件能访问的内存"。

---

## 2. 为什么不能用 malloc

编码器是一块独立的硬件，它不经过 CPU，而是用 **DMA**（Direct Memory Access，直接内存访问）自己去内存里读图像。这对内存有要求：

| | `malloc` 的内存 | DRM / DMA 缓冲区 |
|---|---|---|
| 谁能访问 | 只有 CPU（通过虚拟地址） | CPU 和硬件都能 |
| 物理上 | 分散在各处的 4KB 页，硬件不知道在哪 | 内核专门为设备分配，并且映射好硬件能用的地址（物理连续的 CMA 内存，或者通过 IOMMU 映射的分散内存） |
| 能不能交给别的硬件 | 不能 | 能：导出成一个 **dma-buf fd**，摄像头、RGA、MPP、显示之间直接传，不用拷贝 |
| 怎么申请 | `malloc` / `new` | `mpp_buffer_group_get_internal` + `mpp_buffer_get` |
| 怎么释放 | `free` / `delete` | `mpp_buffer_put` + `mpp_buffer_group_put`（本项目交给 `MppBufferPtr` / `MppBufferGroupPtr` 自动释放） |

```
           CPU                                   编码器（VPU）
            │ 虚拟地址                               │ DMA 地址
            ▼                                       ▼
   ┌──────────────────┐                    ┌──────────────────┐
   │ malloc 的内存     │  ✗ 硬件找不到        │                  │
   └──────────────────┘                    │                  │
   ┌──────────────────────────────────────────────────────────┐
   │              DRM / DMA 缓冲区（同一块物理内存）            │  ✓ 两边都能访问
   └──────────────────────────────────────────────────────────┘
```

所以 readme 自测题"为什么输入缓冲区必须用 `mpp_buffer_get` 申请，不能用 `malloc`"的答案就是：**硬件只能读这种专门分配的内存**。

---

## 3. MPP 支持的几种内存类型

`rk_code/external/mpp/inc/mpp_buffer.h` 第 118～134 行：

| 类型 | 来源（设备文件） | 说明 |
|---|---|---|
| `MPP_BUFFER_TYPE_NORMAL` | malloc | 只给单元测试、软件模拟用，硬件访问不了 |
| `MPP_BUFFER_TYPE_ION` | `/dev/ion` | Android 早期的分配器，已被 DMA-HEAP 取代 |
| `MPP_BUFFER_TYPE_DRM` | `/dev/dri/card0` | DRM 子系统的内存分配接口（GEM / dumb buffer）。**本项目代码里写的是这个** |
| `MPP_BUFFER_TYPE_DMA_HEAP` | `/dev/dma_heap/*` | Linux 5.6 之后的新标准，专门用来给设备分配内存 |
| `MPP_BUFFER_TYPE_EXT_DMA` | 外部传进来的 dma-buf fd | 不由 MPP 分配，导入别人的缓冲区。Day 3 摄像头会用到 |

头文件里的原话：

```c
 * MPP default allocator select priority for kernel above 5.10:
 * MPP_BUFFER_TYPE_DMA_HEAP > MPP_BUFFER_TYPE_DRM > MPP_BUFFER_TYPE_ION
```

### 标志位（和类型用 `|` 组合）

| 标志 | 含义 |
|---|---|
| `MPP_BUFFER_FLAGS_CACHABLE` | CPU 读写走缓存（**本项目用了这个**） |
| `MPP_BUFFER_FLAGS_CONTIG` | 物理连续（CMA） |
| `MPP_BUFFER_FLAGS_DMA32` | 分配在 4GB 以下（给只能访问 32 位地址的硬件用） |
| `MPP_BUFFER_FLAGS_SECURE` | 安全内存（DRM 版权保护视频用，这里才是"数字版权"那个意思） |

---

## 4. CACHABLE 和 sync_begin / sync_end

### CPU 缓存带来的问题

```
CPU ──写──→ CPU 缓存（L1/L2/L3） ──（不一定马上）──→ 内存 ←──DMA 读── 编码器
                     ↑
           CPU 写的数据可能还停在这里
```

- **CACHABLE**：CPU 读写这块内存走缓存，速度快（逐行 `fread` 1620 次，每次都直接写内存会慢很多）。
- 但**硬件 DMA 读的是内存，不是 CPU 缓存**。CPU 刚写完的数据可能还没刷到内存。

所以 CPU 写完必须 `mpp_buffer_sync_end`，把缓存刷到内存：

```cpp
// src/source/read_yuv.cpp  ReadYUV::read_frame
mpp_buffer_sync_begin(frameBuffer);           // CPU 开始访问
const bool ok = read_nv12_rows(nv12FrameCpuAddr);
mpp_buffer_sync_end(frameBuffer);             // CPU 写完：把缓存刷到内存，硬件才看得到
```

| 不调 sync_end | 后果 |
|---|---|
| 硬件读到内存里的旧数据（上一帧或者垃圾） | 画面花屏、残影、颜色块错乱，而且时有时无，很难排查 |

### 不用 CACHABLE 行不行

可以（uncached），CPU 每次读写都直接访问内存，不需要 sync。但 CPU 访问会慢很多，适合"CPU 基本不碰、只有硬件读写"的缓冲区 —— 编码器内部的缓冲区就是这样（见第 5 节）。

---

## 5. 实测：板子上到底是谁分配的

### 5.1 MPP 的选择逻辑

`rk_code/external/mpp/osal/mpp_allocator.cpp` 第 170～175 行，申请 `DRM` 类型时：

```c
case MPP_BUFFER_TYPE_DRM : {
    p->os_api = (mpp_rt_allcator_is_valid(MPP_BUFFER_TYPE_DMA_HEAP)) ? allocator_dma_heap :   // ← 优先
                (mpp_rt_allcator_is_valid(MPP_BUFFER_TYPE_DRM)) ? allocator_drm :
                (mpp_rt_allcator_is_valid(MPP_BUFFER_TYPE_ION)) ? allocator_ion :
                allocator_std;
} break;
```

判断 DMA-HEAP 能不能用，只是看 `/dev/dma_heap` 在不在（`osal/mpp_runtime.cpp` 第 91 行）：

```c
allocator_valid[MPP_BUFFER_TYPE_DMA_HEAP] = !access("/dev/dma_heap", F_OK | R_OK);
```

DMA-HEAP 分配器再根据标志位挑具体的 heap（`osal/allocator/allocator_dma_heap.c` 第 77～84 行）：

| heap | CMA（物理连续） | CACHABLE | DMA32 |
|---|:---:|:---:|:---:|
| `system-uncached` | | | |
| `system-uncached-dma32` | | | ✓ |
| **`system`** | | **✓** | |
| `system-dma32` | | ✓ | ✓ |
| `cma-uncached` | ✓ | | |
| `cma` | ✓ | ✓ | |

`DRM | CACHABLE` → 不要求连续、要缓存 → **`system`**。

### 5.2 板子上有什么

```bash
adb shell "uname -r; ls /dev/dma_heap/ /dev/dri/"
```
```
5.10.160
/dev/dma_heap/:
cma  cma-uncached  system  system-dma32  system-uncached  system-uncached-dma32
/dev/dri/:
by-path  card0  card1  renderD128  renderD129
```

内核 5.10，`/dev/dma_heap` 存在 → MPP 会优先用 DMA-HEAP。

### 5.3 strace 跟踪

```bash
adb shell 'cd /tmp/CLion/run && strace -f -e trace=openat,ioctl -o /tmp/st.txt ./day1_yuv2h264 -o /userdata/av/x.h264 >/dev/null
           grep dma_heap /tmp/st.txt | grep openat
           grep DMA_HEAP_IOCTL_ALLOC /tmp/st.txt | awk "{print \$1, \$2}" | sort | uniq -c'
```

打开的设备（节选）：

```
openat("/dev/dma_heap/system-uncached", ...) = 3
openat("/dev/dma_heap/system-uncached-dma32", ...) = 4
openat("/dev/dma_heap/system", ...) = 5
openat("/dev/dma_heap/system-dma32", ...) = 6
openat("/dev/dma_heap/cma-uncached", ...) = 7
openat("/dev/dma_heap/cma", ...) = 8
```

分配次数（第一列是线程号）：

```
      1 3272 ioctl(5,      ← 主线程，fd 5 = /dev/dma_heap/system
      9 3273 ioctl(3,      ← 编码器线程，fd 3 = /dev/dma_heap/system-uncached
```

| 谁 | 分配几块 | 从哪个 heap | 是什么 |
|---|---|---|---|
| 主线程 | 1 | **`system`**（带缓存） | `ReadYUV::prepare` 申请的那块 3,133,440 字节的帧缓冲 |
| 编码器线程 | 9 | `system-uncached`（不带缓存） | 编码器内部自己用的缓冲（参考帧、码流输出等），只有硬件读写，不需要 CPU 缓存 |

整个运行过程**没有打开 `/dev/dri`**。

### 5.4 结论

- 代码写 `MPP_BUFFER_TYPE_DRM`，在这块板子上实际走的是 **DMA-HEAP 的 `system` heap**。
- 这不是 bug，是 MPP 故意设计的：上层只说"我要硬件能访问的内存"，具体用哪个内核接口由 MPP 按内核版本自动挑。同一份代码在老内核（只有 DRM 或 ION）上也能跑。
- 想直接指定，也可以写 `MPP_BUFFER_TYPE_DMA_HEAP | MPP_BUFFER_FLAGS_CACHABLE`，在这块板子上效果一样。

---

## 6. 在本项目里

| 位置 | 做什么 |
|---|---|
| `src/source/read_yuv.cpp` `ReadYUV::prepare` | 建内存池（`DRM \| CACHABLE`），申请一块 3,133,440 字节的帧缓冲，整个循环反复用这一块 |
| `src/source/read_yuv.cpp` `ReadYUV::read_frame` | `sync_begin` → 逐行 `fread` → `sync_end` |
| `src/encoder/mpp_encoder.cpp` `MppEncoder::encode` | `mpp_frame_set_buffer` 把这块缓冲挂到 MppFrame 上，编码器通过 DMA 读 |
| `src/source/read_yuv.h` 成员声明顺序 | 文件 → 内存池 → 缓冲区：析构时缓冲区先 put，再销毁内存池 |
| `src/common/mpp_utils.h` | `MppBufferGroupPtr` / `MppBufferPtr`：RAII 自动 put |

为什么"申请缓冲区"放在 `ReadYUV`（`FrameSource`）里，不放在编码器里：文件输入要自己申请这块内存；摄像头输入（Day 3）直接用摄像头驱动给的缓冲区，不需要申请。两种输入差别很大，所以由输入自己管。

---

## 7. 和后面几天的关系：零拷贝

这类内存都能导出成 **dma-buf fd**（一个文件描述符，代表"这块硬件内存"）：

```
Day 1（现在）：  NV12 文件 ──CPU fread──→ DMA 缓冲区 ──DMA──→ 编码器          （CPU 要搬一次数据）

Day 3：         摄像头 ──DMA──→ 摄像头的缓冲区 ──导出 dma-buf fd──→ MPP（EXT_DMA）──DMA──→ 编码器
                                                                                        （CPU 一个字节都不碰）
```

Day 3 时，摄像头驱动把画面直接写进它自己的 DMA 缓冲区，导出 fd，用 `MPP_BUFFER_TYPE_EXT_DMA` 交给 MPP。整个过程不需要 `fread`，也不需要 `sync`，这就是**零拷贝**。RGA（Day 4）缩放、格式转换也是同样的方式在硬件之间传递。

---

## 8. 自测

- [ ] 为什么输入缓冲区必须用 `mpp_buffer_get` 申请，不能用 `malloc`？
- [ ] DRM 缓冲区的 DRM 是什么的缩写？和数字版权保护有关系吗？
- [ ] `MPP_BUFFER_FLAGS_CACHABLE` 带来什么好处、什么代价？
- [ ] 不调 `mpp_buffer_sync_end` 会怎样？为什么？
- [ ] 代码里写 `MPP_BUFFER_TYPE_DRM`，在 5.10 内核的板子上实际是谁分配的？怎么验证？
- [ ] 编码器内部的缓冲区为什么用 uncached？
- [ ] dma-buf fd 是什么？Day 3 摄像头输入为什么能做到零拷贝？
