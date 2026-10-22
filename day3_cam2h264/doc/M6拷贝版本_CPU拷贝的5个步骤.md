# M6 拷贝版本：CPU 拷贝的 5 个步骤

> M6 要回答一个问题：**零拷贝到底省了多少 CPU？** 做法是写一个"拷贝版本"，让 CPU 先把每一帧拷到另一块内存，再交给编码器，然后和零拷贝版本比较 CPU 占用。
> 本文把拷贝版本要写的 5 处代码（①～⑤）一步一步讲清楚：写什么、为什么这样写、写错了会怎样。
> 代码在 `src/main.cpp`，用常量 `kUseZeroCopy` 切换两个版本（方案 A：两个版本共用其余 95% 的代码，比较才公平）。
> 实测数据都是 2026-10-22 在板子上得到的（IMX415 接在 MIPI CSI3，`/dev/video22`）。

---

## 0. 先看全局：两个版本差在哪

```
零拷贝版本（kUseZeroCopy = true）：
    DQBUF ──► encode(摄像头的 buffer) ──► QBUF
              编码器直接读摄像头那块内存，CPU 不碰画面

拷贝版本（kUseZeroCopy = false）：
    DQBUF ──► sync_begin ──► memcpy 3,110,400 字节 ──► sync_end ──► QBUF ──► encode(g_copyBuf)
              └──────────── CPU 搬一整帧 ────────────┘            拷完就还   编码器读副本
```

**打个比方**：摄像头拍好的一帧画面是一份文件。
- 零拷贝：把**原件**直接交给编码器看，看完还给摄像头。
- 拷贝：CPU 先**复印**一份，原件马上还给摄像头，编码器看复印件。复印要花时间和力气，这就是 CPU 的开销。

1080p30 时，CPU 每秒要复印 3,110,400 × 30 ≈ **93 MB**。

5 处代码在程序里的位置：

| # | 做什么 | 放在哪 | main.cpp 现在的行号 |
|:---:|---|---|---|
| ① | 开关常量 `kUseZeroCopy` | 文件开头的常量区 | 第 26～28 行 |
| ② | 两个全局变量：内存池 `g_copyBufGroup`、副本 `g_copyBuf` | 全局变量区 | 第 43～44 行 |
| ③ | 申请副本内存（只有拷贝版本才申请） | `encoder.init()` 之后 | 第 327～339 行 |
| ④ | 主循环里分成两条路：零拷贝 / 拷贝 | 主循环第 2、3 步 | 第 379～420 行 |
| ⑤ | 释放副本内存 | `release_resources()` | 第 476～481 行 |

> 下面每一步给的是**按 rk_work/代码规范.md 写的推荐代码**。现在 main.cpp 里有 4 处名字和推荐写法不一样，见第 7 节。

---

## ① 开关常量：kUseZeroCopy

```cpp
// true  = 零拷贝（编码器直接读摄像头的 buffer）
// false = 拷贝版本（CPU 先 memcpy 到 g_copyBuf，编码器读 g_copyBuf）
static const bool kUseZeroCopy = false;
```

**为什么用一个常量，而不是复制一份 main.cpp**：比较的只是"编码器读谁"这一步。其余代码（打开摄像头、设格式、申请 buffer、写文件……）两个版本应该**完全一样**，不然测出来的差别可能是别的代码造成的。用一个开关，就能保证只有这一处不同。

**为什么要加 `const`**：名字以 `k` 开头，表示它是常量（代码规范 2）。不加 `const`，编译器不会阻止你在别处不小心写 `kUseZeroCopy = true;`。

**怎么切换**：改这一个值，重新编译，再运行。

---

## ② 两个全局变量

```cpp
static MppBufferGroup g_copyBufGroup = nullptr;   // 拷贝版本用的内存池（副本从这里分出来）
static MppBuffer g_copyBuf           = nullptr;   // 拷贝版本：CPU 把画面拷到这里，编码器读这里
```

