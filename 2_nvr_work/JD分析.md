# JD 分析：嵌入式音视频 / NVR / IPC 岗位要什么

> **整理日期**：2026-11-18
> **来源**：用户在招聘网站上找的 22 份 JD 截图，范围是 NVR、IPC 和其他音视频相关岗位。去掉重复的和不相关的，剩下 **18 份**。
> **用途**：判断 [README.md](README.md) 里的 NVR 学习计划有没有学偏。以后每个阶段开始前，先在这里查它对应哪几份 JD。
> **注意**：次数是读截图时手数的，个别项可能差 1。

---

## 1. 结论（先看这里）

1. **市场要的是"嵌入式音视频"，专门招 NVR、IPC 的岗位不多。** 几乎每份 JD 都要这一组：**C/C++ + 嵌入式 Linux + H.264/H.265 + 瑞芯微/海思平台 + FFmpeg + RTSP/RTP**。README 的主干（MPP → N1 RTSP 服务端 → N2 客户端 → N3 录像）正好落在这一组上，**方向没错**。
2. **有两块要求多，但计划里排得靠后或者没有：**
   - **AI 部署**（RKNN/NPU/yolo，8 份）：N10 应该提进主干。
   - **摄像头链路**（Sensor/ISP/V4L2/驱动/总线，9 份）：写驱动在够用线以外。够用线是能讲清 sensor → MIPI → ISP → V4L2 这条链，建议加一个约 1 周的小阶段。
3. **可以往后放的：** LVGL（1 份）、HLS（1 份）、HTTP-FLV（0 份）、NVR 业务功能（2 份）。ONVIF/GB28181 有 6 份，排到 HLS 前面。
4. **项目之外要另外准备的**：C++（RAII、STL、多线程）、Linux 系统编程（多进程、进程间通信、socket）、数据结构与算法。几乎每份 JD 都写了，面试一定会问。
5. **年限是硬门槛，只能靠作品抵。** 18 份里大多数要 2～3 年以上。最贴近现在水平的是 J18（1 年，点名 MPP）、J05 和 J10（接受应届生或实习生）、J02（1～3 年）。
6. **有一个备选方向**：J11、J14 要的是 **Android 底层多媒体**（OMX/Codec2、安卓多媒体框架）。这正好是"Android 背景 + 现在学的 MPP"：RK 的 Android 系统里，Codec2 组件底下调用的就是 MPP。现在不改计划，先记着。

---

## 2. 要求出现次数

| 要求 | 次数 | 哪几份 JD | 我们的计划 |
|---|:---:|---|---|
| C/C++ + 嵌入式 Linux | 18 | 全部 | 全程 |
| H.264/H.265（或泛指"编解码"） | 14 | J02 J03 J04 J05 J06 J07 J09 J10 J11 J13 J14 J15 J16 J17 | ✅ Day 1～8 |
| 瑞芯微/海思等安防芯片平台 | 10 | J01 J02 J03 J04 J05 J07 J09 J10 J15 J18 | ✅ 主线（J18 点名 MPP） |
| RTSP/RTP | 9 | J03 J04 J06 J07 J08 J09 J10 J13 J17 | N1、N2 |
| FFmpeg | 9 | J02 J03 J06 J10 J12 J13 J14 J15 J17 | ✅ Day 8，N3 |
| **Sensor/ISP/V4L2/驱动/BSP/总线** | **9** | J01 J02 J03 J04 J05 J10 J11 J14 J15 | ⚠️ 计划里没有 |
| **AI 部署（RKNN/NPU/yolo/算法移植）** | **8** | J02 J04 J05 J06 J10 J12 J15 J17 | ⚠️ N10，排在最后 |
| 多线程/多进程/socket/网络编程 | 8 | J03 J04 J06 J07 J08 J09 J12 J17 | N1、N2、N5 |
| ONVIF / GB28181 | 6 | J03 J04 J08 J09 J10 J13 | N8、N9，排得靠后 |
| 性能、内存泄漏、稳定性 | 5 | J01 J02 J04 J06 J17 | N5 |
| WebRTC | 5 | J04 J06 J13 J14 J17 | 没有（多是会议、医疗、机器人岗位） |
| 封装/存储（MP4/TS、录像） | 5 | J01 J02 J04 J09 J12 | N3 |
| 音频（AAC/Opus/采集/AEC） | 5 | J03 J04 J05 J06 J13 | Day 8 有 AAC 软解 |
| GStreamer | 4 | J06 J12 J14 J15 | 只在 Day 6 M4 用一下 |
| RTMP | 4 | J04 J07 J10 J13 | 没有 |
| 码控、GOP、编码参数 | 3 | J03 J04 J16 | N6 |
| GDB/CMake/Git 等工具链 | 3 | J02 J04 J13 | ✅ 全程在用 |
| NVR 业务功能（告警联动、网络/硬盘/设备管理） | 2 | J01 J09 | 没有 |
| MQTT/HTTP 云端通信 | 2 | J04 J06 | 没有 |
| Android 底层多媒体（OMX/Codec2） | 2 | J11 J14 | 没有（见结论 6） |
| LVGL | 1 | J01 | 没有 |
| HLS | 1 | J10 | N7 |
| HTTP-FLV | 0 | — | N7 |

