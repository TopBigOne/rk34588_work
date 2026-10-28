#include "log.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/videodev2.h>
#include <sys/mman.h>

#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

static const char* TAG                 = "DAY3_MMAP";
static const char* kTestFilePath       = "/tmp/day3_mmap_test.bin";
static const size_t kMallocSizeBytes   = 4 * 1024 * 1024;
static const size_t kTestFileSizeBytes = 4096;

static bool create_test_file() {
    LOGD(TAG, "print_whole_maps()");
    // 0644：新建文件的权限（只在 O_CREAT 真的新建文件时用到）。
    //   开头的 0 表示这是八进制数；后面 3 个数字依次是：文件主人 / 同组的人 / 其他人。
    //   每个数字 = 读 4 + 写 2 + 执行 1 加起来：6 = 读 + 写，4 = 只读。
    //   所以 0644 = 主人能读写，别人只能读（ls -l 显示为 -rw-r--r--）。
    const int filedFd = open(kTestFilePath, O_RDWR | O_CREAT | O_CLOEXEC | O_TRUNC, 0644);
    if (filedFd < 0) {
        LOGE(TAG, "  open %s: %s\n", kTestFilePath, strerror(errno));
        return false;
    }
    char content[kTestFileSizeBytes];
    memset(content, 'A', sizeof(content));
    const bool isWritten = write(filedFd, content, sizeof(content)) == static_cast<ssize_t>(sizeof(content));
    close(filedFd);
    return isWritten;
}

/**
 * @brief 把整个 /proc/self/maps 原样打印出来，一行一条日志
 * @note 用 std::getline 读：它读到 '\n' 就停，并且把 '\n' 丢掉，不放进 line。
 *       fgets 会把 '\n' 留在 line 末尾，log.h 又自动加一个换行，日志就会隔一行一个空行。
 */
static void print_whole_maps() {
    LOGD(TAG, "print_whole_maps()");
    std::ifstream mapsFile("/proc/self/maps");
    if (!mapsFile.is_open()) {
        LOGE(TAG, "open /proc/self/maps: %s", strerror(errno));
        return;
    }
    std::string line;
    while (std::getline(mapsFile, line)) {
        LOGI(TAG, "     %s", line.c_str());
    }
}

static int run_m1_maps() {
    LOGD(TAG, "run_m1_maps()");
    LOGD(TAG, "===== M1：到 /proc/self/maps 里查指针是谁 =====");
    // step 1: maloc 申请一块大于128kb的内存，它内部会调用mmap
    void* mallocAddr = malloc(kMallocSizeBytes);
    memset(mallocAddr, 0, kMallocSizeBytes);
    // step 2 :1个fileFd ，用mmap映射2次
    if (!create_test_file()) {
        free(mallocAddr);
        return 1;
    }
    const int fileFd      = open(kTestFilePath, O_RDWR | O_CLOEXEC);
    void* fileAddrShared  = mmap(nullptr, kTestFileSizeBytes, PROT_READ | PROT_WRITE, MAP_SHARED, fileFd, 0);
    void* fileAddrPrivate = mmap(nullptr, kTestFileSizeBytes, PROT_READ | PROT_WRITE, MAP_PRIVATE, fileFd, 0);
    // mmap 完 fd 就可以关，映射还在，为什么？
    //   fd 只是一张"取件单"：mmap 时内核凭它找到文件，然后在这段映射里自己记下"这是哪个文件"
    //   （所以 maps 那一行能显示 /tmp/day3_mmap_test.bin），并且自己拿住这个文件不放。
    //   close(fileFd) 只是撕掉你手里的单子，内核的那份记录还在，映射照样能用；
    //   要等 munmap，内核才放手。摄像头 buffer、dma_buf 的 mmap 也一样。
    close(fileFd);
    if (fileAddrPrivate == MAP_FAILED || fileAddrShared == MAP_FAILED) {
        LOGE(TAG, "mmap %s: %s", kTestFilePath, strerror(errno));
        free(mallocAddr);
        return -1;
    }
    LOGI(TAG, "--- 第 1 步：整个 maps 原样打印，用眼睛找下面两个地址 ---");
    LOGI(TAG, " mallocAddr      = %p", mallocAddr);
    LOGI(TAG, " sharedFileAddr  = %p", fileAddrShared);
    LOGI(TAG, " privateFileAddr = %p", fileAddrPrivate);
    print_whole_maps();
    munmap(fileAddrPrivate, kTestFileSizeBytes);
    munmap(fileAddrShared, kTestFileSizeBytes);
    free(mallocAddr);
    return 0;
}

int main() {
    run_m1_maps();
    return 0;
}