**MppBufferGroup 和 MppBuffer 是什么关系**：
- `MppBufferGroup` 是一个**内存池**，规定了这里分出来的内存是什么类型（DRM、CACHABLE……）。
- `MppBuffer` 是从池子里**分出来的一块**。
- 打个比方：group 是一个仓库，buffer 是仓库里的一个货架。要先有仓库，才能分货架。

**为什么放在全局**：和 `g_bufs`、`g_videoFd` 一样。`run()` 里任何一步失败都会 `return -1`，然后由 `release_resources()` 统一释放。变量放在全局，`release_resources()` 才找得到它们。

**为什么初始值是 `nullptr`**：`release_resources()` 靠"是不是 `nullptr`"判断要不要释放。零拷贝版本根本不申请副本，它们一直是 `nullptr`，释放时就会跳过。

---

## ③ 申请副本内存

放在 `encoder.init()` 之后，**只有拷贝版本才执行**：

```cpp
if (!kUseZeroCopy) {
    // 第 1 步：建内存池。类型 DRM，加上 CACHABLE（CPU 读写走缓存，快）
    MPP_RET mppRet = mpp_buffer_group_get_internal(&g_copyBufGroup,
                                                   MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE);
    if (mppRet != MPP_OK) {
        LOGE(kTag, "      mpp_buffer_group_get_internal failed: %d", mppRet);
        return -1;
    }

    // 第 2 步：从内存池里分一块，大小 = 一帧 NV12 = sizeImage（3,110,400 字节）
    mppRet = mpp_buffer_get(g_copyBufGroup, &g_copyBuf, sizeImage);
    if (mppRet != MPP_OK) {
        LOGE(kTag, "      mpp_buffer_get failed: %d", mppRet);
        return -1;
    }
}
```

这两步和 Day 1 读 YUV 文件时一模一样（`day1_yuv2h264/src/source/read_yuv.cpp` 第 41、48 行）。

### 为什么要 CACHABLE

CPU 要往这块内存里写 3 MB。CACHABLE 表示 CPU 读写会经过 CPU 缓存（cache），速度快得多。代价是写完要 `sync_end`，见第 ④ 步。原理见 Day 1 的 [DRM缓冲区.md](../../day1_yuv2h264/doc/DRM缓冲区.md) 第 4 节。

### 实测：这块内存到底是什么

拷贝版本运行时，看内核的 dma_buf 记录（方法见 [dma_buf_fd给了谁](dma_buf_fd给了谁_从EXPBUF到编码器.md) 第 4 节）：

```
size      ...  exp_name  ...
03112960  ...  system    ...  2972-day3_cam2h264
	Attached Devices:
	fdbd0000.rkvenc-core
```

| 看到的 | 说明 |
|---|---|
| 大小 3,112,960 | 要的是 3,110,400，内核按 4 KB 一页向上取整：760 页 × 4096 = 3,112,960 |
| exp_name 是 `system` | 来自 dma_heap 的 `system` 堆。**不是** `system-uncached`，说明 CACHABLE 生效了 |
| 挂着 `rkvenc-core` | 编码器读的是这块副本 |

> 代码里写的是 `MPP_BUFFER_TYPE_DRM`，实际却来自 dma_heap。原因见 `rk_code/external/mpp/inc/mpp_buffer.h` 第 124～125 行：内核 5.10 以上，MPP 优先用 DMA_HEAP，其次才是 DRM。对我们来说没有区别，都是一块能交给硬件的内存。

---

## ④ 主循环：分成两条路

主循环的第 1 步（DQBUF）和最后写文件、打日志都不变，只把中间"编码 + 还 buffer"分成两条路：

