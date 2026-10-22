# day3_camera_cache_test：摄像头的 buffer 是 cached 还是 uncached？

> M6 里有一个猜测："摄像头的 buffer 是 uncached 的，CPU 用 memcpy 读它会特别慢"。这个探针用来验证它。
> 结论（2026-10-23 实测）：**猜错了，摄像头的 buffer 是 cached 的**，CPU 读它和读普通内存一样快。

## 1. cached 和 uncached

CPU 旁边有一块很小但很快的存储，叫缓存（cache）。
- **cached 内存**：CPU 读的时候，会顺便把附近的数据搬进缓存，下次读附近的数据直接从缓存拿，快。
- **uncached 内存**：CPU 每次都直接读内存，不经过缓存，慢。好处是硬件写进内存的新数据，CPU 一定能马上看到，不用做缓存同步。

打个比方：内存是图书馆书库，缓存是你的书桌。cached = 借书时顺手把旁边几本也抱回书桌；uncached = 每次都亲自跑一趟书库，一次只拿一本。

## 2. 怎么测

同样读 3,110,400 字节（一帧 1080p NV12），只换"从哪块内存读"：

| # | 内存 | 作用 |
|:---:|---|---|
| 1 | `malloc` 的普通内存 | 一定是 cached，当"快"的标准 |
| 2 | dma_heap `/dev/dma_heap/system` | cached 的 dma_buf，和 M6 里 MPP 申请的 `g_copyBuf` 是同一种（M6 实测它来自 system 堆） |
| 3 | dma_heap `/dev/dma_heap/system-uncached` | 一定是 uncached，当"慢"的标准 |
| 4 | 摄像头的 buffer（`/dev/video22` mmap 出来的地址） | **要测的** |

每块内存测两种读法，各重复 30 次取平均：
- **memcpy**：整块拷到 malloc 内存（M6 拷贝版本的读法）
- **逐 8 字节读求和**：模拟 CPU 自己一个一个处理像素（以后做 OSD、图像分析时的读法）

摄像头那块：STREAMON 后跳过 10 帧，第 11 帧 DQBUF 出来**先不还**，测完再 QBUF，这样测试期间摄像头不会往里写。

两轮测试，第二轮倒过来测：排除"第一个测的吃亏"（CPU 刚开始干活时频率还低）。

## 3. 怎么运行

```bash
cd /Users/dev/Documents/AV/rk_work/day3_camera_cache_test
./run_on_board_with_mac.sh
```

和 day3_cam2h264 一样：交叉编译（`cmake-build-rk3588-debug/`）→ `adb push` → 板子上运行。运行前确认没有别的程序占着 `/dev/video22`（相机 App、day3_cam2h264）。

## 4. 结果（2026-10-23）

```
read 3110400 bytes, average of 30 runs:
round 1:
  1 malloc (cached, reference)       memcpy   1.23 ms ( 2.53 GB/s)   read-sum    0.89 ms ( 3.50 GB/s)
  2 dma_heap system (cached)         memcpy   0.45 ms ( 6.91 GB/s)   read-sum    0.46 ms ( 6.69 GB/s)
  3 dma_heap system-uncached         memcpy   3.02 ms ( 1.03 GB/s)   read-sum    5.30 ms ( 0.59 GB/s)
  4 camera buffer (video22 mmap)     memcpy   0.32 ms ( 9.78 GB/s)   read-sum    0.29 ms (10.71 GB/s)
round 2 (reverse order):
  4 camera buffer (video22 mmap)     memcpy   0.35 ms ( 8.90 GB/s)   read-sum    0.29 ms (10.56 GB/s)
  3 dma_heap system-uncached         memcpy   3.07 ms ( 1.01 GB/s)   read-sum    5.30 ms ( 0.59 GB/s)
  2 dma_heap system (cached)         memcpy   0.31 ms ( 9.93 GB/s)   read-sum    0.30 ms (10.53 GB/s)
  1 malloc (cached, reference)       memcpy   0.35 ms ( 8.94 GB/s)   read-sum    0.30 ms (10.45 GB/s)
```

| 看到的 | 说明 |
|---|---|
| 第二轮里 1、2、4 都是 0.3 ms 左右，3 是 3～5 ms | **摄像头的 buffer 和 cached 内存一样快**，比 uncached 快 9～18 倍 → 是 cached 的 |
| uncached 的"逐 8 字节读"（5.30 ms）比 memcpy（3.07 ms）还慢 | uncached 内存最怕"一点一点地读"：每次都要跑一趟内存。CPU 要逐像素处理画面时，千万别直接在 uncached 内存上做 |
| 第一轮 malloc 比第二轮慢 3 倍 | 第一个测的吃亏：CPU 频率还没升上去。所以要测两轮、倒序 |

## 5. 和 M6 的 2.12 ms 怎么对上

探针里拷一帧只要 0.35 ms，M6 拷贝版本却要 2.12 ms，原因（按原理推断，未单独测量）：
- 探针把**同一块**内存反复读 30 次，数据大多已经在 CPU 缓存里（RK3588 的 L3 缓存 3 MB，一帧刚好放得下）
- M6 每一帧都是 ISP **刚写进内存的新画面**，缓存里没有，必须真的从内存读；还要写 `g_copyBuf`，再 `sync_end` 把 3 MB 缓存刷回内存

所以探针说明的是"这块内存是不是 cached"，而"拷一帧新画面要多久"要看 M6 的 2.12 ms。

## 6. 对以后写代码的意义

- 摄像头的 buffer 是 cached 的：以后 CPU 要直接读画面（叠加水印、简单分析），可以直接在 `mmap` 出来的地址上读，不用先拷出来
- 但 CPU **写**了摄像头的 buffer，再交给硬件（编码器）之前，要记得做缓存同步，道理和 M6 的 `sync_end` 一样
- 遇到 uncached 内存（比如 MPP 内部的 buffer，M3 时 bufinfo 里看到的 `system-uncached`），不要让 CPU 逐像素处理，先整块拷到 cached 内存再处理，或者交给 RGA（Day 4）
