#include "experiments/m1_maps.h"

#include "camera/camera_buffers.h"
#include "common/log.h"
#include "common/maps_printer.h"
#include <cerrno> // errno
#include <cstdio> // snprintf
#include <cstdlib> // malloc、free
#include <cstring> // memset、strerror
#include <fcntl.h> // open、O_RDWR、O_CREAT、O_TRUNC、O_CLOEXEC
#include <sys/mman.h> // mmap、munmap、MAP_SHARED、MAP_PRIVATE、MAP_FAILED
#include <unistd.h> // write、close

// static 写在文件最外层：这几个名字只在本 .cpp 里能用，别的 .cpp 看不到，重名也不冲突
static const char* kTag          = "M1";
static const char* kTestFilePath = "/tmp/day3_mmap_test.bin";
// 4 MB：大于 glibc 的 mmap 阈值（默认 128 KB），malloc 不从堆里切，而是直接 mmap 一段匿名内存
static const size_t kMallocSizeBytes = 4 * 1024 * 1024;
// 4096 字节 = 1 页（ARM64 Linux 这里的页大小）。mmap 的最小单位是页
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
    // O_CREAT：文件不存在就新建；O_TRUNC：文件已经存在就把内容清空（长度变成 0）
    const int testFileFd = open(kTestFilePath, O_RDWR | O_CREAT | O_CLOEXEC | O_TRUNC, 0644);
    if (testFileFd < 0) {
        LOGE(kTag, "  open %s: %s", kTestFilePath, strerror(errno));
        return false;
    }
    char content[kTestFileSizeBytes];
    memset(content, 'A', sizeof(content));
    // write 返回实际写了多少字节（类型 ssize_t，失败是 -1），可能比要求的少，所以要和 sizeof(content) 比
    const bool isWritten = write(testFileFd, content, sizeof(content)) == static_cast<ssize_t>(sizeof(content));
    close(testFileFd);
    return isWritten;
}

int run_m1_maps() {
    LOGD(kTag, "run_m1_maps()");
    LOGD(kTag, "===== M1：到 /proc/self/maps 里查指针是谁 =====");
    // step 1: maloc 申请一块大于128kb的内存，它内部会调用mmap
    //   这种 mmap 没有 fd、不对应任何文件，叫"匿名映射"，maps 里那一行没有文件名
    void* mallocAddr = malloc(kMallocSizeBytes);
    // 为什么要 memset：mmap 只是先划好一段地址，物理内存要等第一次写某一页时才真正分配
    //   （写的那一刻触发"缺页"，内核这时才拿一页物理内存补上）。memset 把 4 MB 每一页都写一遍
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
    // MAP_SHARED：和文件共用同一批页（内核的页缓存）。往这个地址写，就是在改文件，别的进程读文件也能看到
    void* fileAddrShared = mmap(nullptr, kTestFileSizeBytes, PROT_READ | PROT_WRITE, MAP_SHARED, fileFd, 0);
    // MAP_PRIVATE：写时复制。没写之前读到的是文件内容；第一次写某一页时，内核复制一份给本进程私用，
    //   之后的修改只在这份副本里，不会写回文件，别人也看不到。maps 里两者的权限列分别是 rw-s 和 rw-p
    void* fileAddrPrivate = mmap(nullptr, kTestFileSizeBytes, PROT_READ | PROT_WRITE, MAP_PRIVATE, fileFd, 0);
    // mmap 完 fd 就可以关，映射还在，为什么？
    //   fd 只是一张"取件单"：mmap 时内核凭它找到文件，然后在这段映射里自己记下"这是哪个文件"
    //   （所以 maps 那一行能显示 /tmp/day3_mmap_test.bin），并且自己拿住这个文件不放。
    //   close(fileFd) 只是撕掉你手里的单子，内核的那份记录还在，映射照样能用；
    //   要等 munmap，内核才放手。摄像头 buffer、dma_buf 的 mmap 也一样。
    close(fileFd);
    if (fileAddrPrivate == MAP_FAILED || fileAddrShared == MAP_FAILED) { // mmap 失败返回 MAP_FAILED，不是 nullptr
        LOGE(kTag, "mmap %s: %s", kTestFilePath, strerror(errno));
        free(mallocAddr);
        return 1;
    }
    LOGI(kTag, "--- 第 1 步：整个 maps 原样打印，用眼睛找下面两个地址 ---");
    // %p：按十六进制打印指针。这些都是虚拟地址，每个进程都有自己的一套，不是物理内存的地址
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
            // snprintf：把格式化的结果写进 label，最多写 sizeof(label) 个字节（含 '\0'），不会写出界
            snprintf(label, sizeof(label), "camera buf %d", bufIndex);
            print_maps_line_containing(label, camera.mmapAddresses[bufIndex]);
            LOGI(kTag, "    QUERYBUF: mem_offset 0x%08x, length %zu", camera.memOffsets[bufIndex],
                camera.lengths[bufIndex]);
        }
    }
    close_camera(camera); // 成功、失败都要调：失败时已经打开的 fd 也要关
    munmap(fileAddrPrivate, kTestFileSizeBytes);
    munmap(fileAddrShared, kTestFileSizeBytes);
    // 用 mmap 申请的大块，free 时 glibc 直接 munmap 还给内核（小块则留在堆里给下次 malloc 用）
    free(mallocAddr);
    return 0;
}
