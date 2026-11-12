---
name: feedback-learning-depth-line
description: 2026-11-13 用户同意：每个知识点划“够用线”，超出的只给一句结论并记进“以后再挖”清单，不展开
metadata:
  type: feedback
---

2026-11-13 用户说学习进度很慢（10 月初到 11-13 才到 Day 5 M1 编码段），目标是“把 3588 的 MPP 弄明白”，但陌生知识点太多（V4L2、mmap、C++），又都想弄会。商量后定下：**每个知识点划一条“够用线”**。

判断标准：**不懂它，我能不能看懂、写出眼前这段 MPP 相关的代码？**

参考的够用线：
- mmap：把内核 buffer 映射到用户态拿到地址；失败返回 `MAP_FAILED`；不分配新内存。不学页表、VMA、缺页
- V4L2：REQBUFS → QBUF/DQBUF → STREAMON；EXPBUF 拿 dma-buf fd。不学 media controller、驱动
- dma-buf：一个 fd 代表一块物理内存，可在摄像头、RGA、MPP 之间零拷贝传递。不学内核实现
- C++：能看懂、能写出 RAII、`unique_ptr` 加删除器、引用、`const`。不学模板元编程、标准库内部

**Why:** 慢的主要原因是 MPP 和 C++/Linux 底子同时在补，每个点都往下挖；还有一部分是流程本身造成的（重文档、规范反复改、review 分多轮）。用户的好奇心不能压掉，但主线不能被拖住。

**How to apply:**
- 用户问问题时，我先说一句“这条在够用线以内 / 以外”
- 线以内：照常讲清楚
- 线以外：只给一句结论，提醒记进 `day5_transcoder/doc/我不理解的代码.md` 的“以后再挖”区（我可以直接帮着加一行），不展开成好几节
- Day 5 做完后，提醒用户回清单挑一两条专门挖
- review 代码一次给全清单：标出“必须改 / 建议改”，用户改完我直接验证，减少来回轮数

相关：[[feedback-stay-on-codec-transport]]、[[feedback-new-tech-point-doc]]
