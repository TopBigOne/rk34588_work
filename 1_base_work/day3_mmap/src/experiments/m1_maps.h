/*
 * m1_maps.h：M1 实验——申请几种不同来历的内存，到 /proc/self/maps 里查每个指针是哪一行
 *
 * 结果分析见 doc/1-理解M1的运行结果.md
 */
#pragma once

/**
 * @brief M1：申请几种不同来历的内存，到 /proc/self/maps 里查每个指针是哪一行
 * @return 成功返回 0，失败返回 1
 * @note 第 1 步 malloc + 文件 MAP_SHARED / MAP_PRIVATE；第 2 步摄像头 4 块 buffer
 */
int run_m1_maps();