```cpp
if (kUseZeroCopy) {
    // ==== 零拷贝版本：和以前完全一样 ====
    if (!encoder.encode(g_bufs[filledBufIndex].mppBuf, false, packet)) {
        queue_buffer(filledBufIndex);
        return -1;
    }
    if (!queue_buffer(filledBufIndex)) {   // encode 返回之后才能还
        return -1;
    }
} else {
    // ==== 拷贝版本 ====
    // 第 1 步：CPU 要开始写 g_copyBuf 了
    mpp_buffer_sync_begin(g_copyBuf);

    // 第 2 步：CPU 把一整帧从摄像头的 buffer 拷到 g_copyBuf
    memcpy(mpp_buffer_get_ptr(g_copyBuf), g_bufs[filledBufIndex].start, sizeImage);

    // 第 3 步：CPU 写完了，把缓存里的数据刷到内存，编码器才看得到
    mpp_buffer_sync_end(g_copyBuf);

    // 第 4 步：⭐ 拷完马上把摄像头的 buffer 还回去
    if (!queue_buffer(filledBufIndex)) {
        return -1;
    }

    // 第 5 步：编码副本（失败时不用再 QBUF，第 4 步已经还过了）
    if (!encoder.encode(g_copyBuf, false, packet)) {
        return -1;
    }
}
```

下面一步一步讲。

### 第 1 步和第 3 步：sync_begin / sync_end

**先说人话**：CPU 写内存时，数据先写进 CPU 自己的缓存，不一定马上写到内存。编码器是另一块硬件，**只读内存，看不到 CPU 的缓存**。`sync_end` 就是让 CPU "把缓存里的东西全部写回内存"。

**打个比方**：你在草稿纸（缓存）上抄好了一份文件，但还没誊到正式的本子（内存）上。编码器只看正式的本子。`sync_end` = 誊写到本子上。

```
CPU ──写──► CPU 缓存 ──（sync_end 时才保证写回）──► 内存 ◄──读── 编码器
```

**为什么 begin 和 end 要成对**：`begin` 告诉内核"CPU 要开始访问这块内存了"，`end` 告诉内核"CPU 访问完了"。内核在这两个时刻做必要的缓存处理。两个都要写，把 CPU 的写操作包在中间。

**源码里它们做了什么**：
- `mpp_buffer_sync_begin` / `sync_end`（`rk_code/external/mpp/mpp/base/mpp_buffer.cpp` 第 302～324 行）先检查这块内存是不是 uncached（第 293～297 行：**uncached 的内存直接返回，什么都不做**）
- 是 cached 的，就调用 `mpp_dmabuf_sync_begin` / `sync_end`（`osal/mpp_dmabuf.cpp` 第 33、49 行），也就是对 dma_buf fd 调 `ioctl(fd, DMA_BUF_IOCTL_SYNC, ...)`，标志分别是 `DMA_BUF_SYNC_START` 和 `DMA_BUF_SYNC_END`，由内核去刷缓存

**忘了 sync_end 会怎样**：编码器可能读到内存里的旧数据，画面出现花屏、残影、色块，而且时有时无，很难查（Day 1 DRM缓冲区.md 第 4 节）。

> 为什么零拷贝版本不用 sync？因为 CPU 根本没写那块内存，是 ISP（硬件）写、编码器（硬件）读，两个硬件都直接读写内存，不经过 CPU 缓存。

### 第 2 步：memcpy

```cpp
memcpy(目的地, 来源, 大小);
memcpy(mpp_buffer_get_ptr(g_copyBuf), g_bufs[filledBufIndex].start, sizeImage);
```

| 参数 | 是什么 | 从哪来 |
|---|---|---|
| 目的地 | `mpp_buffer_get_ptr(g_copyBuf)` | 副本在本程序里的 CPU 地址。第一次调用时 MPP 会自动帮你做 mmap（`mpp/base/mpp_buffer.cpp` 第 175～176 行：`info.ptr` 为空就先 `mpp_buffer_mmap`） |
| 来源 | `g_bufs[filledBufIndex].start` | 摄像头 buffer 的 CPU 地址，M2 时 `mmap` 得到的。**零拷贝版本一直没用到它，拷贝版本终于用上了** |
| 大小 | `sizeImage` = 3,110,400 | G_FMT 返回的一帧实际大小。**不是** buffer 长度 3,133,440 |

**为什么一次 memcpy 就够，不用像 Day 1 那样逐行拷**：编码器的 stride 和摄像头的 stride 一样，都是 1920 × 1080。两边内存里的排布完全相同：第 0 字节到第 2,073,599 字节是 Y，后面是 UV。所以整块搬过去就行。

