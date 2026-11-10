//
// Created by DEV on 2026/11/9.
//
#pragma once

#include <chrono>

inline double getNowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}
