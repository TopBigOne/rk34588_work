// 来源：3_one_year_work/N0_work/n0_nvr_mosaic/claude_reference/第02周_N0_Day6的M4和Day8上板/src/common/log.h
#pragma once

#include <cstdarg>    // va_list
#include <cstdio>     // printf / vprintf / fflush
#include <cstdlib>    // getenv
#include <ctime>      // localtime_r
#include <sys/time.h> // gettimeofday

enum class LogLevel { Verbose, Debug, Info, Warn, Error };

#define LOGV(tag, ...) logPrint(LogLevel::Verbose, tag, __VA_ARGS__)
#define LOGD(tag, ...) logPrint(LogLevel::Debug, tag, __VA_ARGS__)
#define LOGI(tag, ...) logPrint(LogLevel::Info, tag, __VA_ARGS__)
#define LOGW(tag, ...) logPrint(LogLevel::Warn, tag, __VA_ARGS__)
#define LOGE(tag, ...) logPrint(LogLevel::Error, tag, __VA_ARGS__)

/**
 * @brief 要不要输出颜色
 * @return true = 没设置环境变量 NO_COLOR，输出颜色；false = 设置了（任何值），不输出颜色
 * @note 只在第一次调用时读一次环境变量（static 局部变量只初始化一次）。
 *       没有用 isatty() 判断"是不是终端"：通过 adb shell 运行时，板子上的 stdout 不是终端，
 *       用 isatty 判断的话颜色就全没了，但 Mac 这边的终端其实能显示颜色
 */
inline bool isLogColorEnabled() {
    static const bool isEnabled = std::getenv("NO_COLOR") == nullptr;
    return isEnabled;
}

/**
 * @brief 打印一条带时间、级别、标签、颜色的日志，自动换行
 * @param[in] level  级别，决定颜色和字母（V/D/I/W/E）
 * @param[in] tag    标签，一般写模块名，例如 "MppDecoder"
 * @param[in] format 和 printf 一样的格式字符串，末尾不用写 \n
 * @param[in] ...    格式字符串对应的参数
 * @return 无
 * @note format(printf, 3, 4)：让编译器像检查 printf 一样检查格式串和参数类型对不对得上
 *       （第 3 个参数是格式串，可变参数从第 4 个开始）。
 *       每条日志后 fflush：通过 adb 运行时 stdout 是全缓冲的，不刷新的话日志会攒一大段才出来，
 *       程序崩溃时最后几条还会丢。
 *       多个线程同时打日志时，一条日志由好几次 printf 拼成，两条日志的片段可能交错在一行里（推断，没实测到）
 */
__attribute__((format(printf, 3, 4))) inline void logPrint(LogLevel level, const char *tag, const char *format, ...) {
    static const char *const kColors[] = {
        "\033[38;2;14;131;227m", // V：0E83E3
        "\033[38;2;218;117;55m", // D：DA7537
        "\033[38;2;16;138;13m",  // I：108A0D
        "\033[38;2;232;169;8m",  // W：E8A908
        "\033[38;2;196;14;10m",  // E：C40E0A
    };
    static const char kLetters[] = {'V', 'D', 'I', 'W', 'E'};
    const char *const kResetColor = "\033[0m";

    const int levelIndex = static_cast<int>(level);
    const bool isColorEnabled = isLogColorEnabled();

    timeval nowTime;                   // 秒 + 微秒
    gettimeofday(&nowTime, nullptr);   // 取当前时间，第 2 个参数（时区）早就不用了，传 nullptr
    tm localTime;                      // 拆成年月日时分秒
    localtime_r(&nowTime.tv_sec, &localTime); // _r = 线程安全版：结果写进我们给的 localTime，不用全局变量

    printf("%s%02d:%02d:%02d.%03ld %c %s: ", isColorEnabled ? kColors[levelIndex] : "", localTime.tm_hour,
           localTime.tm_min, localTime.tm_sec, static_cast<long>(nowTime.tv_usec / 1000), kLetters[levelIndex], tag);
    va_list args;           // 可变参数列表
    va_start(args, format); // 从 format 后面那个参数开始取
    vprintf(format, args);  // printf 的"接收 va_list"版本
    va_end(args);
    printf("%s\n", isColorEnabled ? kResetColor : "");
    fflush(stdout);
}
