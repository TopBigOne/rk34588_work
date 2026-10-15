---
name: project-day2-status
description: 截至 2026-10-15 Day 2 解码的进度：M1～M6 完成，重构代码已提交并验收通过，用户已读完重构代码（b8825fd）
metadata:
  node_type: memory
  type: project
  originSessionId: 8f840744-0df2-415d-9dc8-43ed13063bd9
  modified: 2026-10-13T15:10:35.972Z
---

截至 2026-10-15：

- **分支 `day2_h2642yuv_most_light`**：M1～M6 面条代码完成（`src/mpi_dec_most_light.cpp`），最后一个提交 `782789a`；本地比 origin 多 6 个提交，**都没推送**。验收结果：`-n 60 -o` 写出 186,624,000 字节、md5 `5aea639a616d92bb8ef8af809eee5218`（和官方 mpi_dec_test -n 60 一致）；不带参数解完 1803 帧走到 EOS，约 900 fps；`-t vp8` 退出码 255。
- **分支 `day2_h2642yuv_most_light_refactor`（当前）**：
  - 已提交：`1b1d2ca` 文档（MPP 编解码速查表 ×3、doc/MPP解码流程骨架.md、guide/03_重构成类.md、readme）；`f5fdc12` 项目记忆 memory/ + CLAUDE.md；`ac3316a` **重构代码**（src/main.cpp + app/、common/、decoder/MppDecoder、source/ReadStream、sink/WriteYuv、pipeline/DecodeStats + DecodePipeline，CMakeLists.txt）。
  - **2026-10-13 上板验收通过**：`-n 60 -o` md5 `5aea639a…`、不带参数 1803 帧走到 EOS（约 900 fps）、`-t vp8` / 输入不存在 / 输出不可写都退出码 255 且不崩溃。
  - 这个分支还没有远端分支，所有提交都只在本机。
  - 验收后：用户按"手抄（init、handle_info_change、run、drain_frames）/ 精读（get_frame 所有权、析构顺序、feed、= delete）/ 扫一眼（args、utils、读写、stats）"清单学习重构代码。
  - `090c092` 方法注释统一成 Doxygen 格式。
  - **2026-10-15 用户读完 day2 代码**，提交 `b8825fd`：两处 unique_ptr::reset 的 todo 换成说明注释。读代码时追问过的点：reset 实际是调用删除器释放旧资源、unique_ptr 第二个模板参数（删除器）、`vector<uint8_t>&` 为什么不是 `vector<uint8_t*>`。
- 2026-10-15：day2 readme 进度表 M1～M6 和重构都已改成 ✅（实验仍是 ⬜）；学习计划 README 里 Day 2 的工程名已改成实际的 `day2_h2642yuv`。
- **之后**：开始 Day 3（`day3_cam2h264`，分支沿用 `_most_light` / `_most_light_refactor` 命名），见 [[project-day3-status]]。
- 旧分支 `main_v2` 上有 Day 1 和 Day 2 的裁剪版 demo（`cff8038`）。

**Why:** 会话可能丢失，记下断点方便接着做。
**How to apply:** 恢复时先 `git status` 和 `adb devices` 确认实际状态，以 git 为准。相关：[[reference-rk-work-docs]]
