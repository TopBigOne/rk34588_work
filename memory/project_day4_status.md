---
name: project-day4-status
description: 2026-11-05：day4_rga_M1 分支，M1（RGA 1080p→720p，3.5 ms）上板跑通并已提交；下一步 M2 文件双码流
metadata:
  type: project
---

2026-11-04 凌晨状态（分支 `day4_rga_M1`）：

- 板子 11-03 洗完、能用：`adb devices` 正常；风扇 pwm-fan 自动调速正常（满载 60 秒 42.5 °C，PWM 50 → 180）；`/dev/rga`、`/usr/lib/librga.so.2.1.0`、`/userdata/av/in_1080p_60f.nv12` 都在。
- 目录没有按流程文档建 `src/`，而是 `day4_rga/common/`、`day4_rga/encoder/` 直接放在工程根下。
- `encoder/mpp_encoder.h/.cpp`、`common/mpp_utils.h` 是用户**从头手写**的（不是复制 Day 3），review 后 `-Wall -Wextra` 0 warning，已提交。CMakeLists 链接了 `rockchip_mpp rga`，C++ 标准还是 20（文档写 17，暂不改）。
- 2026-11-05：用户写完 `rga/rga_resizer.h/.cpp` 和 M1 `main.cpp`，**M1 上板跑通并已提交**。实测：1920x1080（stride 1920x1088）→ 1280x720，RGA 用时 3.64～3.75 ms（第一次调用），输出 1382400 字节，md5 `d00d3d5e92a2b7dcab9e580a2b6ff6db`，和输入第 1 帧并排看画面、颜色一致。review 抓到的 bug：buffer 大小少乘 verStride（2880 字节，fread 拿到 EFAULT 悄悄失败、不崩）、函数结尾没 return exitCode。
- M2 要用的 `drain_encoder` 第 118 行还是 `encoder.encode(...) != MPP_OK`（bool 和 MPP_OK 比，成功会被当失败；第 98 行 init 已改成 `!encoder.init(...)`），写 M2 时先改成 `!encoder.encode(...)`。

**下一步**：用 M1 实测替换流程文档第 4 节标 ⚠️ 的期望输出；然后 M2 文件双码流。

**How to apply:** 恢复工作时先看 `git log`、`git status`；用户写完代码让我 review 时，按 [[feedback-teaching-workflow]] 先用交叉编译器加 -Wall -Wextra 编一遍。
