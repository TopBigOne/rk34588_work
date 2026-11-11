---
name: project-day5-status
description: 2026-11-12：M1 解码段上板通过（1803 帧、错误帧 0、stride 1920×1088）；DecodeStage 和解码测试未提交；下一步写 RGA、编码器
metadata:
  type: project
---

- 分支：`day5_transcoder`（2026-11-11 第一次 push，已设 upstream）。
- 2026-11-11 进度：用户照 m1 手写了 common/（log、mpp_utils、nv12_picture、time_utils）、source/StreamFileReader、decoder/MppDecoder，review 后提交 240ce69，交叉编译加链接通过（-Wall -Wextra -Wshadow 0 警告）。main 还是 Hello World，app_config.h 等还是空壳。
  - review 时用户踩过的坑：MPP 句柄是 void*，`mpp_init(&ctx)`、`MppDecCfg *` 多写 `&`/`*` 编译器不报；少了 `mpp_dec_cfg_init`；`getReadBytes(){}` 没 return；CMake 没链接 rockchip_mpp。
  - 用户的问题整理在 `day5_transcoder/doc/我不理解的代码.md`（按值返回局部变量、`&decoderCfg` 二级指针、`chunk.resize`、vector 的 size 和 fread）。
  - 用户代码里还没改的小问题：`reqChunkSizeBytes` 是缩写（应为 requestedChunkSizeBytes，.h 声明也要改）；CMake 还是 C++17、没加 -Wshadow；多数函数 Doxygen 只写了一半；mpp_decoder.cpp 的 `// step N:` 后面是空的。
  - 2026-11-12：用户写完 pipeline/DecodeStage 和 app/m1_decode_only_check.cpp（main 参数 `decode`），**上板通过**：正常帧 1803、错误帧 0、读入 58449590 字节（= Mac 上 aaa.264 的大小）、info change 1920x1080 stride 1920x1088 buf_size 4177920、第 1 帧 fmt 0x0。m1 文档和 m1_one_frame_buffers.svg 的 stride 已改成实测。这批代码还没提交。
  - 用户在 MppDecoder::getFrame 里加了调试日志（`decode nothing` 每次轮询打一行 LOGE），一次运行 1 万多行，把 stride 日志挤出了 CLion 窗口；建议删掉。
  - 下一步：rga_resizer → mpp_encoder → stream_file_writer → resize_encode_stage → m1_transcode_single_thread。
- 文档：`day5_transcoder/day5编码流程/m1流程.md`～`m4流程.md`。原来的 `具体编码day5_transcoder的流程.md` 已经拆进这 4 份，删掉了。
  - m1：Day 5 架构（4 层、两个 Stage）+ M1 的需求、技术点 + 3.6 节全部 25 个文件的完整代码。
  - m2：FrameQueue（mutex + 两个条件变量、容量 4、close_input / abort）+ m2_transcode_two_threads.cpp 的完整代码，以及改动的 4 个文件。FrameQueue 在 Mac 上用假帧类型测过（顺序、背压、两边叫停）。输出 out_720p_m2.h265，预计和 M1 的 md5 一样（推断）。
  - m3：run_rtsp_server_via_usb.sh（adb forward tcp:8554 → 板子 554，live555 优先用板子自带的）+ ffplay -rtsp_transport tcp。live555 按扩展名 .265 识别 H.265，所以要建 out_720p.265 链接（推断 .h265 认不出）。
  - m4：Wireshark 抓 lo0、tcp.port == 8554，看 4 个命令和 RTP 包头。报文是示例，没实测。
  - 用户说"我不想参考 day4"，所以代码在文档里写全，不让用户翻前几天的代码。
- M1 参考代码的情况：
  - 2026-11-08 在 scratchpad 里建过同样的工程，交叉编译通过，`-Wall -Wextra` 没有警告。
  - **还没上板跑过**：当时 adb devices 为空。stride 1920×1088、1803 帧都要等上板确认。
- 和 Day 4 的区别：
  - `EncoderConfigArg` 改名成 `EncoderConfig`，`type` 改名成 `codingType`
  - 2026-11-11 按用户要求全部改成驼峰（函数、方法），私有成员不加 `_`，参数叫 requestedXxx，见 [[feedback-naming-camelcase]]
  - 修了 Day 4 `init` 重复调用时打了错误日志却不 return 的 bug
- 下一步：用户照 m1 敲代码；我 review、上板验证，然后依次做 M2、M3、M4。上板时把文档里所有标"推断"的地方核对一遍。
- 相关：[[feedback-architecture-first]]、[[project-day4-status]]
