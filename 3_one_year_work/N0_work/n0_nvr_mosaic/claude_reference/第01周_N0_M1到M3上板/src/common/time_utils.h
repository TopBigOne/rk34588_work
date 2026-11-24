#pragma once

#include <chrono>

/**
 * @brief 取当前时间，单位毫秒，用来算耗时（两次相减）
 * @return 从某个固定起点（一般是开机）到现在的毫秒数，带小数
 * @note steady_clock：只往前走、不受改系统时间影响的时钟，专门用来计时；它的绝对值没有意义，只能拿来相减
 */
inline double getNowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}
