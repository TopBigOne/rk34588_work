// 实验 2：mmap 1 GB，内存却一点没涨；碰到哪一页，才真正分配哪一页
//
// 看点：mmap 只是在页表里"预留"一段地址，不分配物理内存。第一次访问某一页时触发"缺页"，
// 内核这时才分配一页物理内存、填进页表。讲解见 ../doc/open_ioctl_mmap是什么.md 第 6 节。
//
// Mac 上编译运行：  cc -o /tmp/mmap_lazy_demo mmap_lazy_demo.c && /tmp/mmap_lazy_demo
#include <stdio.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>

/**
 * @brief 本进程到目前为止"最多占用过"多少物理内存（RSS），单位 MB
 * @return MB 数
 * @note getrusage 的 ru_maxrss：Linux 上单位是 KB，macOS 上单位是字节
 */
static long max_rss_mb(void) {
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
#ifdef __APPLE__
    return usage.ru_maxrss / 1024 / 1024;
#else
    return usage.ru_maxrss / 1024;
#endif
}

/**
 * @brief 本进程到目前为止发生了多少次"缺页"（第一次碰到某一页、内核临时补上物理页）
 * @return 次数
 */
static long minor_page_fault_count(void) {
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_minflt;
}

int main(void) {
    const size_t kMapSizeBytes   = 1UL << 30; // 1 GB
    const size_t kTouchSizeBytes = 1UL << 28; // 只碰前 256 MB
    const long pageSizeBytes     = sysconf(_SC_PAGESIZE);
    printf("page size: %ld bytes\n", pageSizeBytes);

    printf("start:                 max RSS %4ld MB, page faults %7ld\n", max_rss_mb(), minor_page_fault_count());

    // MAP_ANON：不用 fd，要一段全新的空内存（malloc 申请大块内存时，背后就是这么做的）
    char* mapAddr = mmap(NULL, kMapSizeBytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (mapAddr == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    printf("after mmap 1024 MB:    max RSS %4ld MB, page faults %7ld   (mapAddr %p)\n", max_rss_mb(),
        minor_page_fault_count(), (void*)mapAddr);

    // 每一页写 1 个字节：第一次碰到每一页都会触发一次缺页
    for (size_t offset = 0; offset < kTouchSizeBytes; offset += (size_t) pageSizeBytes) {
        mapAddr[offset] = 1;
    }
    printf("after touching 256 MB: max RSS %4ld MB, page faults %7ld   (256 MB / %ld = %ld pages)\n", max_rss_mb(),
        minor_page_fault_count(), pageSizeBytes, (long) (kTouchSizeBytes / pageSizeBytes));

    munmap(mapAddr, kMapSizeBytes);
    return 0;
}
