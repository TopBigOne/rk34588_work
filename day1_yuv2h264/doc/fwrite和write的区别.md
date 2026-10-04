# fwrite 和 write 的区别

> 出处：[src/sink/write_stream.cpp](../src/sink/write_stream.cpp) 里一行 `fwrite` 就写完一整帧码流。
> 相关文档：[fread和fwrite 的详细用法.md](fread和fwrite%20的详细用法.md)（fwrite 参数、返回值、缓冲的细节）、
> [void* 能转成任何对象指针.md](%20void*%20能转成任何对象指针.md) 第 6.2 节（为什么不用 for 循环）

---

## 1. 先纠正一个直觉：两个函数都是"从一个地址开始，连续写一大块内存"

```c
size_t  fwrite(const void* ptr, size_t size, size_t nmemb, FILE* fp);   // C 标准库
ssize_t write (int fd, const void* buf, size_t count);                  // Linux 系统调用（POSIX）
```

两个函数都**不是"写一个值"，而是"写一块内存"**：给它一个起始地址和长度，它就把这段连续的字节原样写出去，循环在它们内部完成，调用方不用自己遍历。

```cpp
std::vector<uint8_t> data = ...;            // 一帧码流，74674 字节，连续存放

fwrite(data.data(), 1, data.size(), fp);    // 一次写完 74674 字节
write(fd, data.data(), data.size());        // 一次写完 74674 字节（大多数情况下，见第 5 节）
```

```
data.data()                                 data.data() + data.size()
    ↓                                              ↓
    ┌────┬────┬────┬────┬────┬────────────────────┐
    │0x00│0x00│0x00│0x01│0x06│  …… 共 74674 字节  │ ──一次调用──→ 文件
    └────┴────┴────┴────┴────┴────────────────────┘
```

前提只有一个：**这段内存是连续的**。`std::vector`、数组、`malloc` 出来的内存、MPP buffer 都满足。不连续的（比如 NV12 缓冲区每行末尾有 stride 填充）才需要自己循环，一段一段地写。

既然都能一次写一大块，区别在哪？在**层次**和**缓冲**。

---

## 2. 一张表看区别

| | `fwrite` | `write` |
|---|---|---|
| 属于 | **C 标准库**（`<cstdio>` / `<stdio.h>`） | **Linux / POSIX 系统调用**（`<unistd.h>`） |
| 跨平台 | ✅ 任何有 C 编译器的地方都能用（Windows、Mac、Linux、单片机） | 只在 POSIX 系统（Linux、Mac）上有，Windows 要用别的 API |
| 文件句柄 | `FILE*`（`fopen` 返回） | `int fd`（文件描述符，`open` 返回） |
| 缓冲 | ✅ **有用户态缓冲区**：先攒在内存里，攒满一块再交给内核 | ❌ **没有**：每次调用都直接进内核 |
| 计数单位 | 按"元素"：`size` 字节一个，写 `nmemb` 个 | 按字节：写 `count` 个字节 |
| 返回值 | 写成功的**元素个数**（`size_t`，出错时小于 `nmemb`） | 写成功的**字节数**（`ssize_t`），出错返回 **-1** 并设置 `errno` |
| 写不完的情况 | 内部会循环重试，正常情况下要么全写完、要么出错 | 可能只写一部分（"短写"），要自己循环补写（见第 5 节） |
| 错误信息 | `ferror(fp)` / `perror` | `errno` / `perror` / `strerror(errno)` |
| 多线程 | 每次调用会给 `FILE` 加锁 | 不加锁（内核保证单次调用的原子性，有条件） |
| 适合 | 普通文件、大量小块读写 | 设备文件、管道、socket、需要 fd 的场合（`ioctl`、`mmap`、DMA-BUF） |

一句话：**`fwrite` 是建在 `write` 之上的一层"带缓冲的包装"**。

---

## 3. 关系：fwrite 最终也是调用 write

