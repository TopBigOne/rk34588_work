---
name: feedback-architecture-first
description: 2026-11-08 起（Day 5 开始）不再先写面条代码，一开始就按 Day 4 重构后的分层目录写，要有架构思维
metadata:
  type: feedback
---

用户 2026-11-08 说："day5 开始，不要写面条代码了；参考 day4（src/app、camera、common、encoder、rga、main.cpp），注意要有架构思维"。

这条取代 [[feedback-teaching-workflow]] 里"先在一个文件里写面条代码，跑通后再重构"的做法。

**Why:** Day 1～4 都是先写面条代码再重构，重构要再花一轮时间。用户想从一开始就练习怎么拆模块。

**How to apply:**
- 新的一天先设计目录和分层，写进流程文档，用户认可后再写代码：
  - 目录：app / common / 各部件（source、decoder、rga、encoder、sink）/ pipeline
  - 依赖只能往下，部件之间互相不 include
  - 写清楚每个类管什么、对象谁拥有、什么时候释放
- 接口按后面的里程碑来切。例：Day 5 M1 就把"解码段"和"缩放编码段"分开，M2 只要在中间加队列和线程。
- 不为想象中的需求加抽象：只有一种实现时不写虚接口。
- 分工照旧：MPP 核心代码用户手写，见 [[feedback-teaching-workflow]]。从上一天复制过来的代码顺手按命名规范改名。
