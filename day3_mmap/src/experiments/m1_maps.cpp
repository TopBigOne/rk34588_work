#include "experiments/m1_maps.h"

#include "camera/camera_buffers.h"
#include "common/log.h"
#include "common/maps_printer.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

static const char* kTag                = "M1";
static const char* kTestFilePath       = "/tmp/day3_mmap_test.bin";
static const size_t kMallocSizeBytes   = 4 * 1024 * 1024;
static const size_t kTestFileSizeBytes = 4096;

/**
 * @brief 新建（或清空重建）测试文件 kTestFilePath，写入 kTestFileSizeBytes 个 'A'
 * @return 文件建好并且写满返回 true，否则返回 false
 * @note 先写满再 mmap：mmap 超过文件末尾的部分，访问时会收到 SIGBUS
 */
static bool create_test_file() {
    LOGD(kTag, "create_test_file()");
    // 0644：新建文件的权限（只在 O_CREAT 真的新建文件时用到）。
    //   开头的 0 表示这是八进制数；后面 3 个数字依次是：文件主人 / 同组的人 / 其他人。
    //   每个数字 = 读 4 + 写 2 + 执行 1 加起来：6 = 读 + 写，4 = 只读。
    //   所以 0644 = 主人能读写，别人只能读（ls -l 显示为 -rw-r--r--）。
    const int testFileFd = open(kTestFilePath, O_RDWR | O_CREAT | O_CLOEXEC | O_TRUNC, 0644);
    if (testFileFd < 0) {
        LOGE(kTag, "  open %s: %s", kTestFilePath, strerror(errno));
        return false;
    }
    char content[kTestFileSizeBytes];
    memset(content, 'A', sizeof(content));
    const bool isWritten = write(testFileFd, content, sizeof(content)) == static_cast<ssize_t>(sizeof(content));
    close(testFileFd);
    return isWritten;
}

int run_m1_maps() {
    LOGD(kTag, "run_m1_maps()");
    LOGD(kTag, "===== M1：到 /proc/self/maps 里查指针是谁 =====");
    // step 1: maloc 申请一块大于128kb的内存，它内部会调用mmap
    void* mallocAddr = malloc(kMallocSizeBytes);
    memset(mallocAddr, 0, kMallocSizeBytes);
    // step 2 :1个fileFd ，用mmap映射2次
    if (!create_test_file()) {
        free(mallocAddr);
        return 1;
    }
    LOGI(kTag, " 文件创建成功，然后把这个文件，用mmap映射2次，但2次的flags不同");
    const int fileFd = open(kTestFilePath, O_RDWR | O_CLOEXEC);
    if (fileFd < 0) {
        LOGE(kTag, "open %s: %s", kTestFilePath, strerror(errno));
        free(mallocAddr);
        return 1;
    }
    void* fileAddrShared  = mmap(nullptr, kTestFileSizeBytes, PROT_READ | PROT_WRITE, MAP_SHARED, fileFd, 0);
    void* fileAddrPrivate = mmap(nullptr, kTestFileSizeBytes, PROT_READ | PROT_WRITE, MAP_PRIVATE, fileFd, 0);
    // mmap 完 fd 就可以关，映射还在，为什么？
    //   fd 只是一张"取件单"：mmap 时内核凭它找到文件，然后在这段映射里自己记下"这是哪个文件"
    //   （所以 maps 那一行能显示 /tmp/day3_mmap_test.bin），并且自己拿住这个文件不放。
    //   close(fileFd) 只是撕掉你手里的单子，内核的那份记录还在，映射照样能用；
    //   要等 munmap，内核才放手。摄像头 buffer、dma_buf 的 mmap 也一样。
    close(fileFd);
    if (fileAddrPrivate == MAP_FAILED || fileAddrShared == MAP_FAILED) {
        LOGE(kTag, "mmap %s: %s", kTestFilePath, strerror(errno));
        free(mallocAddr);
        return 1;
    }
    LOGI(kTag, "--- 第 1 步：整个 maps 原样打印，用眼睛找下面两个地址 ---");
    LOGI(kTag, " mallocAddr      = %p", mallocAddr);
    LOGI(kTag, " sharedFileAddr  = %p", fileAddrShared);
    LOGI(kTag, " privateFileAddr = %p", fileAddrPrivate);
    print_whole_maps();
    // 第 2 步：摄像头 4 块 buffer 在 maps 里是哪几行
    CameraBuffers camera;
    if (open_camera_and_map_buffers(camera)) {
        LOGI(kTag, "--- 第 2 步：摄像头 4 块 buffer ---");
        for (int bufIndex = 0; bufIndex < kBufCount; ++bufIndex) {
            char label[32];
            snprintf(label, sizeof(label), "camera buf %d", bufIndex);
            print_maps_line_containing(label, camera.mmapAddresses[bufIndex]);
            LOGI(kTag, "    QUERYBUF: mem_offset 0x%08x, length %zu", camera.memOffsets[bufIndex],
                camera.lengths[bufIndex]);
        }
    }
    close_camera(camera); // 成功、失败都要调：失败时已经打开的 fd 也要关
    munmap(fileAddrPrivate, kTestFileSizeBytes);
    munmap(fileAddrShared, kTestFileSizeBytes);
    free(mallocAddr);
    return 0;
}