> 如果两边 stride 不一样（比如编码器要 1088 行），就要逐行拷：Y 拷 1080 行，然后跳到编码器的 UV 起点，再拷 540 行 UV。Day 1 的 `read_nv12_rows` 就是这么做的。

### 第 4 步：⭐ QBUF 提前

这是拷贝版本和零拷贝版本**最本质的区别**。

| | 什么时候 QBUF | 为什么 |
|---|---|---|
| 零拷贝 | **encode 返回之后** | 编码器读的就是摄像头那块内存。提前还，摄像头可能一边写下一帧、编码器一边读，画面撕裂 |
| 拷贝 | **memcpy 完成之后，encode 之前** | 画面已经复印到 `g_copyBuf` 了。原件马上还给摄像头，摄像头再写什么都不影响复印件 |

这说明 **"什么时候能还 buffer" 取决于 "谁还在用这块内存"**。零拷贝时编码器在用，所以要等；拷贝时只有 CPU 用了一下，拷完就没人用了。

### 第 5 步：encode(g_copyBuf)

**失败时为什么不用 QBUF**：第 4 步已经把摄像头的 buffer 还了。同一块再 QBUF 一次，驱动会报 `EINVAL`（2026-10-21 用探针实测：同一块重复 QBUF → `Invalid argument`）。

**为什么一块 `g_copyBuf` 就够，不用 4 块**：`encode()` 是阻塞的（`mpp_encoder.cpp` 第 156～158 行注释），返回时编码器已经读完了这一帧。下一轮再往 `g_copyBuf` 里写，不会和编码器抢。

> 如果以后改成多线程（一个线程拷、一个线程编），编码器读的时候 CPU 可能已经在写下一帧了，那时就要多块副本轮流用。Day 5 再说。

---

## ⑤ 释放副本内存

在 `release_resources()` 里，**STREAMOFF 和摄像头 buffer 都释放之后**：

```cpp
if (g_copyBuf != nullptr) {
    mpp_buffer_put(g_copyBuf);           // 先还货架
    g_copyBuf = nullptr;
}
if (g_copyBufGroup != nullptr) {
    mpp_buffer_group_put(g_copyBufGroup); // 再拆仓库
    g_copyBufGroup = nullptr;
}
```

**为什么先 put buffer，再 put group**：buffer 是从 group 里分出来的。顺序和申请相反：先申请的 group 后释放，后申请的 buffer 先释放。

**为什么释放后要置 `nullptr`**：和 `cameraBuf.mppBuf = nullptr` 一样。万一以后 `release_resources()` 被调用两次，第二次看到 `nullptr` 就会跳过，不会重复释放同一块内存（重复释放可能直接崩溃）。

**零拷贝版本会不会出问题**：不会。零拷贝版本没申请，两个变量一直是 `nullptr`，两个 `if` 都不进去。

---

## 6. buffer 在谁手里：两个版本的时间线

一帧的生命周期，看摄像头那块 buffer（`g_bufs[filledBufIndex]`）在谁手里：

```
零拷贝：
  驱动 ──DQBUF──► 程序手里 ─────── 编码器在读（约几毫秒）────────► QBUF ──► 驱动
                  └──────────── 这段时间摄像头不能用这块 ────────────┘

拷贝：
  驱动 ──DQBUF──► 程序手里 ── CPU 在拷 ──► QBUF ──► 驱动
                  └─ 这段时间摄像头不能用这块 ─┘
                                         然后编码器去读 g_copyBuf（和摄像头的 buffer 无关）
```

拷贝版本里，摄像头的 buffer 在程序手里的时间 = memcpy 的时间。如果 memcpy 比编码快，buffer 还得更早；反过来，如果 memcpy 很慢，就会拖住摄像头。到底哪个快，要实测（第 8 节）。

---

## 7. 现在 main.cpp 和推荐写法的差别

2026-10-22 review 用户写的 ①②③⑤：逻辑都对，以下 4 处名字或写法和代码规范不一致：

