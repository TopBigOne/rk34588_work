# day3_mmap：在 RK3588 上动手学 mmap

> 为什么有这个工程（2026-10-27）：用户在 `/Users/dev/Documents/GCC_Work/CPP/mmap` 里系统学了 mmap（stage1～9），其中 stage5 "设备与驱动层 mmap" 讲了 V4L2、DMA-BUF、DRM，但 Mac 上没有这些设备，只能打印文字。
> RK3588 上全都有。这个工程把 stage5 的理论在真实硬件上**动手做一遍**。
>
> 先读：[day3_camera_cache_test/doc/open_ioctl_mmap是什么.md](../day3_camera_cache_test/doc/open_ioctl_mmap是什么.md)（open / ioctl / mmap 从头讲，9 张图）。
>
> 写代码时看：[具体编码day3_mmap的流程.md](具体编码day3_mmap的流程.md)（M1～M4 每一步的任务拆解和参考代码，已交叉编译通过）。
>
> 实验结果解读：[doc/1-理解M1的运行结果.md](doc/1-理解M1的运行结果.md)（怎么到 maps 里查指针、31 行分 9 组、库文件为什么变成 4 行、s 和 p 写时复制实测、从高往低找空位，6 张图）。
>
> M2 结果解读：[doc/2-理解M2的运行结果.md](doc/2-理解M2的运行结果.md)（pagemap 逐页实测 A、B 是同一批物理页、maps 两行对比、offset 对两种 fd 意思不一样、buffer 物理上分 8 段、EXPBUF 的 flags 和"每次新建一个 dma_buf"、关掉摄像头后 B 还能用，4 张图）。
>
> M2 代码怎么串起来：[doc/5-理解M2代码的串联流程.md](doc/5-理解M2代码的串联流程.md)（7 步流程图：open → S_FMT → REQBUFS → QUERYBUF → mmap → EXPBUF → mmap；ioctl"填表 → 写回"，exportBuf.fd 是怎么来的；mmap 靠 fd 和 V4L2 连起来；CameraBuffers 每个字段是哪一步填的）。
>
> 知识点：[doc/3-理解v4l2_format.md](doc/3-理解v4l2_format.md)（结构体里套了哪些结构体、type 选哪个成员、本板 30 个节点只用到 pix_mp 和 meta、没有音频参数、S_FMT 填什么写回什么、平面、bytesperline，7 张图）；[doc/4-理解v4l2_requestbuffers.md](doc/4-理解v4l2_requestbuffers.md)（REQBUFS 背后分配了什么、每块比一帧大、count 上限 64 和 0 = 释放、三种 memory、队列只认一个 fd，4 张图）。

---

## 1. 和其他工程的关系

