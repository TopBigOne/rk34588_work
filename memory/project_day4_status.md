---
name: project-day4-status
description: 2026-11-08：M1、M2 已提交；M3 摄像头双码流上板跑通（30 fps，主 3.87 / 子 1.43 Mbps），代码还没提交；下一步 Day 5
metadata:
  type: project
---

2026-11-04 凌晨状态（分支 `day4_rga_M1`）：

- 板子 11-03 洗完、能用：`adb devices` 正常；风扇 pwm-fan 自动调速正常（满载 60 秒 42.5 °C，PWM 50 → 180）；`/dev/rga`、`/usr/lib/librga.so.2.1.0`、`/userdata/av/in_1080p_60f.nv12` 都在。
- 2026-11-06 源码已移到 `day4_rga/src/`（main.cpp、common/、encoder/、rga/），和流程文档第 3.1 节一致；CMakeLists 加了 `target_include_directories(day4_rga PRIVATE src)`。
- `encoder/mpp_encoder.h/.cpp`、`common/mpp_utils.h` 是用户**从头手写**的（不是复制 Day 3），review 后 `-Wall -Wextra` 0 warning，已提交。CMakeLists 链接了 `rockchip_mpp rga`，C++ 标准还是 20（文档写 17，暂不改）。
- 2026-11-05：用户写完 `rga/rga_resizer.h/.cpp` 和 M1 `main.cpp`，**M1 上板跑通并已提交**。实测：1920x1080（stride 1920x1088）→ 1280x720，RGA 用时 3.64～3.75 ms（第一次调用），输出 1382400 字节，md5 `d00d3d5e92a2b7dcab9e580a2b6ff6db`，和输入第 1 帧并排看画面、颜色一致。review 抓到的 bug：buffer 大小少乘 verStride（2880 字节，fread 拿到 EFAULT 悄悄失败、不崩）、函数结尾没 return exitCode。

- 2026-11-06：**M2 文件双码流上板跑通并已提交（e58194a）**。实测：60 帧，RGA 平均 2.65～2.73 ms/帧；主码流 1080p 4 Mbps → 898,624 字节（≈3.6 Mbps），子码流 720p 1.5 Mbps → 337,773 字节（≈1.35 Mbps，主码流的 38%）；子码流设成 4 Mbps 时 859,490 字节、6 Mbps 时 1,263,134 字节（几乎和主码流一样大甚至更大，说明分辨率降了码率不降就没意义）。两路 ffprobe High profile、各 60 帧、ffmpeg 完整解码无错。drain 实测取出 0 字节（没开 B 帧、put 后阻塞 get，一进一出）。review 抓到的 bug：exitCode 声明在 if 块里（编译错）、主码流 fopen 用了 "rb"、drain 里 `!= MPP_OK`、int 函数 `return false`、子码流码率写成 `4 * 1500 * 1000`；我给的 drain 示例代码省略了失败检查，用户照抄后丢了检查——讲解用的示例也要写完整。
- `day3_cam2h264/run_move_file_from_board_via_usb.sh` 会先 cd 到脚本所在目录，第 2 个参数（Mac 目录）写相对路径会落到 day3_cam2h264 下面，要写绝对路径。

- 2026-11-06 写了 `doc/1_理解M1的需求和代码.md`、`doc/1_理解M2的需求和代码.md`（含 SVG 流程图、verStride 1080 实验：顶部绿边、颜色下移 16 行；I/P 帧大小；SPS/PPS 只有开头一份的源码原因）。

- 2026-11-06 写了 `doc/2_NV12内存排布.md`（用户说 NV12 排布不太懂）。
- 2026-11-06 用户要求把 M3 完整代码写进流程文档：第 6 节已写好 `src/camera/camera_capture.h/.cpp`（CameraCapture 类，Day 3 的 V4L2 步骤收拢，去掉 mmap 和拷贝版本）+ `run_m3_dual_stream_from_camera()`。在 scratchpad 的工程副本里交叉编译通过（-Wall -Wextra 0 warning），**没上板**；用户自己照着写进 src/。当天 adb 连的是三星手机 SM-A5460，不是板子。

- **2026-11-08 M3 上板跑通**（用户照第 6 节写的 camera_capture，函数名用 camelCase、成员用 this-> 区分，没按规范改名）：300 帧 10.01 s 30.0 fps；主码流 1080x1920 4,831,990 字节（3.87 Mbps），子码流 720x1280 1,788,490 字节（1.43 Mbps，37%）；ffprobe 300 帧、完整解码无错、画面正、颜色对。review 抓到：pixelformat 判断写成 ==、EXPBUF 的 type 填成 bufIndex 且没填 index、参数和成员同名（width 写进参数）、REQBUFS 失败没 return、析构空。流程文档 6.7 已换成实测。板子 adb 序列号 3a6aff9a20ba7de4（三星手机是 R5CWA1RSV3T）。还没提交。

- 2026-11-08 用户让我重构 main.cpp（470 行）：拆成 app/（m1/m2/m3 各一个文件、app_config.h、milestones.h）、common/nv12_picture、common/time_utils.h、encoder/dual_stream_encoder（M2、M3 共用，每帧 encode_main_and_resize_to_sub → encode_sub，M3 在两步之间 QBUF）；释放改用 mpp_utils.h 的 FilePtr / MppBufferPtr / MppBufferGroupPtr。没改用户的 camelCase 方法名。上板验证：M1 md5 不变，M2 两个码流和重构前逐字节相同，M3 30 fps。M1、M2 文档的 main.cpp 行号指 603dbee，开头加了说明；NV12 文档和 SVG 改指 nv12_picture.cpp；流程文档 3.1 换成重构后的目录 + 对照表。用户把 doc 改名为 0_base_NV12…、2_理解M2…、3_理解M3…（3 是用户留的待写占位，内容是"结合 run_m3_dual_stream_from_camera 方法，完整以下文档"）。都还没提交。

- 2026-11-08 用户说 M3 文档"写的不好、别和 M1/M2 扯在一起"，按新结构重写了 doc/1、2、3 三篇（干了什么/输入/输出/技术点/业务场景/运行），行号都改指重构后的文件，去掉了 review 坑和问答。还没提交。

- 2026-11-08 已提交并 push：9758b47（M3 代码 + 重构）、497a637（文档 + 记忆）。

**下一步**：Day 5 转码器（MPP 解码 + RGA + MPP 编码）。