**关于 Sensor/ISP/驱动那 9 份**：真正要求"写驱动"的是 J05、J11、J14、J15（J02 是加分项）。J03、J04 要的是 Sensor/ISP 的集成和调优，J01、J05、J10 要的是 I2C/SPI/UART 这些总线。

---

## 3. 每份 JD 的摘要

| 编号 | 岗位类型 | 年限 | 关键要求 |
|---|---|---|---|
| J01 | NVR + 带屏设备，LVGL 界面 | 3 年 | LVGL 界面开发和性能优化；录像存储、回放、预览、告警联动、网络与硬盘管理、参数配置；加分：海思/瑞芯微平台、外设/网络/串口 |
| J02 | 嵌入式视频处理 | 1～3 年 | C/C++、数据结构；GCC/CMake/GDB/Git；交叉编译、ARM 性能分析；H.264/H.265、FFmpeg；加分：RK/海思、AI 后处理、多路采集与存储架构、驱动/sensor 调试 |
| J03 | IPC 编解码 | 2 年 IPC | 编码参数调优、CBR/VBR、GOP 设计；VPU/ISP 集成；编码原理（帧类型、QP、码控、参考帧管理）；Linux 网络编程；加分：RTSP/SIP/RTCP/ONVIF/GB28181、瑞芯微/君正/爱芯、FFmpeg、x264/x265、ffprobe |
| J04 | 安防摄像头 C++ | 2 年 | 采集、编码、存储、网络传输；ONVIF/GB28181；配合调 Sensor/ISP/音频 Codec/Wi-Fi；内存泄漏；C++11/14、多线程、TCP/UDP/RTP/RTSP、MP4/AVI/TS；Hi35xx/RV11xx/君正 T 系列；加分：编码参数、低照度调试、RKNN/NPU |
| J05 | 安防芯片 SDK 移植 | 3 年（优秀应届可） | 芯片 SDK 对接移植，实现采集、编码、推流；AI 人脸识别、移动侦测；BSP 外设驱动（GPIO/I2S/传感器）；H.264/H.265/AAC；GPIO/I2C/SPI/I2S |
| J06 | 机器人多媒体应用 | 5 年 | GStreamer/FFmpeg pipeline；音频前处理 AEC/ANS；MQTT/WebSocket/HTTP；NPU 部署和量化；RTSP/RTP/WebRTC；多线程、进程间通信、socket；perf/valgrind/strace |
| J07 | IPC 系统架构 | 3 年 | 海思/MSTAR/君正/安霸 IPC 方案，低功耗优先；RTSP/RTMP、TCP/IP；H.264/H.265 |
| J08 | IPC 软件开发 | 3 年 | C/C++、Linux；TCP/IP 网络编程；ONVIF/PSIA/GB28181、HTTP/RTSP/SIP/FTP |
| J09 | 智能一体机 | 3～5 年 | 网络管理、设备管理、平台接入、媒体传输；编解码、封装解析、媒体 SDK；socket、Makefile、Shell、多进程多线程；海思优先；ONVIF/GB28181 或封装解封装实现优先 |
| J10 | 视频采集与图像处理 | 未写（接受实习生） | H.264/H.265 原理；yolov8 训练和部署；USB/I2C/SPI/UART；加分：海思/联咏/sigmastar/瑞芯微、IPC/NVR、多目拼接、FFmpeg/x264/OpenCV/live555、GB28181/ONVIF/RTSP/RTMP/HLS |
| J11 | 芯片 Codec 驱动 + Android Codec2 | 3 年（2 年 Codec） | 编码器驱动开发；H.264/H.265/VP9/AV1；Android OMX/Codec2 组件；Linux 驱动、RTOS、ARM |
| J12 | 嵌入式 Linux C++（车载、无人机、机器人） | 5 年 | 软件架构、中间件；相机/视频流/存储；算法移植；STL、RAII、多线程、异步；多进程、共享内存、消息队列；GStreamer/FFmpeg/DeepStream；Jetson |
| J13 | 视频会议 | 3 年 | SIP/GB28181/RTP/RTCP；Opus/AAC/H.264/VP8/VP9/H.265；x264/x265/FFmpeg；WebRTC、freeswitch、MCU/SFU；H.323 |
| J14 | Linux/安卓多媒体底层 | 2～5 年 | 编解码器、多媒体架构；硬件驱动；RTOS；安卓底层多媒体；FFmpeg/GStreamer/WebRTC |
| J15 | 音视频 pipeline + CMOS 驱动 | 未写 | pipeline 开发（输入、编解码、输出）；CMOS 驱动；产线标定；rk/海思/高通/全志任一平台的 pipeline；V4L2；GStreamer/FFmpeg；NPU 部署 |
| J16 | 编解码协议优化与测试 | 未写 | H.264/H.265/VP9/AV1/AVS2/AVS3 的理解、优化和测试 |
| J17 | 医疗超声音视频传输 | 3～5 年 | FFmpeg 编解码；WebRTC、RTP/RTCP、SDP，抖动、丢包、延迟；音视频 + AI 数据融合；多线程、网络编程 |
| J18 | 嵌入式视频处理 | 1 年 | 海思/瑞芯微视频芯片开发流程，**掌握 MPP**；Linux、Bash；固件移植和调试 |

