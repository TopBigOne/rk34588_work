---
name: project-day2-status
description: 截至 2026-10-13 Day 2 解码的进度：M1～M6 完成，重构代码已写好但还没上板验收、没提交
metadata:
  node_type: memory
  type: project
  originSessionId: 8f840744-0df2-415d-9dc8-43ed13063bd9
  modified: 2026-10-13T15:10:35.972Z
---

截至 2026-10-13：

- **分支 `day2_h2642yuv_most_light`**：M1～M6 面条代码完成（`src/mpi_dec_most_light.cpp`），最后一个提交 `782789a`；本地比 origin 多 6 个提交，**都没推送**。验收结果：`-n 60 -o` 写出 186,624,000 字节、md5 `5aea639a616d92bb8ef8af809eee5218`（和官方 mpi_dec_test -n 60 一致）；不带参数解完 1803 帧走到 EOS，约 900 fps；`-t vp8` 退出码 255。
- **分支 `day2_h2642yuv_most_light_refactor`（当前）**：
  - 已提交：`1b1d2ca` 文档（MPP 编解码速查表 ×3、doc/MPP解码流程骨架.md、guide/03_重构成类.md、readme）。
  - **未提交**：我写好的重构代码（src/main.cpp ← 由 mpi_dec_most_light.cpp git mv 而来，app/、common/、decoder/MppDecoder、source/ReadStream、sink/WriteYuv、pipeline/DecodeStats + DecodePipeline，CMakeLists.txt）。编译 0 warning，但**板子断开，还没上板验收**。用户之前把部分代码文件预先暂存了。
  - 下一步：板子插上后跑 3 条验收（md5、1803 帧 EOS、vp8 → 255），通过后单独提交代码；然后用户按"手抄 / 精读 / 扫一眼"清单学习重构代码。
- **之后**：readme 进度表 M1～M6 还都是 ⬜ 没更新；然后开始 Day 3（day3_cam2h264）。
- 旧分支 `main_v2` 上有 Day 1 和 Day 2 的裁剪版 demo（`cff8038`）。

**Why:** 会话可能丢失，记下断点方便接着做。
**How to apply:** 恢复时先 `git status` 和 `adb devices` 确认实际状态，以 git 为准。相关：[[reference-rk-work-docs]]