```
你的程序
  │  fwrite(data, 1, 74674, fp)
  ▼
┌──────────────────────────────────────┐
│ C 标准库（glibc）：FILE 里的用户态缓冲区│  ← fwrite 先把数据拷到这里
│  （一般几 KB，由实现按文件系统块大小定）│
└──────────────────────────────────────┘
  │  缓冲区满了 / fflush(fp) / fclose(fp) 时
  │  调用 write(fd, 缓冲区, n)            ← 这才是真正的系统调用
  ▼
┌──────────────────────────────────────┐
│ 内核：页缓存（page cache）             │  ← write 返回 = 数据到了这里，还没到磁盘
└──────────────────────────────────────┘
  │  内核择机写盘，或者你调用 fsync(fd)
  ▼
 磁盘 / eMMC
```

- **用 `write`**：直接从第二层开始，每次调用都要从用户态切到内核态（系统调用），有固定开销。
- **用 `fwrite`**：多了一层用户态缓冲。很多次小的 `fwrite` 先攒在缓冲区里，攒满了才调一次 `write`，系统调用的次数大大减少。
- **一次写一大块**（比如 74674 字节）：`fwrite` 发现数据比缓冲区还大，通常会绕过缓冲直接 `write`，两者差别不大。

可以用 `fileno(fp)` 拿到 `FILE*` 底层的 fd，这也说明 `FILE*` 就是"fd + 缓冲区"的包装。

---

## 4. 实测：缓冲到底有多大作用

写 1,000,000 字节，两种方式各测一次（`-O2` 编译，测试程序见第 8 节）：

| 写法 | Mac（M 系列） | RK3588 板子 |
|---|---|---|
| `fwrite` 一次写 1MB | 0.0016 s | 0.0035 s |
| `write` 一次写 1MB | 0.0009 s | 0.0037 s |
| `fwrite` 逐字节写 100 万次 | 0.037 s | **0.048 s** |
| `write` 逐字节写 100 万次 | 0.80 s | **1.05 s**（慢 22 倍） |

结论：
- **一次写一大块**：两者差不多，瓶颈在数据拷贝，不在函数本身。
- **很多次小块写**：`fwrite` 快一个数量级以上。100 万次 `fwrite` 背后只有几百次真正的 `write`（缓冲区 4KB 时约 1000000 / 4096 ≈ 245 次），而 100 万次 `write` 就是 100 万次系统调用。
- 这也是 `read_nv12_rows` 一帧调 1620 次 `fread`（每次一行）却不慢的原因 —— 背后是缓冲区在帮忙。

---

## 5. 用 write 时要注意的两个坑

### 坑 1：短写（partial write）—— 返回值可能比你要求的少

`write` 返回 `n`（`0 < n < count`）是**合法的**，不算出错：只写了一部分，剩下的要自己接着写。普通文件很少遇到，管道、socket、被信号打断时常见。所以严谨的写法是循环：

```c
// 把 buf 里的 count 字节完整写出去
bool write_all(int fd, const uint8_t* buf, size_t count) {
    while (count > 0) {
        ssize_t n = write(fd, buf, count);
        if (n < 0) {
            if (errno == EINTR) continue;   // 被信号打断，重试
            perror("write");
            return false;
        }
        buf   += n;                         // 往后挪 n 字节（uint8_t* 才能这样做指针运算）
        count -= n;
    }
    return true;
}
```

`fwrite` 内部已经替你做了这个循环，所以 `WriteStream::write` 只要比较一次返回值就够了。

### 坑 2：`write` 返回了 ≠ 数据到磁盘了

`write` 只保证数据进了内核的页缓存。这时程序崩溃，数据不会丢（内核还会写盘）；但**板子突然断电**，数据可能还没写到 eMMC 上。要确保落盘，调用 `fsync(fd)`。

`fwrite` 多一层：数据可能还在用户态缓冲区里。**程序崩溃**时这部分会丢 —— 所以一定要 `fclose`（本项目由 `FilePtr` 析构时自动 `fclose`），或者需要时调用 `fflush(fp)`。