| 工程                                                                             | 回答什么问题                                                                          |
|----------------------------------------------------------------------------------|---------------------------------------------------------------------------------------|
| day3_cam2h264                                                                    | 摄像头 → 零拷贝 → H.264 编码，怎么写（mmap 只是其中一步）                             |
| day3_camera_cache_test                                                           | 摄像头的 buffer 对 CPU 是 cached 还是 uncached（测速度）                              |
| **day3_mmap（本工程）**                                                          | **mmap 本身**：映射到了哪里、两个指针是不是同一块内存、sync 在做什么、显示怎么用 mmap |
| [GCC_Work/CPP/mmap ](https://github.com/TopBigOne/GCC_Work/tree/master/CPP/mmap) | mmap 的通用理论（文件、匿名、共享内存、内核原理），在 Mac 上                          |

---

## 2. 四个实验（里程碑）

|   #    | 实验                                                                                                        | 要看到什么                                                                    | 状态 |
|:------:|-------------------------------------------------------------------------------------------------------------|-------------------------------------------------------------------------------|:---:|
| **M1** | 看 `/proc/self/maps`：摄像头 buffer、dma_buf、malloc、文件映射在地址空间里长什么样                          | 每种映射一行，能认出哪一行是摄像头 buffer、哪一行是 dma_buf                   | ⬜ |
| **M2** | ⭐ **同一块内存，两个指针**：`mmap(videoFd, mem_offset)` 和 `mmap(EXPBUF 的 fd, 0)` 指向同一块摄像头 buffer | 从指针 A 写一串字，从指针 B 读出来一模一样 → 证明是同一块物理内存             | ✅ 2026-10-31 |
| **M3** | 自己调 `DMA_BUF_IOCTL_SYNC`：不经过 MPP，直接对 dma_buf fd 做 sync                                          | 理解 M6 里 `mpp_buffer_sync_begin / end` 背后在做什么；计时看 sync 一次要多久 | ⬜ |
| **M4** | DRM dumb buffer：`/dev/dri/card0` 申请一块显存，mmap 后往里画一个色块                                       | 接了显示器的话，屏幕上出现色块（没接显示器就只做到 mmap 成功）                | ⬜ |

做完 M1～M4，stage5 里"只打印文字"的 V4L2、DMA-BUF、DRM 三部分，就都在真实硬件上跑过了。

---

## 3. 每个实验的思路（不是完整代码）

### M1：一个指针，到"登记表"里查它是谁

#### 一句话需求

**拿到几个指针，到 `/proc/self/maps` 里找到每个指针所在的那一行，看那一行是怎么描述它的。**

#### 为什么要做

程序里的指针只是一个数字（虚拟地址），比如 `0x7f8a000000`。光看这个数字，**分不出**它是 malloc 来的、文件映射来的，还是摄像头 buffer。

Linux 给每个程序都记了一张**登记表**：从哪个地址到哪个地址，归谁管、能读还是能写。这张表就是 `/proc/<pid>/maps`（程序自己看自己用 `/proc/self/maps`），它是一个普通文本文件，用 `fgets` 一行一行读就行。

> 打个比方：指针是**门牌号**，maps 是小区物业的**登记簿**。门牌号上看不出住的是谁，查登记簿就知道："3 栋 1～4 号，住户 /dev/video22，可读可写，共用"。

你之前问过"mmap 就是把一个 fd 变成一个能操作的指针？"，M1 就是这句话的**证据**：mmap(videoFd) 返回的指针，在登记簿里那一行写的就是 `/dev/video22`。

#### 登记簿的一行长什么样

```
起始地址-结束地址      权限  偏移     设备  inode  路径（谁的）
7f8a000000-7f8a2fd000  rw-s  00000000 00:06 12345  /dev/video22
                       ││││
                       │││└─ s = MAP_SHARED（共用），p = MAP_PRIVATE（私有）
                       ││└── x 可执行
                       │└─── w 可写
                       └──── r 可读
```

（这一行是举例，具体以板子上打印出来的为准。）

#### 程序要做的事（分三小步，一步一步来）

**第 1 步：只看 malloc 和文件，先跑通**

1. `malloc` 一块 4 MB 的内存，得到指针 `mallocAddr`（超过 128 KB 的 malloc，glibc 内部会用 mmap）
2. 打开一个普通文件（比如程序自己写一个 `/tmp/day3_mmap_test.bin`，4 KB），`mmap` 得到 `fileAddr`
3. 打印这两个地址
4. 用 `fopen("/proc/self/maps")` + `fgets`，把整个文件**原样打印**出来
5. 你自己用眼睛在输出里找：哪一行包含 `mallocAddr`？哪一行包含 `fileAddr`？

**第 2 步：加上摄像头 buffer**

6. 打开 `/dev/video22`，S_FMT、REQBUFS(4)，对 4 块 buffer 分别 QUERYBUF + mmap（和 day3_cam2h264 一样，**不用 STREAMON**）
7. 打印 4 个地址，再打印一次 maps，找到它们

**第 3 步：加上 dma_heap**

8. 从 `/dev/dma_heap/system` 申请一块 3 MB，mmap（和 day3_camera_cache_test 的 `alloc_dma_heap` 一样）
9. 打印地址，再打印一次 maps，找到它

**选做**：写一个函数 `print_maps_line_containing(const void* address)`，自动找出包含这个地址的那一行，只打印那一行，不用眼睛找。（注意：解析十六进制不能用 `sscanf` / `strtoul`，见第 5 节，要自己写一个小循环。）

#### 期望看到的输出（大概样子，⚠️ 是我的猜测，要上板验证）

```
[M1] mallocAddr   = 0x7f9b400010
[M1] fileAddr     = 0x7f9c7ff000
[M1] cameraAddr0  = 0x7f9a000000
[M1] heapAddr     = 0x7f99d00000

7f9b400000-7f9b801000 rw-p 00000000 00:00 0                        ← malloc：没有路径，p
7f9c7ff000-7f9c800000 rw-s 00000000 b3:08 1234 /tmp/day3_mmap_test.bin  ← 文件：路径是文件名，s
7f9a000000-7f9a2fb000 rw-s 00000000 00:06 567  /dev/video22        ← 摄像头 buffer
7f99d00000-7f9a000000 rw-s 00000000 00:09 89   /dmabuf:            ← dma_heap（路径具体写什么，不确定）
```

#### 做完要回答的问题

| # | 问题 | 和什么有关 |
|---|---|---|
| 1 | malloc 那一行，路径那一列是空的，为什么？ | 匿名映射（MAP_ANONYMOUS）：不对应任何文件 |
| 2 | 摄像头 4 块 buffer 是 4 行，还是合成了 1 行？ | 每次 mmap 是一段独立的映射 |
| 3 | 摄像头那一行的"偏移"列，和 QUERYBUF 拿到的 `mem_offset` 一样吗？ | mmap 的最后一个参数 |
| 4 | dma_heap 那一行的路径写的是什么？是 `/dev/dma_heap/system` 吗？ | mmap 的是 `allocData.fd`，不是 `heapFd` |
| 5 | malloc 那行是 `p`，摄像头那行是 `s`，为什么？ | MAP_PRIVATE / MAP_SHARED |

问题 4 最有意思：如果路径**不是** `/dev/dma_heap/system`，就说明 mmap 的对象确实是新拿到的 dma_buf fd，而不是 heap 本身（对应 open_ioctl_mmap是什么.md 里 heapFd 和 allocData.fd 那张图）。

### M2：同一块内存，两个指针 ⭐

**要证明的事**：M2（day3_cam2h264）里 CPU 用 `mmap(videoFd, mem_offset)` 拿到的指针，和 M3 里交给编码器的 EXPBUF fd，**背后是同一块物理内存**。之前我们是用 bufinfo 间接证明的，这次直接证明。

**步骤**：
1. 打开 video22，S_FMT、REQBUFS（不用 STREAMON，不需要真的拍）
2. 对第 0 块：
   - 指针 A：QUERYBUF 拿 `mem_offset` → `mmap(videoFd, mem_offset)`
   - 指针 B：`VIDIOC_EXPBUF` 拿 dma_buf fd → `mmap(dmaBufFd, 0)`
3. 打印两个地址：**应该不一样**（两扇门，门牌号不同）
4. 通过指针 A 写一串字，比如 `"hello from A"`
5. 通过指针 B 读出来：**应该是同一串字**
6. 反过来，从 B 写、从 A 读，再试一次

**要回答的问题**：两个地址不一样，为什么读写的是同一块内存？（提示：页表，open_ioctl_mmap是什么.md 第 1 节）

### M3：自己调 DMA_BUF_IOCTL_SYNC

**要理解的事**：M6 的 `mpp_buffer_sync_begin / end`，MPP 源码里就是对 dma_buf fd 调 `ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync)`（`rk_code/external/mpp/osal/mpp_dmabuf.cpp` 第 33、49 行）。这次不经过 MPP，自己调。

**步骤**：
1. 从 dma_heap `system`（cached）申请一块 3 MB，mmap
2. 写之前：`sync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE`，ioctl
3. memset 写满
4. 写之后：`sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE`，ioctl
5. 用 `steady_clock` 量 sync_start、memset、sync_end 各花多久
6. 再对 `system-uncached` 的一块做同样的事，比较

**要回答的问题**：cached 和 uncached 两块，sync_end 花的时间一样吗？为什么？（提示：MPP 源码 `mpp_buffer.cpp` 第 293～297 行，uncached 的 buffer 直接跳过 sync）

### M4：DRM dumb buffer

**要理解的事**：以后要把画面显示到屏幕上（预览），会用到 DRM。dumb buffer 是 DRM 里最简单的一种显存：CPU 用 mmap 直接写像素。和 stage5 打印的步骤一样：

```
open("/dev/dri/card0")
ioctl(DRM_IOCTL_MODE_CREATE_DUMB)   → 申请一块显存，拿到 handle、pitch（每行字节数）、size
ioctl(DRM_IOCTL_MODE_MAP_DUMB)      → 拿到一个"假 offset"
mmap(drmFd, offset)                 → CPU 指针
往指针里写像素                       → 地址 = y * pitch + x * 4（XRGB8888）
```

真的显示到屏幕上，还要再做 `DRM_IOCTL_MODE_ADDFB` + 设置 CRTC，步骤比较多，M4 先做到"mmap 成功、写进像素"，显示放到后面（Day 4 以后）。

**要回答的问题**：`pitch` 和 `width × 4` 一样吗？（和摄像头的 stride、stage5 的 `line_length` 是同一个概念）

---

## 4. 板子上要先确认的事（⬜ 还没确认）

| 事情                         | 怎么确认                         | 影响哪个实验 |
|------------------------------|----------------------------------|---|
| 有没有 `/dev/dri/card0`      | `adb shell ls -l /dev/dri/`      | M4 |
| 有没有接显示器               | 看板子                           | M4 能不能看到色块 |
| EXPBUF 拿到的 fd 能不能 mmap | M2 里实测                        | M2 |
| 相机 App 有没有占着 video22  | `adb shell 'fuser /dev/video22'` | M1、M2 |

---

## 5. 写代码时注意

- **不要用 `scanf`、`sscanf`、`atoi`、`strtol`**：工具链的 glibc 比板子新，
- 这几个函数会链接到 `GLIBC_2.38` 才有的 `__isoc23_*` 版本，
- 板子上一运行就报 `version GLIBC_2.38 not found`（2026-10-21 写丢帧探针时踩过；GCC_Work/CPP/mmap 交叉编译时也查出来了）。
- 读文件用 `fgets` / `std::getline`，数字自己解析或用 `std::istringstream`。
  - 编译完可以检查：`aarch64-buildroot-linux-gnu-objdump -T cmake-build-rk3588-debug/day3_mmap | grep GLIBC_2.38`，没有输出才对。
- 名字按 `rk_work/代码规范.md`：`videoFd`、`dmaBufFd`、`drmFd`、`mmapAddress`、`mapLengthBytes`……
- mmap 失败返回 `MAP_FAILED`，不是 `NULL`。
- 每个 mmap 都要 munmap，每个 fd 都要 close，顺序和申请相反。

---

## 6. 运行

```bash
cd /Users/dev/Documents/AV/rk_work/day3_mmap
./run_on_board_with_mac.sh m1     # M1：到 /proc/self/maps 里查指针是谁
./run_on_board_with_mac.sh m2     # M2：同一块内存，两个指针
```

不带参数时跑 main.cpp 里写的默认实验（experimentName 为空就改指向它，2026-10-31 调 M1 时是 "m1"）。参数不是 m1 / m2 时返回 1。CLion 里运行时，在 Run Configuration 的 **Program arguments** 填 `m1` 或 `m2`。

**代码目录**（2026-10-31 从一个 main.cpp 拆开，main.cpp 只负责按参数选实验、调用）：

```
src/
├── main.cpp                       ./day3_mmap m1 | m2
├── experiments/
│   ├── m1_maps.h / .cpp           run_m1_maps()、create_test_file()
│   └── m2_two_pointers.h / .cpp   run_m2_two_pointers()
├── camera/
│   └── camera_buffers.h / .cpp    CameraBuffers、open_camera_and_map_buffers()、close_camera()   ← M1 M2
├── dma_heap/
│   └── dma_heap_buffer.h / .cpp   DmaHeapBuffer、alloc_dma_heap()、free_dma_heap()           ← M1 第 3 步、M3
└── common/
    ├── log.h                      带颜色的日志（每个 .cpp 自己的 kTag：Main、M1、M2、Camera、DmaHeap、Maps）
    └── maps_printer.h / .cpp      print_whole_maps()、print_maps_line_containing()（parse_hex 是内部函数）
```

以后加 M3、M4：在 `src/experiments/` 加 `m3_sync.h/.cpp`、`m4_dumb.h/.cpp`，CMakeLists 里加上 .cpp，main.cpp 里加一个 `if`。

编译目录 `cmake-build-rk3588-debug/` 已经配好（交叉编译）。CLion 里要加 **RK3588-Debug** profile（CMake options 填 `-DCMAKE_TOOLCHAIN_FILE=/Users/dev/rk-toolchain/rk3588-toolchain.cmake`），和 day3_cam2h264 一样。
