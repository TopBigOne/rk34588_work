# fread 和 fwrite 的详细用法

> 结合 day1_yuv2h264 里用到的地方来讲：`read_nv12_frame()` 读 NV12（现在封装成了 `src/source/read_yuv.cpp` 的 `ReadYUV::read_frame()`，读法一样），`fwrite` 写 SPS/PPS 和码流。

---

## 1. 函数原型

```c
#include <cstdio>   // C 里是 <stdio.h>

size_t fread (void*       ptr, size_t size, size_t nmemb, FILE* stream);
size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream);
```

| 参数 | fread | fwrite |
|---|---|---|
| `ptr` | 读出来的数据**放到哪里**（目标内存） | 要写的数据**从哪里来**（源内存） |
| `size` | 每个"元素"多少字节 | 同左 |
| `nmemb` | 想读多少个元素 | 想写多少个元素 |
| `stream` | 从哪个文件读 | 写到哪个文件 |
| **返回值** | **实际读到的元素个数** | **实际写入的元素个数** |

一句话：**从 `stream` 搬 `size × nmemb` 个字节到 `ptr`（fread），或者反过来（fwrite）**，返回搬成功了几个元素。

---

## 2. size 和 nmemb 怎么填

读的总字节数都是 `size × nmemb`，区别在于**返回值按什么单位算**。

```c
uint8_t buf[1920];

fread(buf, 1, 1920, fp);    // ① 1920 个 1 字节的元素 → 返回值 0～1920，按字节数
fread(buf, 1920, 1, fp);    // ② 1 个 1920 字节的元素 → 返回值 0 或 1
```

| 写法 | 文件只剩 1000 字节时 | 适合 |
|---|---|---|
| ① `size=1, nmemb=N` | 返回 **1000**，能知道读到了多少 | **读字节流（推荐）**，项目里都用这种 |
| ② `size=N, nmemb=1` | 返回 **0**，不知道读了多少（数据其实读进去了一部分） | 只关心"整块读成功没有" |

读结构体数组时按元素来填，很自然：
```c
struct Point { int x, y; };
Point pts[100];
size_t n = fread(pts, sizeof(Point), 100, fp);   // 返回读到了几个 Point
```

---

## 3. 打开文件：fopen 的模式

```c
FILE* fp = fopen(path, mode);
if (!fp) { perror("fopen"); /* 处理错误 */ }
```

| mode | 意思 | 文件不存在 | 文件已存在 |
|---|---|---|---|
| `"rb"` | 只读 | 返回 `NULL` | 从头读 |
| `"wb"` | 只写 | **新建** | **清空**后从头写 |
| `"ab"` | 追加写 | 新建 | 保留内容，写到末尾 |
| `"r+b"` | 读写 | 返回 `NULL` | 保留内容，从头开始 |
| `"w+b"` | 读写 | 新建 | **清空** |

- **`b` = 二进制模式**。Linux / macOS 上有没有 `b` 都一样；但 **Windows 上文本模式会把 `\n` 转成 `\r\n`**，音视频数据会被改坏。所以读写二进制文件**养成习惯加 `b`**。
- 项目里：输入 `fopen(in_path, "rb")`，输出 `fopen(out_path, "wb")`。所以 `out.h264` 每次运行都会被清空重写，不用提前创建，但**所在目录必须存在**。
- `perror("fopen")` 会打印出系统给的原因，比如 `fopen: No such file or directory`、`fopen: Permission denied`，比自己写 "open failed" 更好查问题。

---

## 4. 文件位置：fread / fwrite 会自动往后走

每个 `FILE*` 内部有一个**文件位置**（当前读写到第几个字节）。
**每次 fread / fwrite 成功 N 个字节，位置自动 +N。**

```
文件：  [ Y第0行 | Y第1行 | ... | Y第1079行 | UV第0行 | ... | UV第539行 | 下一帧Y第0行 | ...
         ↑
       初始位置 0

fread(..., 1920, fp)  → 读 Y第0行，位置 = 1920
fread(..., 1920, fp)  → 读 Y第1行，位置 = 3840
...
读完 1080 行 Y        → 位置 = 2,073,600，正好是 UV 第 0 行
读完 540 行 UV        → 位置 = 3,110,400，正好是下一帧的开头
```

这就是 `read_nv12_frame()` **不需要 fseek** 的原因：文件是紧密排列的，顺序读下去，位置自然就对了。

