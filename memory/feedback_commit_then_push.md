---
name: feedback-commit-then-push
description: 2026-11-04 用户说"提交了以后就直接 push"：用户让我提交时，提交完紧接着 push 到 origin 当前分支，不用再问
metadata:
  type: feedback
---

2026-11-04 用户说："你提交了以后，就直接 push 代码吧"。

**Why:** 用户不想每次提交后再单独说一遍 push，代码要及时备份到 GitHub（origin = git@github.com:TopBigOne/rk34588_work.git）。

**How to apply:**
- 只在用户要求提交时才提交（CLAUDE.md 的规则不变）；提交完立刻 `git push`，新分支用 `git push -u origin <分支名>`。
- 不 force push；push 失败（比如远端有新提交）就停下来告诉用户，不自己 rebase 或强推。
- 提交前缀用用户指定的，例如 `[day4_rga_M1]`，见 [[feedback-commit-msg-prefix]]。
