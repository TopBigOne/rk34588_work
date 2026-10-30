/*
 * maps_printer.h：到 /proc/self/maps 里查"这个指针是谁"（M1、M2 都用）
 *
 *   - print_whole_maps()            整个 maps 原样打印，一行一条日志
 *   - print_maps_line_containing()  只打印包含某个地址的那一行
 *
 * maps 每一列的意思见 doc/1-理解M1的运行结果.md 第 2 节
 */
#pragma once

/**
 * @brief 把整个 /proc/self/maps 原样打印出来，一行一条日志
 * @return 无
 * @note 用 std::getline 读：它读到 '\n' 就停，并且把 '\n' 丢掉，不放进 line。
 *       fgets 会把 '\n' 留在 line 末尾，log.h 又自动加一个换行，日志就会隔一行一个空行。
 */
void print_whole_maps();

/**
 * @brief 在 /proc/self/maps 里找到包含 address 的那一行，打印出来
 * @param[in] label   打印时的前缀，说明这是谁的地址
 * @param[in] address 要查的地址
 * @return 无
 * @note 每一行开头是 "起始-结束"（十六进制），起始 <= address < 结束 就是这一行
 */
void print_maps_line_containing(const char* label, const void* address);
