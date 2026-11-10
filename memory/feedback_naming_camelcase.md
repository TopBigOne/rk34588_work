---
name: feedback-naming-camelcase
description: 2026-11-11 用户定：Day 5 起函数和方法用驼峰，私有成员不加结尾 _；参数叫 requestedXxx，CMake 加 -Wshadow
metadata:
  type: feedback
---

从 Day 5 起：函数、方法一律 camelCase（`getFrame`、`readChunk`、`makeAlignedNv12Picture`），私有成员也是 camelCase，**结尾不加 `_`**（`decoderCtx`）。MPP、V4L2、RGA 的 API 名字保持原样。

**Why:** 用户 review 时明确说"我就是要用驼峰命名"、"私有成员不要结尾的 _"。

**How to apply:**
- 已经写进 CLAUDE.md 的命名表和 `代码规范.md`；m1～m4 流程文档的代码、3 张 SVG 都已改成驼峰（参考工程 M1、M2 两种状态都用 `-Wall -Wextra -Wshadow` 交叉编译过，0 警告）
- 不加 `_` 后，参数和成员同名会让 `x = x;` 变成参数自己赋给自己（编译不报错）。所以参数不和成员同名，统一叫 `requestedXxx`；CMake 加 `-Wshadow`。即使写了 `this->`，`-Wshadow` 也照样警告，所以不靠 `this->`
- review 用户代码时不再提"函数要 snake_case""成员要加 _"
- Day 1～4 已提交的代码保持 snake_case 函数和 `camelCase_` 成员，不回头改

相关：[[feedback-code-quality-naming-comments]]、[[project-day5-status]]
