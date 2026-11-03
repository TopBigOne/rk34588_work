---
name: project-day4-status
description: 2026-11-04：day4_rga 分支 day4_rga_M1，mpp_encoder.h/.cpp 用户手写完、review 过、0 warning 已提交；rga_resizer 和 M1 main.cpp 还没写
metadata:
  type: project
---

2026-11-04 凌晨状态（分支 `day4_rga_M1`）：

- 板子 11-03 洗完、能用：`adb devices` 正常；风扇 pwm-fan 自动调速正常（满载 60 秒 42.5 °C，PWM 50 → 180）；`/dev/rga`、`/usr/lib/librga.so.2.1.0`、`/userdata/av/in_1080p_60f.nv12` 都在。
- 目录没有按流程文档建 `src/`，而是 `day4_rga/common/`、`day4_rga/encoder/` 直接放在工程根下。
- `encoder/mpp_encoder.h/.cpp`、`common/mpp_utils.h` 是用户**从头手写**的（不是复制 Day 3），review 后 `-Wall -Wextra` 0 warning，已提交。CMakeLists 链接了 `rockchip_mpp rga`，C++ 标准还是 20（文档写 17，暂不改）。
- `main.cpp` 还是 Hello World 模板。

**下一步**：写 `rga/rga_resizer.h/.cpp` 和 M1 的 `main.cpp`（流程文档第 4 节），上板跑，用实测替换文档里标 ⚠️ 的期望输出。然后 M2 文件双码流。

**How to apply:** 恢复工作时先看 `git log`、`git status`；用户写完代码让我 review 时，按 [[feedback-teaching-workflow]] 先用交叉编译器加 -Wall -Wextra 编一遍。
