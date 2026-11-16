---
name: project-day5-status
description: 2026-11-17：M2 双线程转码上板验收通过（1803 帧、225 fps、md5 和 M1 相同），代码未提交；M1 已提交（87b84e5）；下一步 M3 RTSP
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
  - 2026-11-13：MppEncoder 提交 545083d。
  - 2026-11-14：用户写完 rga_resizer、stream_file_writer、resize_encode_stage、m1_transcode_single_thread，**M1 上板验收通过**：解码 1803 帧、错误帧 0、编码 1803 帧，8670 ms（207.9 fps），58449590 → 15028188 字节；Mac 上 ffprobe hevc 1280×720 1803 帧，`ffmpeg -f null` 0 报错，文件开头是 VPS。
    - 踩过的坑：`imcheck` 是宏、展开用 memset，要 `#include <cstring>`（用户写成 `<string>`）；CMake 漏 nv12_picture.cpp；`getEncodedFrameCount` 没定义；`encode` 判断少了 `!`；**调了 MPP_ENC_GET_HDR_SYNC 必须把码流头写进文件**，否则 MPP 认为头已交给用户（mpp_enc_impl.cpp:1007 added_by_ctrl，:1540 跳过），第一个 IDR 不带 VPS/SPS/PPS，前 60 帧解不出（实测 120 条 PPS id out of range）。
    - 还没改的建议项：main.cpp 参数 `decode` 现在跑的是转码（应加 `transcode` 分支，保留 decode）；命名（`FilePtr file`→filePtr、writenBytes 拼错、bufferPtr→resizedBufPtr、resizePicture→resizedPicture、decodeFrame→decodedFrame、drain 的 30 → 常量）；app_config.h 两个 #pragma once；stream_file_writer.cpp 重复 include；空的 `// step N:`、Doxygen。
    - 这批代码还没提交。
  - 2026-11-17：用户写完 M2（frame_queue、m2_transcode_two_threads.h/.cpp，main 分 decode / m1 / m2），**上板验收通过**：1803 帧进 1803 帧出、错误帧 0，8009 ms（225.1 fps），最多排过 4 帧，输出 md5 和 M1 相同（2b8634412e7b12bfb1af066701b9e9f9）。代码还没提交。
    - review 时踩过的坑：解码出错分支 `return true`；pop 的第二个判断写成 EndOfStream（应为 Aborted）；没用 drainEncoder 的返回值；`double isDecodeOk`。都已改。
    - 2026-11-17 用户让 Claude 直接补完：-Wshadow 挪到编译选项并加注释、Threads::Threads（工具链 libc 自带 pthread，不加也能链接）、frame_queue / m2 / main 的 Doxygen 和步骤注释、app_config 的队列容量注释。重新编译 0 警告，再上板 1803 帧、228.1 fps、md5 不变。
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