| 位置 | 现在 | 推荐 | 原因（代码规范） |
|---|---|---|---|
| ① | `static bool kUseZeroCopy` | `static const bool kUseZeroCopy` | `k` 开头是常量，要加 `const`（第 2 节） |
| 常量区 | `totalRecordFrameCount` | `kTotalRecordFrameCount` | 常量要以 `k` 开头（第 2 节） |
| ③ | `mmp_buf_result` | `mppRet` | 局部变量用 camelCase；`mmp` 是笔误（第 2 节） |
| ⑤ | `mpp_buffer_put` 之后没有置 `nullptr` | 加上 `g_copyBuf = nullptr;`、`g_copyBufGroup = nullptr;` | 防止重复释放，和其他释放的写法统一 |

---

## 8. 实测结果

### 8.1 拷贝版本能不能用（2026-10-22）

| 检查 | 结果 |
|---|---|
| 编译 | 0 warning |
| 运行 | 500 帧，`Done: 500 frames, 8299787 bytes` |
| ffprobe | 1080 × 1920，500 帧，完整解码无错误 |
| 第 100 帧画面 | 方向正、颜色正常，没有花屏、绿条（说明 sync 和 memcpy 都对） |
| 帧率 | frame 0 → frame 480 用了 16.0 秒，仍然是 30fps，和零拷贝一样 |
| dma_buf 记录 | 副本 `g_copyBuf` 来自 `system` 堆（cached），挂着 `rkvenc-core`；**摄像头的 4 块 buffer 上没有挂任何设备**（零拷贝时挂的是 `rkvenc-core`）→ 编码器确实没碰摄像头的 buffer |

### 8.2 CPU 占用对比（2026-10-22 ～ 23 实测，两个版本各 3 次）

**怎么测**：main.cpp 主循环里加了计时（`now_ms()` 用 `steady_clock`，`process_cpu_seconds()` 用 `getrusage`），循环结束打印 3 行 `[M6]`：

```
[M6] mode cpu-copy: 500 frames in 16.68 s -> 30.0 fps                       ← 哪个版本、实际帧率
[M6] avg per frame: dqbuf wait 26.51 ms, copy 1.82 ms, encode 4.53 ms        ← 平均每帧各步骤耗时
[M6] process CPU: 1.49 s in 16.68 s -> 9.0% (100% = one full core)          ← 主循环期间用掉的 CPU
```

改 `kUseZeroCopy`、重新编译、运行 `./run_on_board_with_mac.sh`，两个版本各跑 3 次：

| 每次 500 帧 | 拷贝 ① | 拷贝 ② | 拷贝 ③ | 零拷贝 ① | 零拷贝 ② | 零拷贝 ③ |
|---|---|---|---|---|---|---|
| fps | 30.0 | 30.0 | 30.0 | 30.0 | 30.0 | 30.0 |
| dqbuf wait（ms） | 26.10 | 25.91 | 26.51 | 27.69 | 27.70 | 27.87 |
| copy（ms） | 2.21 | 2.34 | 1.82 | 0.00 | 0.00 | 0.00 |
| encode（ms） | 4.52 | 4.55 | 4.53 | 4.94 | 4.93 | 4.84 |
| process CPU | 10.2% | 10.8% | 9.0% | 5.1% | 5.1% | 4.5% |

**平均：**

| | 拷贝版本 | 零拷贝版本 | 差别 |
|---|---|---|---|
| fps | 30.0 | 30.0 | 一样 |
| copy（ms/帧） | **2.12** | **0.00** | 零拷贝没有这一步 |
| encode（ms/帧） | 4.53 | 4.90 | 零拷贝慢 0.37 ms（见 8.4） |
| **process CPU** | **10.0%** | **4.9%** | **拷贝版本约是零拷贝的 2 倍** |

另外用 `adb shell top -b -d 1 -w 250 | grep day3` 看零拷贝版本运行时的实时 `%CPU`：每秒一个值，在 1.9%～5.9% 之间，大多是 3%～4%，和 `[M6]` 的结果一致。