### 手动移动位置：fseek / ftell / rewind

```c
fseek(fp, offset, SEEK_SET);   // 移到 offset（从文件头算）
fseek(fp, offset, SEEK_CUR);   // 从当前位置往后/往前移 offset
fseek(fp, 0,      SEEK_END);   // 移到文件末尾
long pos = ftell(fp);          // 当前位置
rewind(fp);                    // 回到开头，同时清除 EOF 和错误标志
```

常见用法：
```c
// 求文件大小
fseek(fp, 0, SEEK_END);
long file_size = ftell(fp);
rewind(fp);

// 算 NV12 文件有几帧
long frame_bytes = 1920 * 1080 * 3 / 2;          // 3,110,400
long frames = file_size / frame_bytes;           // 186,624,000 / 3,110,400 = 60

// 直接跳到第 n 帧
fseek(fp, n * frame_bytes, SEEK_SET);
```

> `fseek` / `ftell` 用的是 `long`。RK3588 上是 64 位 Linux，`long` 是 64 位，几个 GB 的文件也没问题。如果在 32 位系统上处理超过 2GB 的文件，要改用 `fseeko` / `ftello`（参数是 `off_t`）并定义 `_FILE_OFFSET_BITS=64`。

---

## 5. 返回值不够数时：feof 和 ferror

fread 返回值 < 想要的个数，只有两种可能：

| 原因 | 怎么判断 | 例子 |
|---|---|---|
| **读到文件末尾了** | `feof(fp)` 返回非 0 | 60 帧读完了，第 61 帧读不到 |
| **出错了** | `ferror(fp)` 返回非 0 | 磁盘错误、U 盘被拔了 |

```c
size_t n = fread(buf, 1, want, fp);
if (n < want) {
    if (feof(fp))        printf("文件读完了，只读到 %zu 字节\n", n);
    else if (ferror(fp)) perror("fread");
}
```

`read_nv12_frame()` 里没有区分这两种，统一当作"没有下一帧了"返回 `false`，对练习程序来说够用。

fwrite 返回值 < 想要的个数，一般就是出错了：**磁盘满了**、文件系统只读、没有权限等。
```c
if (fwrite(ptr, 1, len, fp) != len) {
    perror("fwrite");
}
```

### ⚠️ 经典错误：`while (!feof(fp))`

```c
// ❌ 错误：最后会多处理一次
while (!feof(fp)) {
    fread(buf, 1, N, fp);
    process(buf);
}
```
`feof` 只有在**某次 fread 已经读不到东西之后**才会变成真。所以读完最后一帧时它还是假，循环会再跑一次：这一次 fread 读到 0 字节，`buf` 里还是上一帧的旧数据，被**重复处理了一遍**。

```c
// ✅ 正确：用 fread 的返回值判断
while (fread(buf, 1, N, fp) == N) {
    process(buf);
}
```

`mpi_enc_test.c` 里的写法是 `if (ret == MPP_NOK || feof(p->fp_input))`：先看 `read_image` 的返回值，再看 `feof`，也是"读过之后再判断"，所以没问题。

---

## 6. 缓冲：为什么逐行 fread 不慢

`FILE*` 自带一块用户态缓冲区（glibc 默认一般是 4KB 左右，具体由实现决定）。

```
fread(1920 字节)
   │
   ├─ 缓冲区里有数据 → 直接从缓冲区拷贝（很快，不进内核）
   └─ 缓冲区空了    → 调一次 read() 系统调用，一次填满一整块缓冲区
```

所以 `read_nv12_frame()` 虽然一帧调了 1620 次 fread（1080 + 540），但真正进内核的 `read()` 次数要少得多。读一帧 3MB 数据，主要的开销是内存拷贝，不是函数调用次数。

fwrite 也一样：先写进缓冲区，**缓冲区满了、调用 `fflush(fp)`、或者 `fclose(fp)` 时**，才真正写到磁盘（准确说是交给内核）。

这带来一个坑：
```c
fwrite(sps_pps, 1, 40, fp);
// 程序在这里崩溃了 → 这 40 字节可能还在缓冲区里，out.h264 是 0 字节！
```
- 正常结束一定要 `fclose(fp)`（项目里在 `CLEANUP` 里关）。
- 想马上落盘（比如边编码边让别人读文件），写完调用 `fflush(fp)`。

### 调整缓冲区大小：setvbuf（可选）

