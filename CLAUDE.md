# rk_work 项目约定

RK3588 MPP 学习项目。代码在 Mac 上用 CLion 交叉编译，放到板子上运行和调试。

## 板子连接：运行和调试都走 USB（adb），不走网络

- 不要用 scp、ssh，也不要用板子 IP（IP 会变，之前从 .196 变成了 .197，导致 CLion 一直卡在 "Starting run configuration"）
- adb 已经加进 PATH（实际位置 `~/Documents/Android_Env/sdk/platform-tools/adb`），命令和脚本里直接写 `adb`，不要写绝对路径
- 板子用 OTG 线连 TypeC0 口，先用 `adb devices` 确认能看到板子

### 运行

- Mac：`day1_yuv2h264/run_on_board_with_mac.sh`，做的事情是：编译 → `adb push` 到 `/tmp/CLion/run` → `adb shell` 运行
- Ubuntu：`day1_yuv2h264/run_on_board_with_ubuntu.sh`，保持 `USE_USB=1`（默认值）
- 板子出厂的 `/usr/lib/librockchip_mpp` 太旧，要用 `/userdata/mpp_build/lib` 里的库。程序已经通过 rpath 写进了这个路径，脚本里也设置了 `LD_LIBRARY_PATH`

### 调试

- 脚本：`day1_yuv2h264/debug_on_board_via_usb.sh`
- 原理：Mac 上 `adb forward tcp:1234 tcp:1234`，板子上 `gdbserver :1234`
- 在 CLion 里：
  - 先运行脚本，看到 `Listening on port 1234`
  - 再启动 Remote Debug 配置，target 填 `localhost:1234`
  - Debug Profile 必须选 GDB（RK3588-GDB），不能用 LLDB
- 完整说明见 `day1_yuv2h264/doc/用usb连接并调试板子.md`

新写的运行或调试脚本也一样默认走 USB/adb。

## MPP 官方开发参考文档

`rk_code/external/mpp/doc/Rockchip_Developer_Guide_MPP_CN.md`（Rockchip 官方的《MPP 开发参考》）

遇到 MPP 接口、数据类型、编码参数的问题时，先查这份文档，再查 MPP 源码。主要章节：

| 章节 | 行号 | 内容 |
|---|---|---|
| 第一章 MPP 介绍 | 21 | 系统架构、平台支持、注意事项 |
| 第二章 接口设计说明 | 95 | MppBuffer、MppPacket、MppFrame、MppTask、MppCtx、MppApi |
| 3.1 – 3.3 解码器 | 296 | 解码的数据流接口、控制接口、使用要点 |
| **3.4 – 3.6 编码器** | 533 | 编码的数据流接口、控制接口（`mpp_enc_cfg` 的各项参数）、使用要点 |
| 第四章 MPP demo 说明 | 862 | mpi_dec_test、mpi_enc_test 的参数和用法 |
| 第五章 MPP 库编译与使用 | 1078 | 下载、编译 |
| 第六章 常见问题 FAQ | 1110 | 常见问题 |

## MPP 自带的测试程序（参考代码）

`mpi_*` 和 `vpu_api_test` 的源码在 `rk_code/external/mpp/test/`，其余的在 `mpp/utils`、`mpp/base`、`mpp/vproc` 等目录下的 test 里。

### ⭐ 编解码 demo（重点看这些）

| 程序 | 源码 | 作用 | 对应哪一天 |
|---|---|---|---|
| **mpi_enc_test** | test/mpi_enc_test.c | 编码：YUV 文件或摄像头 → H.264/H.265/JPEG | **Day 1**、Day 3 |
| **mpi_dec_test** | test/mpi_dec_test.c | 解码：H.264/H.265 等码流 → YUV | **Day 2** |
| mpi_dec_mt_test | test/mpi_dec_mt_test.c | 多线程解码（一个线程送数据、一个线程取结果） | Day 5 |
| mpi_dec_multi_test | test/mpi_dec_multi_test.c | 多路解码同时运行 | Day 5、以后做 NVR |
| mpi_dec_nt_test | test/mpi_dec_nt_test.c | 非阻塞（no-thread）方式解码 | 了解即可 |
| mpi_enc_mt_test | test/mpi_enc_mt_test.cpp | 多线程编码（C++） | Day 5 |
| mpi_rc2_test | test/mpi_rc2_test.c | 码率控制测试（编码后再解码，比较画质） | Day 1 晚上的实验 |
| vpu_api_test | test/vpu_api_test.c | 旧版 VPU 接口（已经过时） | 不用看 |

### 工具和信息类

| 程序 | 作用 |
|---|---|
| mpp_info_test | 打印 MPP 版本（反馈问题时附上） |
| mpp_platform_test | 读取芯片平台信息 |
| mpp_runtime_test | 检查运行环境 |
| mpp_buffer_test / mpp_dmabuf_test | 测试内存分配、DMA-BUF |

### 其他硬件模块

| 程序 | 作用 |
|---|---|
| rga_test | RGA 的简单测试（Day 4 主要还是看 `linux-rga/samples`） |
| iep_test / iep2_test | 图像增强（去隔行扫描） |
| vdpp_test | 视频显示后处理 |

