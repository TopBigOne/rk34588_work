---
name: feedback-commit-msg-prefix
description: 提交信息用当天工程名作前缀，例如 "day3_cam2h264: xxx"，不要用 "3:" 这种数字前缀
metadata:
  type: feedback
---

用户 2026-10-17 指出："你的 commit msg 应该用 day3_cam2h264 作为前缀"。

**Why:** 一个仓库里有好几天的工程（day1_yuv2h264、day2_h2642yuv、day3_cam2h264……），用工程名做前缀，看 git log 时一眼就知道是哪一天、哪个工程的改动；之前沿用的 "3:" 看不出是什么。

**How to apply:**
- 格式：`<工程名>: <说明>`，例如 `day3_cam2h264: 新增 FindCamera ...`；工程名就是当天的目录名
- 改动跨工程的（比如只改 memory/、CLAUDE.md），用当前分支对应的工程名
- 说明里不要再重复工程名（不要写成 `day3_cam2h264: day3 复制 ...`）
- 2026-10-17 已经把 day3_cam2h264 分支上 15 个 "3:" 开头的本地提交改成了这个格式（用 commit-tree 只改信息，内容、作者、时间不变；改之前的备份分支 `backup/day3_before_msg_prefix`）
- 其他规矩不变：md 文档、脚本、代码分开提交；按功能分组；只有用户要求时才提交或推送（见 CLAUDE.md）
