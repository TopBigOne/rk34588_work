---
name: feedback-token-budget
description: 2026-11-20 用户嫌 token 消耗太大：不并行开多个子助手，先估算再做，一次一个阶段
metadata:
  type: feedback
---

不要同时开多个子助手；大任务先给出 token 估算，用户同意后再做，一次只做一个阶段 / 一个子助手。

**Why:** 2026-11-20 夜里 Claude 一次开了 5 个计划子助手、接着又开 7 个，每个约 20 万 token，撞了组织月度消费上限，全部中断，用户说“太消耗 token 了”。主会话上下文过长时，每次工具调用都重读整段上下文，也很贵。

**How to apply:** 子助手的说明压到最短（只读必要文件、不整段贴代码）；每做完一个报一次实际 token；对话变长时主动建议换新会话，进度写进 memory 和文件。相关：[[project-n0-status]]
