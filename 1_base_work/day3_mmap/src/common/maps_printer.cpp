#include "common/maps_printer.h"

#include "common/log.h"
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>

static const char* kTag = "Maps";

/**
 * @brief 把一段十六进制文字（不带 0x）转成数字，遇到第一个不是十六进制的字符就停
 * @param[in]  text    从这里开始读，例如 "7f8a000000-7f8a2fd000 ..."
 * @param[out] endText 传出停下来的位置（指向那个不是十六进制的字符）
 * @return 转出来的数字
 * @note 为什么不用 sscanf / strtoul：工具链的 glibc 比板子新，这两个函数会链接到
 *       GLIBC_2.38 才有的 __isoc23_* 版本，板子上跑不起来（readme 第 5 节）
 * @note 只有本文件用，所以是 static，不放进头文件
 */
static uintptr_t parse_hex(const char* text, const char** endText) {
    uintptr_t value = 0;
    while (true) {
        const char ch = *text;
        int digit     = -1;
        if (ch >= '0' && ch <= '9') {
            digit = ch - '0';
        } else if (ch >= 'a' && ch <= 'f') {
            digit = ch - 'a' + 10;
        } else if (ch >= 'A' && ch <= 'F') {
            digit = ch - 'A' + 10;
        }
        if (digit < 0) {
            break;
        }
        value = value * 16 + digit;
        ++text;
    }
    *endText = text;
    return value;
}

void print_whole_maps() {
    LOGD(kTag, "print_whole_maps()");
    // /proc/self/maps：不是磁盘上的真文件，读它的时候内核现场生成内容，列出本进程地址空间里每一段映射。
    //   self 指"正在读它的这个进程"，等于 /proc/<本进程 pid>/maps
    std::ifstream mapsFile("/proc/self/maps");
    if (!mapsFile.is_open()) {
        LOGE(kTag, "open /proc/self/maps: %s", strerror(errno));
        return;
    }
    std::string line;
    while (std::getline(mapsFile, line)) {
        LOGI(kTag, "     %s", line.c_str());
    }
}

void print_maps_line_containing(const char* label, const void* address) {
    std::ifstream mapsFile("/proc/self/maps");
    if (!mapsFile.is_open()) {
        LOGE(kTag, "open /proc/self/maps: %s", strerror(errno));
        return;
    }
    // 指针不能直接和 maps 里读出来的数字比大小，先转成整数。
    //   uintptr_t：大小正好能装下一个指针的无符号整数（ARM64 上是 8 字节）
    const uintptr_t targetAddress = reinterpret_cast<uintptr_t>(address);
    std::string line;
    bool isFound = false;
    // 和 print_whole_maps() 一样用 std::getline 读，line 末尾没有 '\n'，交给 LOGI 换行
    while (std::getline(mapsFile, line)) {
        const char* cursor           = line.c_str();
        const uintptr_t startAddress = parse_hex(cursor, &cursor);
        ++cursor; // 跳过中间的 '-'
        const uintptr_t endAddress = parse_hex(cursor, &cursor);
        // maps 每行开头 "起始-结束"：起始地址算在这一段里，结束地址不算（左闭右开）
        if (startAddress <= targetAddress && targetAddress < endAddress) {
            LOGI(kTag, "%-14s %p -> %s", label, address, line.c_str());
            isFound = true;
            break;
        }
    }
    if (!isFound) {
        LOGI(kTag, "%-14s %p -> （maps 里没找到）", label, address);
    }
}
