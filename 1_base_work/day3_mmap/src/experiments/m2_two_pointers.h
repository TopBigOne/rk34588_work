/*
 * m2_two_pointers.h：M2 实验——同一块摄像头 buffer，两个指针
 *
 *   指针 A = mmap(videoFd, mem_offset)
 *   指针 B = mmap(EXPBUF 拿到的 dmaBufFd, 0)
 *
 * 结果分析见 doc/2-理解M2的运行结果.md
 */
#pragma once

/**
 * @brief M2：摄像头第 0 块 buffer 用两种方式 mmap，从 A 写、从 B 读，然后再反过来
 * @return 两个方向都读到一样的内容返回 0，否则返回 1
 * @note 2026-10-31 实测：A 在 maps 里是 /dev/video22，B 是 /dmabuf:<pid>-day3_mmap，两行都 0x2fd000，两个方向都读到
 */
int run_m2_two_pointers();
