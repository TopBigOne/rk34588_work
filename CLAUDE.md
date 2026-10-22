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

## 代码规范：名字要让入门者一眼看懂

写代码、改代码、review 用户的代码时都按这份规范。完整版（每条的理由、反例、现有代码待改清单）见 `代码规范.md`。格式交给各工程的 `.clang-format`。

**总原则**：读代码的是入门者。名字要说清**是什么、处于什么状态、单位是什么**，宁长勿缩。把一行代码念出来，要像一句话：`int filledBufIndex = dequeue_buffer(&bytesUsed);`

| 类别 | 写法 | 例子 |
|---|---|---|
| 文件、目录 | `snake_case` | `mpp_encoder.cpp`、`src/encoder/` |
| 类、结构体 | `PascalCase`，**名词** | `MppEncoder`、`EncoderConfig`、`CameraNode` |
| 函数、方法 | `snake_case`，**动词开头** | `queue_buffer`、`get_header`、`read_frame` |
| 局部变量、参数 | `camelCase` | `filledBufIndex`、`bytesUsed` |
| 私有成员变量 | `camelCase_` | `encoderCtx_`、`cfg_` |
| 结构体公开字段 | `camelCase` | `horStride`、`dmaFd` |
| 常量 | `kPascalCase` | `kBufCount`、`kOutputFilePath` |
| 全局变量（只在面条代码里） | `g_camelCase` | `g_videoFd` |
| 宏 | `UPPER_SNAKE_CASE` | `V4L2_CHECK` |

**必须遵守的规则**：
- **带状态**：`filledBufIndex` 而不是 `index`；`encodedPacket` 而不是 `packet`
- **带单位**：`timeoutMs`、`dqbufWaitUs`、`sizeBytes`、`fps`
- **固定用词**：第几个用 `Index`，一共几个用 `Count`，字节数用 `Bytes` / `Size` / `Length`；不用 `idx`、`num`、`cnt`、`n`
- **bool 用 `is` / `has` / `can` / `should` 开头**：`isStreaming`、`hasCamera`；不用否定名字（`notReady`）
- **函数名说实话**：只打印不返回的叫 `print_xxx`，不叫 `get_xxx`；不用 `process`、`handle`、`do_work` 这类空泛的名字
- **同一个东西全工程一个名字**：`struct v4l2_buffer` 变量统一叫 `v4l2Buf`；文件描述符以 `Fd` 结尾（`videoFd`、`dmaFd`）
- **V4L2 / MPP 结构体变量用类型名去掉前缀**：`capability`、`format`、`requestBufs`、`exportBuf`、`v4l2Buf`、`bufInfo`；不用官方示例里的 `cap`、`fmt`、`req`、`exp`
- **缩写只用白名单**：`fd`、`buf`（只在词组里）、`ctx`、`cfg`、`fps`、`ret`（只接返回码），以及 `MPP`、`V4L2`、`DMA`、`ISP`、`SPS`、`PPS`、`EOS`、`NV12`、`YUV`、`UV`；其他缩写写全称
- **循环变量**：短循环、只数数时可以用 `i`；循环体长或 `i` 有含义时用 `frameIndex`、`bufIndex`
- **禁止无意义的名字**：`tmp`、`temp`、`data1`、`flag`、`caseOne`、`a`、`b`

**注释**：每个函数都写中文 Doxygen 注释（`@brief` / `@param` / `@return` / `@note`，一个都不能漏）；函数里面按流程写（`第 1 步 取`、`第 2 步 编`……），写"为什么"而不只是"是什么"；有实测的写上日期和数字。

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
