---
name: project-day4-status
description: 2026-11-06：day4_rga_M1 分支，M1（RGA 缩放）已提交；M2 文件双码流跑通（子码流 1.5 Mbps = 主码流 38%）还没提交；源码已移到 src/
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

**下一步**：（流程文档 ⚠️ 期望输出已在 11-06 换成实测）M3（摄像头双码流）等摄像头装回。

**How to apply:** 恢复工作时先看 `git log`、`git status`；用户写完代码让我 review 时，按 [[feedback-teaching-workflow]] 先用交叉编译器加 -Wall -Wextra 编一遍。
