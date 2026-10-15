---
name: project-day3-status
description: Day 3（摄像头编码）还没开始：板子上没检测到 IMX415，用户计划 2026-10-17（周六）装摄像头
metadata:
  type: project
---

截至 2026-10-15：

- Day 2 已读完（b8825fd），下一步是 Day 3 `day3_cam2h264`（IMX415 → V4L2 → DMA-BUF 零拷贝 → MPP 编码）。
- **分支 `day3_cam2h264`**：用户从 `main_v2`（cff8038）拉出来，**明确要求不要和 `day2_h2642yuv_most_light_refactor` 有关系**（不 merge、不 rebase、不快进）。需要 Day 2 分支上的东西时，只用 `git checkout <分支> -- <路径>` 取文件内容、单独提交（memory/ 和 CLAUDE.md 就是这样拿过来的，f49ccbb）。
- 2026-10-15 工程按 Day 1 配好：`src/main.cpp`（还是 Hello World）、CMakeLists.txt（C++17、include src、链接 rockchip_mpp、rpath /userdata/mpp_build/lib）、Day 1 的 .clang-format、3 个 adb 脚本（照 Day 2 的版本，adb 直接用 PATH 里的）。交叉编译和上板运行验证过。CLion 里的 RK3588-Debug profile 要用户自己加。
- **板子上没有摄像头**：dmesg 里 `imx415 3-001a` 和 `imx415 7-001a` 都是 `Unexpected sensor id(000000), ret(-5)`（I2C 没有应答）；`media-ctl -p` 里没有 imx415 实体；`rkaiq_3A_server` 开机启动过（/etc/init.d/S40rkaiq_3A），因为 `Bad media topology` 退出了。
- 用户计划 **2026-10-17（周六）断电安装摄像头**。装好后要验证：dmesg 不再出现 Unexpected sensor id、media-ctl 能看到 imx415、`pidof rkaiq_3A_server` 有输出。
- 注意：`/dev/video31` 是 ISP 驱动建的节点，没接摄像头也存在，不能用 `v4l2-ctl --list-devices` 判断摄像头在不在。学习计划 README 的 Day 3 坑点里那句要改正（写 Day 3 文档时一起改）。

**Why:** 摄像头没装之前 Day 3 的 M1 之后都跑不了，记下来避免下次又从头排查。
**How to apply:** 恢复工作时先跑上面 3 条验证命令；摄像头没装好就先做不依赖摄像头的事（Day 3 文档、读 camera_source.c、Day 4 RGA 的文件输入部分）。相关：[[project-day2-status]]
