//
// 计时用的小工具
//
#pragma once

#include <chrono>

/**
 * @brief 取当前时间，单位毫秒（steady_clock：只往前走，不受改系统时间影响）
 * @return 从某个固定起点开始的毫秒数；只用来相减算时间差，单独看没有意义
 * @note inline：写在头文件里，多个 .cpp 都 include 也不会链接冲突
 */
inline double get_now_ms() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}
