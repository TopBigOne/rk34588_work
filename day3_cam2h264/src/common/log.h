/*
 * 带颜色的日志（照 Android logcat 的样子）：
 *
 *   16:52:01.123 D FindCamera: /dev/media0  model=rkcif-mipi-lvds  13 个实体     ← 橙色（DA7537）
 *   16:52:01.125 I FindCamera: main path : /dev/video22                        ← 绿色（108A0D）
 *   16:52:01.126 W FindCamera: ...                                             ← 黄色（E8A908）
 *   16:52:01.127 E FindCamera: 没有找到摄像头                                    ← 红色（C40E0A）
 *
 *   （V 是蓝色 0E83E3）
 *
 * 原理：ANSI 颜色转义码。终端看到 "\033[32m" 不会把它显示出来，而是把后面的字改成绿色，
 * 看到 "\033[0m" 就恢复默认颜色。CLion 的终端和 Run 窗口、macOS 终端、adb shell 都认这种写法。
 * 详见 doc/终端彩色输出_ANSI颜色码.md
 *
 * 用法（不用写 \n，每条日志自动换行）：
 *   LOGI("FindCamera", "main path : %s", path.c_str());
 *   LOGE("FindCamera", "open %s failed: %s", dev, strerror(errno));
 *
 * 不想要颜色（比如输出重定向到文件，不想文件里出现 \033[..m 这些字符）：
 *   NO_COLOR=1 ./day3_cam2h264
 */
#pragma once

#include <cstdarg>    // va_list
#include <cstdio>     // printf / vprintf / fflush
#include <cstdlib>    // getenv
#include <ctime>      // localtime_r
#include <sys/time.h> // gettimeofday

// 日志级别，和 logcat 一样从低到高
enum class LogLevel { Verbose, Debug, Info, Warn, Error };

/**
 * @brief 要不要输出颜色
 * @return 没设置环境变量 NO_COLOR 时返回 true；设置了（任何值）返回 false
 * @note 只在第一次调用时读一次环境变量（static 局部变量）。
 * @note 没有用 isatty() 判断"是不是终端"：通过 adb shell 运行时，板子上的 stdout 不是终端，
 *       用 isatty 判断的话颜色就全没了，但 Mac 这边的终端其实是能显示颜色的。
 */
inline bool log_color_enabled() {
    static const bool enabled = std::getenv("NO_COLOR") == nullptr;
    return enabled;
}

/**
 * @brief 打印一条带时间、级别、标签、颜色的日志，自动换行
 * @param[in] level  级别，决定颜色和字母（V/D/I/W/E）
 * @param[in] tag    标签，一般写模块或类名，例如 "FindCamera"
 * @param[in] fmt    和 printf 一样的格式字符串，末尾不用写 \n
 * @param[in] ...    格式字符串对应的参数
 * @note format(printf, 3, 4)：让编译器像检查 printf 一样检查 fmt 和参数类型对不对得上
 *       （第 3 个参数是格式串，可变参数从第 4 个开始）。
 * @note 每条日志后 fflush：通过 adb 运行时 stdout 是全缓冲的，不刷新的话日志会攒一大段才出来，
 *       程序崩溃时最后几条还会丢。
 */
__attribute__((format(printf, 3, 4))) inline void log_print(LogLevel level, const char* tag, const char* fmt, ...) {
    // 颜色（RRGGBBAA，最后的 FF 是不透明度，终端用不到）：
    // V ：0E83E3FF
    // D ：DA7537FF
    // I ：108A0DFF
    // W ：E8A908FF
    // E ：C40E0AFF
    // 用 24 位真彩色写法 "\033[38;2;R;G;Bm"（38 = 设置文字颜色，2 = 后面跟 R;G;B 三个十进制数）
    // 例：0E83E3 → R=0x0E=14，G=0x83=131，B=0xE3=227 → "\033[38;2;14;131;227m"
    static const char* const kColor[] = {
        "\033[38;2;14;131;227m", // V：0E83E3
        "\033[38;2;218;117;55m", // D：DA7537
        "\033[38;2;16;138;13m",  // I：108A0D
        "\033[38;2;232;169;8m",  // W：E8A908
        "\033[38;2;196;14;10m",  // E：C40E0A
    };
    static const char kLetter[]       = {'V', 'D', 'I', 'W', 'E'};
    const char* const kReset          = "\033[0m";

    const int i      = static_cast<int>(level);
    const bool color = log_color_enabled();

    // 时间：时:分:秒.毫秒（板子的时区可能是 UTC，和 Mac 差 8 小时）
    timeval tv;
    gettimeofday(&tv, nullptr);
    tm t;
    localtime_r(&tv.tv_sec, &t);

    printf("%s%02d:%02d:%02d.%03ld %c %s: ", color ? kColor[i] : "", t.tm_hour, t.tm_min, t.tm_sec,
        static_cast<long>(tv.tv_usec / 1000), kLetter[i], tag);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("%s\n", color ? kReset : "");
    fflush(stdout);
}

// 和 Android 的 Log.v / Log.d / Log.i / Log.w / Log.e 对应
#define LOGV(tag, ...) log_print(LogLevel::Verbose, tag, __VA_ARGS__)
#define LOGD(tag, ...) log_print(LogLevel::Debug, tag, __VA_ARGS__)
#define LOGI(tag, ...) log_print(LogLevel::Info, tag, __VA_ARGS__)
#define LOGW(tag, ...) log_print(LogLevel::Warn, tag, __VA_ARGS__)
#define LOGE(tag, ...) log_print(LogLevel::Error, tag, __VA_ARGS__)
