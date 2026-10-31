---
name: feedback-experiment-design
description: 设计实验前先确认能看到明显对比，并先说清"学完能回答哪个问题"；M3 sync 实验两边都≈0，用户觉得没学到东西
metadata:
  type: feedback
---

2026-10-31 M3（DMA_BUF_IOCTL_SYNC 计时）跑完，用户说"我都不知道学了啥"。

原因：实验设计有缺陷。buffer 没交给过任何硬件，cached 的 sync 也≈0.001 ms，看不到预期的对比；只剩 memset 差 2 倍，结论模糊。

**Why:** 用户是入门者，靠"现象明显不同 → 追问为什么"来学。现象不明显，就学不到东西。

**How to apply:**
- 写实验文档前，先说清"跑完你能回答哪个问题"（一句话）。
- 先自己上板 probe 一下，确认关键现象确实有明显差别，再交给用户写。
- 跑完以后用 3 句话总结"学到了什么、以后哪里会用到"。

相关：[[feedback_teaching_workflow]]