## 学习方式：每个知识点只学到“够用线”

2026-11-13 用户定。目标是把 RK3588 的 MPP 弄明白，但路上的陌生知识点（V4L2、mmap、dma-buf、C++）很多，每个都往下挖会拖住主线。

**判断标准**：不懂它，能不能看懂、写出眼前这段 MPP 相关的代码？

| 知识点 | 够用线 | 先不学 |
|---|---|---|
| mmap | 把内核 buffer 映射到用户态拿到地址；失败返回 `MAP_FAILED`；不分配新内存 | 页表、VMA、缺页中断 |
| V4L2 | REQBUFS → QBUF/DQBUF 循环 → STREAMON；EXPBUF 拿 dma-buf fd | media controller 拓扑、驱动怎么写 |
| dma-buf | 一个 fd 代表一块物理内存，可以在摄像头、RGA、MPP 之间零拷贝传递 | 内核里的 attach/map 实现 |
| C++ | 能看懂、能写出 RAII、`unique_ptr` 加删除器、引用、`const` | 模板元编程、标准库内部实现 |

新碰到的知识点，先按同样的标准定够用线。

**Claude 怎么做**：
- 用户提问时，先说一句“这条在够用线以内 / 以外”
- 线以内：照常讲清楚
- 线以外：只给一句结论，在当天 `doc/我不理解的代码.md` 的“以后再挖”区记一行（日期 + 够用线结论），不展开
- 当天的主线做完后，提醒用户回清单挑一两条专门挖
- review 代码一次给全清单，分“必须改 / 建议改”，用户改完直接验证，不分多轮补

## 代码规范：名字要让入门者一眼看懂

写代码、改代码、review 用户的代码时都按这份规范。完整版（每条的理由、反例、现有代码待改清单）见 `代码规范.md`。格式交给各工程的 `.clang-format`。

> 2026-11-11 用户定下：从 Day 5 起，函数、方法用驼峰，私有成员不加结尾的 `_`。Day 1～4 已经提交的代码保持原来的 snake_case 和 `_`，不回头改。

**总原则**：读代码的是入门者。名字要说清**是什么、处于什么状态、单位是什么**，宁长勿缩。把一行代码念出来，要像一句话：`int filledBufIndex = dequeueBuffer(&bytesUsed);`

| 类别 | 写法 | 例子 |
|---|---|---|
| 文件、目录 | `snake_case` | `mpp_encoder.cpp`、`src/encoder/` |
| 类、结构体 | `PascalCase`，**名词** | `MppEncoder`、`EncoderConfig`、`CameraNode` |
| 函数、方法 | `camelCase`，**动词开头** | `queueBuffer`、`getHeader`、`readFrame` |
| 局部变量、参数 | `camelCase` | `filledBufIndex`、`bytesUsed` |
| 私有成员变量 | `camelCase`，**结尾不加 `_`** | `encoderCtx`、`encoderCfg` |
| 结构体公开字段 | `camelCase` | `horStride`、`dmaFd` |
| 常量 | `kPascalCase` | `kBufCount`、`kOutputFilePath` |
| 全局变量（只在面条代码里） | `g_camelCase` | `g_videoFd` |
| 宏 | `UPPER_SNAKE_CASE` | `V4L2_CHECK` |

**必须遵守的规则**：
- **带状态**：`filledBufIndex` 而不是 `index`；`encodedPacket` 而不是 `packet`
- **带单位**：`timeoutMs`、`dqbufWaitUs`、`sizeBytes`、`fps`
- **智能指针变量以 `Ptr` 结尾**（2026-11-12 用户定）：类型是 `std::unique_ptr` 或它的别名（`MppPacketPtr`、`MppFramePtr`、`MppBufferGroupPtr`、`FilePtr`……）的变量、成员，名字都以 `Ptr` 结尾：`MppPacketPtr packetPtr(rawPacket);`、`MppBufferGroupPtr frameGroupPtr;`。一看就知道它是把裸句柄又包了一层，用的时候要 `.get()`。裸句柄不加 `Ptr`（`rawPacket`、`MppFrame frame`）
- **固定用词**：第几个用 `Index`，一共几个用 `Count`，字节数用 `Bytes` / `Size` / `Length`；不用 `idx`、`num`、`cnt`、`n`
- **bool 用 `is` / `has` / `can` / `should` 开头**：`isStreaming`、`hasCamera`；不用否定名字（`notReady`）
- **函数名说实话**：只打印不返回的叫 `printXxx`，不叫 `getXxx`；不用 `process`、`handle`、`doWork` 这类空泛的名字
- **参数不和成员同名**：私有成员不加 `_`，参数要是和成员同名，`chunkSizeBytes = chunkSizeBytes;` 就成了参数自己赋给自己，成员一直是 0，编译还不报错。参数统一叫 `requestedXxx`（`requestedChunkSizeBytes`、`requestedConfig`）；CMake 加 `-Wshadow`，同名时编译器会警告
- **MPP、V4L2、RGA 的 API 名字保持原样**（`mpp_create`、`decode_get_frame`），只有我们自己写的函数用驼峰
- **同一个东西全工程一个名字**：`struct v4l2_buffer` 变量统一叫 `v4l2Buf`；文件描述符以 `Fd` 结尾（`videoFd`、`dmaFd`）
- **V4L2 / MPP 结构体变量用类型名去掉前缀**：`capability`、`format`、`requestBufs`、`exportBuf`、`v4l2Buf`、`bufInfo`；不用官方示例里的 `cap`、`fmt`、`req`、`exp`
- **缩写只用白名单**：`fd`、`buf`（只在词组里）、`ctx`、`cfg`、`fps`、`ret`（只接返回码），以及 `MPP`、`V4L2`、`DMA`、`ISP`、`SPS`、`PPS`、`EOS`、`NV12`、`YUV`、`UV`；其他缩写写全称
- **循环变量**：短循环、只数数时可以用 `i`；循环体长或 `i` 有含义时用 `frameIndex`、`bufIndex`
- **禁止无意义的名字**：`tmp`、`temp`、`data1`、`flag`、`caseOne`、`a`、`b`