> `top` 的两个坑（2026-10-22 实测）：① 在 `adb shell` 里直接跑交互式 `top`，屏幕会乱，要用 `-b`（batch 模式）；② batch 模式每行默认截到 80 个字符，`run_on_board_with_mac.sh` 用完整路径 `/tmp/CLion/run/day3_cam2h264` 启动，名字被截成 `/tmp/CLion+`，`grep day3` 什么都找不到，要加 `-w 250`。

### 8.3 结论

**1. 拷贝多花的 CPU，基本就是 memcpy 本身**

```
每帧拷贝 2.12 ms × 每秒 30 帧 = 每秒 63.6 ms ≈ 一个核的 6.4%
实测两个版本差：10.0% − 4.9% = 5.1%
```
估算和实测在同一个量级。拷贝速度：3,110,400 字节 ÷ 2.12 ms ≈ **1.5 GB/秒**，每秒要搬 **93 MB**。

**2. 一路 1080p30 时，两个版本都轻松跑满 30fps**

每帧 33 ms 里，程序有 26～28 ms 在 DQBUF 那里等摄像头（等待时不占 CPU）。拷贝多花的 2 ms 远没有用完余量。

**3. 零拷贝的意义在规模变大时**

按实测的 1.5 GB/秒估算，只算 memcpy 的开销：

| 场景 | 每秒要拷 | 拷贝占用 |
|---|---|---|
| 1 路 1080p30（现在） | 93 MB | 约 6% 个核 |
| 1 路 4K30（像素 4 倍） | 373 MB | 约 25% 个核 |
| 1 路 4K60 / 8 路 1080p30（NVR） | 746 MB | 约 50% 个核 |

再加上：内存带宽翻倍（每个字节读一次、写一次）、每帧多 2 ms 延迟、多耗电。所以 4K、多路、低延迟、低功耗的产品里，零拷贝是必需的。

**4. "摄像头 buffer 是 uncached，memcpy 会特别慢"的猜测：没有得到证实**

1.5 GB/秒不算特别慢，一帧只占 2 ms。要确认到底 cached 还是 uncached，需要对照实验：在同一块板子上从一块确定是 cached 的普通内存拷 3 MB，比较速度（⬜ 还没做）。

### 8.4 一个还没解释的现象：零拷贝的 encode 慢 0.37 ms

零拷贝 3 次是 4.94、4.93、4.84 ms，拷贝版本 3 次是 4.52、4.55、4.53 ms。两边各自都很稳定，所以**不是测量波动**。

两个版本里，编码器读的是不同的内存：

| | 编码器读的内存 | 谁分配的 |
|---|---|---|
| 零拷贝 | 摄像头的 buffer | 摄像头驱动（`videobuf2_cma_sg`） |
| 拷贝 | `g_copyBuf` | MPP（dma_heap 的 `system` 堆） |

推测和内存的来源、分配方式有关（比如物理页是否连续、硬件访问它的映射方式不同），**未验证**。这 0.37 ms 主要是在等硬件，不怎么占 CPU，对 CPU 对比的结论没有影响。

---

## 9. 自测

- [ ] 拷贝版本和零拷贝版本，唯一的区别在哪一步？为什么用一个常量切换，而不是复制一份 main.cpp？
- [ ] `MppBufferGroup` 和 `MppBuffer` 是什么关系？释放时先放哪个？
- [ ] 为什么副本要用 CACHABLE？用了 CACHABLE 以后要多做什么？
- [ ] 忘了 `sync_end` 会怎样？为什么零拷贝版本不用 sync？
- [ ] memcpy 的三个参数分别是什么？大小为什么是 3,110,400 而不是 3,133,440？
- [ ] 为什么一次 memcpy 就够，不用逐行拷？什么情况下必须逐行拷？
- [ ] ⭐ 拷贝版本为什么可以在 encode 之前 QBUF？零拷贝版本为什么不行？
- [ ] 拷贝版本 encode 失败时，为什么不用再 QBUF？
- [ ] 为什么一块 `g_copyBuf` 就够？什么情况下要多块？
- [ ] 怎么在板子上证明"拷贝版本里编码器没有碰摄像头的 buffer"？
