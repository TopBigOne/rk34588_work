---
name: feedback-docs-style
description: 用户要的文档风格：详细、图文并茂（SVG 图要渲染检查）、有源码行号和板子实测数据
metadata:
  node_type: memory
  type: feedback
  originSessionId: 8f840744-0df2-415d-9dc8-43ed13063bd9
  modified: 2026-10-13T15:10:13.830Z
---

- 用户嫌"介绍太粗糙"时，要补概念解释（比如"内存池是什么"）、示意图和源码依据，不要只列结论。
- 画图：SVG 单独放在 `doc/img/<主题>/` 下，md 里用 `![](img/...)` 引用；SVG 带白色背景（深色主题也能看清）；画完用 Chrome headless 渲染成 PNG 自己看一遍，修掉文字重叠。
- 结论尽量配板子实测（写探针程序在板子上跑），没实测的明确标"按源码推断，未实测"。
- 注释要详细（用户说过"你加一下详细的方法注释，别偷懒"），用中文 Doxygen 风格。
- 速查类文档放在 `rk_work/`、`day1_yuv2h264/`、`day2_h2642yuv/` 各一份时，以 `rk_work/` 那份为准。

**Why:** 用户是通过这些文档学习的，图和实测数据能帮他真正理解；之前出现过行号写错、图文字重叠，靠渲染和核对才发现。

**How to apply:** 写文档前先读源码确认事实；写完逐条核对引用的行号和数字。相关：[[feedback-teaching-workflow]]
