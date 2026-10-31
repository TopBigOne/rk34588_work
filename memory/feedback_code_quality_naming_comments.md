---
name: feedback-code-quality-naming-comments
description: Claude 写的名字和注释多次误导用户；规则已写进 CLAUDE.md "Claude 自己产出的代码…质量要求" 一节
metadata:
  type: feedback
---

2026-10-31 用户明确要求：Claude 产出的代码要函数规范、变量注释准确、变量名见名知意。"已经出现很多次了，你的命名和注释误导了我的理解。"

**Why:** 实际误导过的例子：
- `cpuAddrs`：所有指针都是 CPU 地址，区分不出是哪次 mmap 得到的，用户看不懂 → 改成 `mmapAddresses`
- `exportBuf.type` 的注释写成"输入：哪个队列"：用户以为 type 就是队列。它其实是 buffer 类型（采集、多平面），内核只是顺带用它找队列

**How to apply:** 具体规则在 CLAUDE.md 的"Claude 自己产出的代码…质量要求"一节，交出代码前按它自查：
- 注释先写字面意思，再写作用
- 名字说"是什么、从哪来"
- 比方只放文档里
- 没核对过的写"推断"

文档代码块里的注释也要按这个查。

同一天用户追加要求：自己是小白，凡是含 V4L2 / mmap / Linux 知识点的代码行都要写注释。day3_mmap 的 src/ 已经全部补过一遍。和 [[feedback-docs-style]]、[[feedback-teaching-workflow]] 相关。
