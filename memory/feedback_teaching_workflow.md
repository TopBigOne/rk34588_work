---
name: feedback-teaching-workflow
description: 学习项目的分工方式：我写文档/方案并 review，用户写 MPP 核心代码；先面条代码再重构；用 md5 上板验证
metadata:
  node_type: memory
  type: feedback
  originSessionId: 8f840744-0df2-415d-9dc8-43ed13063bd9
  modified: 2026-10-13T15:10:07.176Z
---

每个 Day 的工作方式（用户在 Day 2 开始时选定，"和 Day 1 一样"）：
- 我写文档（readme、guide、M1～M6 流程文档、重构方案），用户写代码，每个里程碑写完我 review。
- 先在一个文件里写面条代码（goto CLEANUP 风格）跑通 M1～M6，再单独开分支重构成类（照 Day 1 的目录结构：app / common / decoder|encoder / source / sink / pipeline）。
- 用户说"快速推进，只要不是 bug 就行"时，review 只报 bug，不报代码质量问题。
- 后来调整为：**MPP 核心流程（init、info change、主循环、取帧判断）用户自己写/手抄；样板代码（参数解析、CLEANUP、统计）可以由我写**。Day 2 重构代码是用户让我直接写的，然后给了"哪些手抄、哪些精读、哪些扫一眼"的清单。

- **2026-10-18 用户明确说**：命令行参数解析、帧率 / 耗时统计这类"死知识点"、编解码里很小的分支，**先不学，等所有技术点都学完再回头细学**（不是不学）。现在排里程碑时先跳过它们（常量写死；真需要时我直接写好），时间放在核心机制上：零拷贝 / 内存、V4L2 和 MPP 的交互、stride、**旋转（prep:rotation，用户确认要学）**、多路码流、性能对比。把跳过的这些记在 Day 3 进度里，最后统一补。

**Why:** 用户的目标是真正理解系统（能独立写出 MPP 编解码器），不是只要能跑的代码；同时不想在样板代码上浪费时间。

**How to apply:**
- review 时先编译（-Wall -Wextra），能连板子就上板跑，用 md5 和官方 `mpi_dec_test` / `mpi_enc_test` 的输出对比，结论要有实测或源码行号依据。
- 引用源码行号、错误码、Day 1 的事实前先核对（曾经把 -2 错说成 MPP_ERR_NULL_PTR，实际是 MPP_ERR_UNKNOW）。
- 用户不想学的时候别施压；进度都在 git 里，随时能接着做。
- 文档风格见 [[feedback-docs-style]]。