```c
// 必须在 fopen 之后、第一次读写之前调用
static char big_buf[1 << 20];                       // 1MB
setvbuf(fp, big_buf, _IOFBF, sizeof(big_buf));      // 全缓冲
```
读大文件时把缓冲区调大，可以减少系统调用次数。今天的程序不需要。

---

## 7. 在 day1_yuv2h264 里的用法

### 7.1 fread：逐行读 NV12 到硬件缓冲区

```c
static bool read_nv12_frame(FILE* fp, uint8_t* dst,
                            int width, int height, int hor_stride, int ver_stride) {
    for (int row = 0; row < height; row++) {
        //        目标：缓冲区第 row 行   1 字节一个  读 width 个
        if (fread(dst + row * hor_stride, 1,        width, fp) != (size_t)width)
            return false;
    }
    uint8_t* dst_uv = dst + hor_stride * ver_stride;
    for (int row = 0; row < height / 2; row++) {
        if (fread(dst_uv + row * hor_stride, 1, width, fp) != (size_t)width)
            return false;
    }
    return true;
}
```
- `size = 1`：按字节读，返回值就是读到的字节数，好判断。
- `(size_t)width`：fread 返回 `size_t`（无符号），`width` 是 `int`（有符号）。直接比较编译器会给出有符号和无符号比较的警告，所以转一下。
- **目标地址每行跳 `hor_stride`，但只读 `width` 个字节**：行尾的填充留空。详见 `nv12中yuv分布效果图和读取方式.md`。

### 7.2 fwrite：把码流写到文件

```c
fwrite(mpp_packet_get_pos(packet),      // 源：packet 里码流的起始地址
       1,                               // 1 字节一个
       mpp_packet_get_length(packet),   // 写多少字节
       fpOut);
```
- **H.264 文件就是把每个 packet 原样首尾相接**：先写 SPS/PPS，再一帧一帧写码流。不需要额外的文件头，这种格式叫 **Annex-B 裸流**（每个 NAL 前面是 `00 00 00 01` 起始码），所以 `.h264` 文件可以直接用 VLC / ffplay 播放。
- 要更严谨，可以检查返回值：
  ```c
  size_t len = mpp_packet_get_length(packet);
  if (fwrite(mpp_packet_get_pos(packet), 1, len, fpOut) != len)
      perror("fwrite");    // 板子 /userdata 满了时会走到这里
  ```

---

## 8. 两个完整小例子（在 Mac 上就能编译运行）

### 8.1 统计 NV12 文件帧数，并把第 n 帧单独存出来

```cpp
// nv12_extract.cpp
// 编译：clang++ -std=c++17 nv12_extract.cpp -o nv12_extract
// 运行：./nv12_extract in_1080p_60f.nv12 1920 1080 10 frame10.nv12
#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 6) {
        printf("usage: %s in.nv12 width height frame_index out.nv12\n", argv[0]);
        return -1;
    }
    const char* in_path  = argv[1];
    int width            = atoi(argv[2]);
    int height           = atoi(argv[3]);
    long index           = atol(argv[4]);
    const char* out_path = argv[5];
    long frame_bytes     = (long)width * height * 3 / 2;

    FILE* fin = fopen(in_path, "rb");
    if (!fin) { perror("fopen in"); return -1; }

    // 1. 求文件大小，算帧数
    fseek(fin, 0, SEEK_END);
    long file_size = ftell(fin);
    long frames    = file_size / frame_bytes;
    printf("file %ld bytes, frame %ld bytes, %ld frames\n", file_size, frame_bytes, frames);
    if (file_size % frame_bytes != 0)
        printf("warning: 文件大小不是整帧的倍数，宽高写对了吗？\n");
    if (index < 0 || index >= frames) {
        printf("frame_index 要在 [0, %ld) 之间\n", frames);
        fclose(fin);
        return -1;
    }

    // 2. 跳到第 index 帧，整帧读出来（文件里是紧密排列，这里不涉及 stride，可以一次读）
    std::vector<unsigned char> buf(frame_bytes);
    fseek(fin, index * frame_bytes, SEEK_SET);
    size_t n = fread(buf.data(), 1, frame_bytes, fin);
    fclose(fin);
    if (n != (size_t)frame_bytes) {
        printf("只读到 %zu 字节\n", n);
        return -1;
    }

    // 3. 写到新文件
    FILE* fout = fopen(out_path, "wb");
    if (!fout) { perror("fopen out"); return -1; }
    if (fwrite(buf.data(), 1, frame_bytes, fout) != (size_t)frame_bytes) {
        perror("fwrite");
        fclose(fout);
        return -1;
    }
    fclose(fout);
    printf("frame %ld -> %s\n", index, out_path);
    return 0;
}
```
查看结果：
```bash
/usr/local/ffmpeg/4.4/bin/ffplay -f rawvideo -pixel_format nv12 -video_size 1920x1080 frame10.nv12
```