| 数据在哪 | 程序崩溃 | 板子断电 | 怎么推到下一层 |
|---|---|---|---|
| `FILE` 的用户态缓冲区（只有 fwrite 有） | ❌ 丢 | ❌ 丢 | `fflush(fp)` / `fclose(fp)` |
| 内核页缓存 | ✅ 不丢 | ❌ 可能丢 | `fsync(fd)` |
| 磁盘 / eMMC | ✅ | ✅ | — |

---

## 6. 不要混用：同一个文件别既 fwrite 又 write

```c
FILE* fp = fopen("out.h264", "wb");
fwrite(sps, 1, 40, fp);            // 这 40 字节还在 FILE 的缓冲区里
write(fileno(fp), frame, 74674);   // 这 74674 字节直接进了内核
fclose(fp);                        // 这时才把 40 字节写出去
// 结果：文件里帧在前、SPS/PPS 在后 —— 顺序乱了，播放器打不开
```

必须混用的话，切换前先 `fflush(fp)`。最简单的办法是：一个文件从头到尾只用一套。

---

## 7. 在本项目里怎么选

| 场景 | 用哪个 | 原因 |
|---|---|---|
| Day 1：读 NV12 文件、写 .h264 文件（`ReadYUV` / `WriteStream`） | `fread` / `fwrite` | 普通文件、逐行小块读，缓冲正好有用；跨平台，代码简单 |
| Day 3：摄像头 `/dev/video0` | `open` / `ioctl` / `mmap` | 设备文件要用 fd，`ioctl` 只认 fd；没有"读文件"这回事，是驱动把画面放进缓冲区 |
| 摄像头画面交给 MPP | DMA-BUF **fd** | 内存直接在硬件之间传递，CPU 不拷贝，fread/fwrite/write 都用不上 |
| 以后推流（socket） | `send` / `write` | socket 是 fd，而且要处理短写，正好用第 5 节的 `write_all` 循环 |

所以 `WriteStream` 现在用 `fwrite` 是合适的。以后加推流时，再写一个 `StreamSink` 的实现（比如 `RtspSink`），在它里面用 socket 的 `send`，`EncodePipeline` 不用改。

---

## 8. 附：第 4 节的测试程序

```c
// wbench.c —— Mac：cc -O2 -o wbench wbench.c && ./wbench
//            板子：用交叉编译器编译后 adb push 上去运行（把路径改成 /userdata/av/）
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

int main(void) {
    enum { N = 1000000 };
    static unsigned char buf[N];
    memset(buf, 0xAB, N);
    double t;

    t = now(); FILE* fp = fopen("/tmp/w1.bin", "wb"); fwrite(buf, 1, N, fp); fclose(fp);
    printf("fwrite x1       : %.4f s\n", now() - t);

    t = now(); int fd = open("/tmp/w2.bin", O_WRONLY | O_CREAT | O_TRUNC, 0644); write(fd, buf, N); close(fd);
    printf("write  x1       : %.4f s\n", now() - t);

    t = now(); fp = fopen("/tmp/w3.bin", "wb");
    for (int i = 0; i < N; i++) fwrite(&buf[i], 1, 1, fp);
    fclose(fp);
    printf("fwrite x1000000 : %.4f s\n", now() - t);

    t = now(); fd = open("/tmp/w4.bin", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    for (int i = 0; i < N; i++) write(fd, &buf[i], 1);
    close(fd);
    printf("write  x1000000 : %.4f s\n", now() - t);
    return 0;
}
```

---

## 9. 一句话总结

- **相同点**：都是"从一个地址开始，把连续的一块内存写出去"，不需要调用方 for 循环逐字节写。
- **不同点**：`fwrite` 是 C 标准库，带用户态缓冲，跨平台，按元素计数；`write` 是系统调用，没有缓冲，每次都进内核，按字节计数，可能短写。
- **关系**：`fwrite` 内部最终调用 `write`，多出来的那层缓冲让"大量小块写"快一个数量级（板子实测 22 倍）。
- **怎么选**：普通文件用 `fwrite`；设备、socket、DMA-BUF 这些需要 fd 的场合用 `write` / `ioctl` 那一套。