**没算进统计的 4 份**：

- IPC 编码那份发了两次（和 J03 是同一份）
- Codec 驱动那份发了两次（和 J11 是同一份）
- 交付和运维岗：装机、接线、出厂检测，不是开发岗
- 车载音频底层：Audio DSP、AudioFlinger、车载通话，和视频方向无关

---

## 4. 对学习计划的调整建议

> 这里只是建议，**还没有改进 README**。用户确认后再改 README 的第 3、4 节。

**建议顺序**：

> N0 → **N1 RTSP 服务端 → N2 客户端 → N3 录像 → N10 AI 检测（提前）→ N5 整合** → 摄像头链路小阶段（新增）→ N6 码控 → N8 ONVIF / N9 GB28181 → N4 回放 → N7 HLS（可选）

| 改动 | 理由 |
|---|---|
| N10 AI 检测提进主干 | 8 份要求。先跑通 SDK 自带的 `external/rknpu2/examples/rknn_yolov5_demo`；J10 点名 yolov8，到时候再查 RK 有没有现成的 yolov8 示例 |
| 新增"摄像头链路"小阶段（约 1 周） | 9 份要求。不写驱动，只做到：读懂 IMX415 的设备树（`rk3588-alientek-cameras.dtsi`）、用 `media-ctl` 看链路结构、用 `i2c-tools` 读 sensor 寄存器、知道 rkaiq 的 3A 是做什么的。Day 3、Day 4 已经用过 V4L2 |
| N6 码控补"QP"和"参考帧" | J03 原文要求"帧类型、QP、码控、参考帧管理"。MPP 支持配置参考帧：`mpi_enc_test.c` 第 490 行用 `gop_mode` 生成 `rc:ref_cfg`，其中有安防常用的 smart GOP（`mpi_enc_gen_smart_gop_ref_cfg`） |
| ONVIF/GB28181 排到 HLS 前面 | 6 份对 1 份 |
| N7 HLS/HTTP-FLV 改为可选 | HLS 只有 1 份，HTTP-FLV 0 份 |
| 不加 LVGL | 只有 J01 一份。板子的默认配置里有 LVGL v9（`buildroot/configs/alientek_rk3588_defconfig` 第 25 行 `#include "lvgl/v9/lvgl_sdl.config"`），以后要用时再说 |
| 不加 WebRTC | 5 份都是会议、医疗、机器人岗位，不是安防主线 |

**还可以整理出第二个作品**：N1（板子当 IPC）+ N6（码控）+ 摄像头链路，整理成"mini IPC"，对应 J03、J04、J07、J08 这类 IPC 岗位。
