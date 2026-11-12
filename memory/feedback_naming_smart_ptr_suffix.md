---
name: feedback-naming-smart-ptr-suffix
description: 2026-11-12 用户定：unique_ptr 及其别名（MppPacketPtr 等）类型的变量、成员名字以 Ptr 结尾
metadata:
  type: feedback
---

类型是 `std::unique_ptr` 或其别名（`MppPacketPtr`、`MppFramePtr`、`MppBufferPtr`、`MppBufferGroupPtr`、`FilePtr`）的变量、成员、参数，名字以 `Ptr` 结尾：`MppPacketPtr packetPtr(rawPacket);`、`MppBufferGroupPtr frameGroupPtr;`。裸句柄不加。

**Why:** 用户说"这样我就知道它是把 MppPacket 又封装了一层"。同一段代码里裸句柄和智能指针并存，名字一样就分不清哪个自动释放、哪个要 `.get()`。

**How to apply:**
- 已写进 CLAUDE.md 必须遵守的规则和 `代码规范.md` 3.11
- 新写的代码、流程文档里的代码块都按这条；review 用户代码时检查
- 2026-11-12 当时不符合的地方：`src/decoder/mpp_decoder.cpp` 的 `packet`、`frame`，`src/pipeline/decode_stage.cpp` 的 `outputFrame`，m1/m2 流程文档里一批（`file`、`frameGroup`、`packet`、`frame`、`outputFrame`、`decodedFrame`、`bufferGroup`、`resizedBufOwner`）。已提交的代码改名要先问用户

相关：[[feedback-naming-camelcase]]、[[feedback-code-quality-naming-comments]]
