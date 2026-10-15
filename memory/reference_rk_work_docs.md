---
name: reference-rk-work-docs
description: rk_work 里关键文档、板子上参考程序和会话记录的位置
metadata:
  node_type: memory
  type: reference
  originSessionId: 8f840744-0df2-415d-9dc8-43ed13063bd9
  modified: 2026-10-13T15:10:27.585Z
---

- 总速查：`rk_work/MPP 编解码速查表.md`（编码 6 段 + 解码 4 段骨架、& 规律、返回值、19 条常见坑、板子命令、文档索引）。
- Day 2 文档：`day2_h2642yuv/具体写day2_h2642yuv的流程.md`（M1～M6）、`guide/03_重构成类.md`、`doc/decode_get_frame拿到的几种帧.md`、`doc/mpp_buffer_group_limit_config.md`（含 4 张 SVG + limit 实测）、`doc/MPP解码流程骨架.md`。
- 板子上：官方 demo `/userdata/mpp_build/bin/mpi_dec_test`（库在 `/userdata/mpp_build/lib`），测试素材 `/userdata/av/aaa.264`（1920x1080 Baseline，1803 帧，DPB 4）。
- Mac 上：素材 `~/Documents/AV/rk_test_data/aaa.264`；ffmpeg/ffplay 在 `/usr/local/ffmpeg/4.4/bin`；交叉编译工具链 `~/rk-toolchain/rk3588-toolchain.cmake`（编译器经 OrbStack 的 rk-dev 转发）。
- 本次长会话的完整记录：`~/.claude/projects/-Users-dev-Documents-AV-rk-work/8f840744-0df2-415d-9dc8-43ed13063bd9.jsonl`（约 41 MB）。

相关：[[project-day2-status]]