> 注意这里**整帧一次 fread 是对的**：因为是从文件读到普通内存再写回文件，两边都是紧密排列。只有读进 **MPP 硬件缓冲区**（带 stride）时才必须逐行读。

### 8.2 用固定大小的缓冲区复制一个大文件

```cpp
// copy_file.cpp
// 编译：clang++ -std=c++17 copy_file.cpp -o copy_file
// 运行：./copy_file in_1080p_60f.nv12 copy.nv12 && cmp in_1080p_60f.nv12 copy.nv12 && echo same
#include <cstdio>

int main(int argc, char** argv) {
    if (argc != 3) {
        printf("usage: %s src dst\n", argv[0]);
        return -1;
    }
    FILE* src = fopen(argv[1], "rb");
    if (!src) { perror("fopen src"); return -1; }
    FILE* dst = fopen(argv[2], "wb");
    if (!dst) { perror("fopen dst"); fclose(src); return -1; }

    static unsigned char buf[64 * 1024];   // 每次搬 64KB，不用把 178MB 全读进内存
    size_t n;
    long total = 0;
    int ret = 0;

    // 用返回值判断：读到多少就写多少，最后一次可能不满 64KB
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
        if (fwrite(buf, 1, n, dst) != n) {
            perror("fwrite");
            ret = -1;
            break;
        }
        total += n;
    }
    if (ferror(src)) {
        perror("fread");
        ret = -1;
    }

    fclose(src);
    fclose(dst);
    printf("copied %ld bytes\n", total);
    return ret;
}
```
要点：**最后一次 fread 可能不满**，所以 fwrite 写的是 `n`，不是 `sizeof(buf)`。

---

## 9. 常见错误汇总

| 错误 | 后果 | 正确做法 |
|---|---|---|
| 不检查 `fopen` 返回值 | 文件不存在时传 `NULL` 给 fread → 段错误 | `if (!fp) { perror(...); ... }` |
| 不检查 fread 返回值 | 文件读完了还在用旧数据 | 比较返回值和想要的个数 |
| `while (!feof(fp))` | 最后一次重复处理 | 用 fread 返回值控制循环 |
| `fread(buf, sizeof(buf), 1, fp)`，而 `buf` 是**指针** | `sizeof(指针)` = 8，只读 8 字节 | 用真实长度；`sizeof` 只对数组有效 |
| 最后一次写 `sizeof(buf)` 而不是 `n` | 文件末尾多出垃圾数据 | 读到多少写多少 |
| 忘了 `fclose` | 缓冲区里最后一段没写进文件；文件句柄泄漏 | 每个 `fopen` 对应一个 `fclose` |
| 读写模式（`r+b`）下读完直接写，或者写完直接读 | C 标准规定这是未定义行为，结果可能错乱 | 读写切换之间调用一次 `fseek` 或 `fflush` |
| 二进制文件用 `"r"` / `"w"` 打开 | Windows 上数据被改坏 | 加 `b`：`"rb"` / `"wb"` |
| 往 MPP 缓冲区整帧 fread | 画面错位、底部发绿 | 按 stride 逐行读 |

---

## 10. 和 open / read / write 的区别（了解即可）

| | `fopen` / `fread` / `fwrite` | `open` / `read` / `write` |
|---|---|---|
| 层次 | C 标准库，跨平台 | Linux 系统调用（POSIX） |
| 句柄 | `FILE*` | `int fd`（文件描述符） |
| 缓冲 | 有用户态缓冲区 | 没有，每次调用都进内核 |
| 适合 | 普通文件读写，小块多次读写 | 设备文件（`/dev/video0`、`/dev/mpp_service`）、需要 fd 的场合 |

后面 Day 3 接摄像头时，`/dev/video0` 要用 `open` / `ioctl` / `mmap`，到时候用的就是右边这一套；摄像头缓冲区还会以 **DMA-BUF fd** 的形式直接交给 MPP，中间不用 fread 拷贝。
