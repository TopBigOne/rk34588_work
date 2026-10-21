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
- 2026-10-15 写好 Day 3 文档（未提交时以 git status 为准）：`day3_cam2h264/readme.md`、`guide/01_环境准备.md`（装摄像头、3 条检查、找节点、M0 官方 mpi_enc_test、M1 v4l2-ctl）、`guide/02_读源码.md`（camera_source.c / mpi_enc_test.c 行号已核对）、`guide/03_写代码_M1到M6.md`。学习计划 README 的 Day 3 坑点已改正。
- 关键结论（`--try-fmt-video` 实测，未接摄像头）：video31 是 Multiplanar、1 plane；1080p NV12 bytesperline 1920、sizeimage 3,110,400 → **ver_stride 是 1080**，Day 1 `MppEncoder` 写死 ALIGN(h,16)=1088，用户选了"复制到 day3 再改"：day3_cam2h264/src/encoder/mpp_encoder.* + src/common/mpp_utils.h 已复制，EncoderConfig 加了 horStride/verStride（0 = 按 16 对齐，和 Day 1 一样）。import 进来的摄像头 buffer 是 uncached，`sync_end` 什么也不做（按源码推断）。
- 2026-10-16：用户要"能直接抄的代码"，写了 `day3_cam2h264/具体编码day3_cam2h264的流程.md`（M2/M3/M5/M6 完整 main.cpp + M4 改 3 行），代码在 scratchpad 交叉编译 0 warning、上板跑到 STREAMON（没摄像头时 `VIDIOC_STREAMON: Operation not permitted`）。实测：真正 S_FMT 后 bytesperline 1920 / sizeimage 3,110,400，但 QUERYBUF 缓冲区长度 3,133,440（高度对齐 16 分配），verStride 仍填 1080（UV 在第 1080 行，按 rkisp 驱动写法推断，M2 播放颜色可确认）。day3 编码器又加了 `rotation`（prep:rotation），init+get_header 在 stride 1920x1080、rotation 0/1/3 下上板验证通过。`src/main.cpp` 还是 Hello World，用户自己照文档抄。
- 2026-10-16：新增 `guide/04_摄像头和V4L2知识.md`（采集链路、单平面/多平面、fourcc、MMAP/USERPTR/DMABUF、轮转、mainpath/selfpath、3A）。实测：caps 0x84201000（只有 MPLANE，没有单平面 CAPTURE）；NM12 是 2 个 plane（2,073,600 + 1,036,800）；selfpath video32 最宽 1920、多 GREY/RGBP；mainpath 可到 3840x2160；REQBUFS caps 0x57（MMAP/USERPTR/DMABUF 都支持）；/etc/iqfiles 有 imx415 的 IQ 文件；没接 sensor 时 list-formats-ext 显示 800x600 上限，不可信。
- 2026-10-16：用户说 guide/04 前几节"太潦草、看不懂"（只有像素格式那节看得懂：具体格式 + 实测数字）→ 整篇按"先说人话 → 比方 → 实测 → 代码"重写，配 6 张 SVG（`day3_cam2h264/img/v4l2/`，生成脚本在 scratchpad，Chrome 渲染检查过）；用户不知道 `/dev/video31`、mainpath/selfpath 是什么 → 新增第 2 节专门讲（设备文件、节点开在 ISP 上、两个出口、两个 ISP）。**写给用户的文档不要默认他懂 Linux 设备/内核概念。**
- 2026-10-17 查底板原理图 V1.2 第 12 页：MIPI CSI1(J18)=I2C7、CSI2(J19)=I2C2、CSI3(J20)=I2C3、CSI4(J21)=I2C4（V1.0～1.2 摄像头部分没改）。当前系统镜像只配了 i2c3/i2c7 两个 IMX415 → **只能插 MIPI CSI1 或 CSI3**，建议 CSI1。快速体验手册里"四个接口都配了、video62/71/44/53"说的是出厂镜像，和现在板子上的系统不一样（现在 video 节点只到 40）。安装方向：镜头不朝向核心板，别插反插偏。装好后用 media-ctl 确认接到哪个 ISP（video22 还是 video31）。
- **2026-10-17 摄像头装好（MIPI CSI3 / J20 / I2C3），实测通过**：`Detected imx415 id 0000e0`，3864x2192 RAW10 30fps；链路 imx415(subdev2) → csi2-dphy0 → mipi-csi2 → rkcif-mipi-lvds2(media1) → **ISP0(media2) → rkisp_mainpath = /dev/video22**（不是 video31！video31 的 ISP1 没接摄像头，STREAMON 报 EPERM）。3A 在运行（加载 imx415 IQ 文件）。v4l2-ctl 从 video22 抓 1080p NV12：3,110,400 字节、30fps，Mac 上画面颜色正常 → **UV 在第 1080 行、verStride=1080 实测确认**。相机 App `/opt/ui/src/apps/camera` 会占用 video22（EBUSY）。`v4l2-ctl -z` 在这块板子上不能用（media bus info 为空），按名字找用 `media-ctl -e`。**待办：guide/01、流程文档、M2～M6 代码里默认的 /dev/video31 要改成 /dev/video22**；画面方向待用户确认（倒了就用 -r 2）。
- **2026-10-18 M2 完成**（用户自己照流程文档写的 src/main.cpp）：上板实测 open → QUERYCAP → S_FMT/G_FMT（1920x1080、bytesperline 1920、sizeimage 3110400）→ REQBUFS 4 → mmap（length 3133440）→ STREAMON → 跳 30 帧 → DQBUF（buffer 2）→ 存 3,110,400 字节，退出码 0，Mac 上画面正常。review 修掉的真 bug：`fwrite(g_bufs->start)` 永远写 buffer 0、mmap 只检查 g_bufs[0]、mmap prot 里混了 MAP_SHARED、`CAP_` 写成 `BUF_TYPE_`、`if (fd)` 判断 open。剩下的都是日志格式小问题（结尾 \n、[FAIL]、V4L2_CHECK 里 printf 没换行、重复 include）。M2 代码还没提交。下一步 M3：EXPBUF → mpp_buffer_import → 编码 1 帧。
- **2026-10-18 M3 完成**（分支 `day3_cam2h264_m3`，用户在自己的 M2 上加 EXPBUF + mpp_buffer_import + MppEncoder）：review 修掉"encode 后没 QBUF（10 帧副本实测卡死）"、clear_up 缺 put/close(dmaFd) 且用了 `if (dmaFd)`、输出名 .nv12→.h264。实测：0 warning，kFrames=500 → 8.2MB、9 I + 491 P、解码无错、连跑两次 OK；stride 1088 → 底部绿条；import 用 DRM 类型也能编（不报错）；错误退出码是 255（main 返回 -1）。剩：mpp_encoder.cpp 192～193 行 packetSize printf、日志 \n/[FAIL]、V4L2_CHECK 用 printf。画面方向见下一条（不是倒的，是侧着的）。M3 代码和验证文档都还没提交。
- **往后放（学完所有技术点再补）**：M5 里的命令行参数、帧率统计、编码耗时统计。下一步先做 prep:rotation 把画面转正。
- **2026-10-18 旋转实测**：摄像头是**侧着装的（90°）**，不是倒着（之前说 180° 是错的）；`prep:rotation = 3`（逆时针 270°）画面才正，输出自动变 1080×1920（width/height/stride 仍填摄像头的 1920×1080）。sensor flip（/dev/v4l-subdev2 的 horizontal_flip + vertical_flip）= 180°，颜色正常、影响所有出口，但转不了 90°。文档 doc/画面旋转_prep_rotation和sensor翻转.md；用户下一步自己在 main.cpp 加 `cfg.rotation = 3`。
- **2026-10-21 rotation 上板通过**：用户在 main.cpp 加了 `cfg.rotation = 3`，输出 /userdata/av/cam_m3.h264：500 帧、8,227,043 字节、ffprobe 1080x1920 High、解码无错，第 100 帧画面是正的（竖屏）。`run_move_file_from_board_via_usb.sh` 默认路径从 out.h264 改成 cam_m3.h264（未提交）。相机 App 不运行时 video22 空闲。
- 用户计划 **2026-10-17（周六）断电安装摄像头**。装好后要验证：dmesg 不再出现 Unexpected sensor id、media-ctl 能看到 imx415、`pidof rkaiq_3A_server` 有输出。
- 注意：`/dev/video31` 是 ISP 驱动建的节点，没接摄像头也存在，不能用 `v4l2-ctl --list-devices` 判断摄像头在不在。学习计划 README 的 Day 3 坑点已经改正。

**Why:** 摄像头没装之前 Day 3 的 M1 之后都跑不了，记下来避免下次又从头排查。
**How to apply:** 恢复工作时先跑上面 3 条验证命令；摄像头没装好就先做不依赖摄像头的事（Day 3 文档、读 camera_source.c、Day 4 RGA 的文件输入部分）。相关：[[project-day2-status]]