**注释**：每个函数都写中文 Doxygen 注释（`@brief` / `@param` / `@return` / `@note`，一个都不能漏）；函数里面按流程写（`第 1 步 取`、`第 2 步 编`……），写"为什么"而不只是"是什么"；有实测的写上日期和数字。

### Claude 自己产出的代码、注释、文档里的代码块：质量要求

用户反复被 Claude 起的名字和写的注释带偏过（例：`cpuAddrs` 区分不出是哪次 mmap 得到的，后来改成 `mmapAddresses`；`exportBuf.type` 注释写成"哪个队列"，用户以为 type 就是队列，其实它是"buffer 类型"，只是内核顺带用它找队列）。所以 Claude 写或改的每个名字、每条注释，交出去之前都要过下面这几条。

**函数**
- 一个函数只做一件事，名字动词开头，说清做了什么、对谁做（`openCameraAndMapBuffers`，不叫 `init`、`setup`）
- Doxygen 四项写全：`@brief` 一句话说做什么；`@param` 标 `[in]` / `[out]` / `[in,out]`，说清单位和取值；`@return` 说清每个值的意思（`0 = 通过，1 = 失败`，`true = 成功`）；`@note` 写调用前提、谁负责释放
- 返回类型和返回值一致（`int` 函数不 `return true`）；每条失败路径都把已拿到的资源释放掉

**变量命名**
- 名字说**它是什么、从哪来**，能和同一段代码里相似的东西区分开：`mmapAddress`（mmap 的返回值）、`dmaBufFd`（EXPBUF 写回的 fd），不用 `cpuAddr`、`ptr`、`result` 这种放哪都成立的词
- 写完把名字单独拿出来问一句：**只看名字、不看上下文，入门者会不会猜错？** 会猜错就换

**注释**
- **先写字面意思，再写作用**：先说这个字段 / 值本身是什么（和头文件、官方文档的说法一致），再说它在这里起什么作用。不能只写引申出来的作用，否则读的人会把作用当成定义
  - ✗ `exportBuf.type = kBufType; // 哪个队列`
  - ✓ `exportBuf.type = kBufType; // buffer 类型（采集、多平面），内核用它找到对应的队列，必须和 REQBUFS 时一样`
- 比方（仓库、柜子、取件号）只放在文档里，并且标明"打个比方"；代码注释用准确的术语
- 注释里的数字、行号、"实测"都要真的核对过；没核对过的写"推断"
- 注释和代码不一致时，以改准为先，不能留着"差不多对"的说法
- **知识点逐行注释**（2026-10-31 用户要求）：用户还是入门者，凡是一行代码里有 V4L2、mmap、Linux 系统调用或 C/C++ 的知识点（每个参数、flag 的意思，返回值和失败时的样子，内核在背后做了什么，为什么这样写），就在这一行或上一行写注释讲清楚。例：mmap 的 6 个参数逐个说明、失败返回 `MAP_FAILED` 不是 `nullptr`、mmap 不分配新内存。用户写的代码也一样补上

**交出去之前自查**：把新写的每个名字、每条注释念一遍，对照上面几条；文档代码块里的注释也一样。发现自己以前写的名字或注释会误导人，主动指出并提议修改。

**改名的范围**：新写的代码直接按规范。已经提交的代码要改名，先跟用户确认；Day 1、Day 2 的代码不回头改，复制到新的一天时再顺手改。

## Git

- md 文档、shell 脚本和代码分开提交
- 只有用户要求时才提交或推送

## 项目记忆

学习进度、合作方式、文档风格等记忆放在本仓库的 `memory/` 目录（不放在 `~/.claude` 下）：

@memory/MEMORY.md

- 索引里每一行对应 `memory/` 下的一个文件，需要时打开对应文件看详细内容
- 进度有变化（里程碑完成、提交、换分支）时，更新 `memory/project_*.md`；新的合作约定写进 `memory/feedback_*.md`，并在 `memory/MEMORY.md` 里加一行
- 恢复工作时以 `git status`、`git log` 和 `adb devices` 的实际结果为准，记忆可能过时
