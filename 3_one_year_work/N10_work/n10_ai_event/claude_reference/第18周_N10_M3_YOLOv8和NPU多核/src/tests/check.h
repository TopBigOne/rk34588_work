#pragma once

#include <cstdio>

// Mac 单测的检查宏（不引入 gtest）：失败时打印文件、行号和表达式，最后 main 按失败数返回
extern int g_failedCheckCount;
extern int g_passedCheckCount;

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (condition) {                                                                                               \
            g_passedCheckCount++;                                                                                      \
        } else {                                                                                                       \
            g_failedCheckCount++;                                                                                      \
            printf("  [FAIL] %s:%d  %s\n", __FILE__, __LINE__, #condition);                                            \
        }                                                                                                              \
    } while (0)

void runEventMacTests();  // event_mac_tests.cpp
void runYolov8MacTests();  // yolov8_mac_tests.cpp
