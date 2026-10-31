//
// Created by DEV on 2026/10/31.
//

/**
 * M3 学到了什么（2026-10-31 上板实测，3 MB，每种 heap 10 轮取平均）：
 *   +-----------------+------------+-----------+----------+-----------+-----------+
 *   | heap            | sync_start | memset    | sync_end | 合计      | 写入速度  |
 *   +-----------------+------------+-----------+----------+-----------+-----------+
 *   | system          | 0.001 ms   | 0.349 ms  | 0.001 ms | 0.351 ms  | 8601 MB/s |
 *   | system-uncached | 0.001 ms   | 0.651 ms  | 0.001 ms | 0.653 ms  | 4607 MB/s |
 *   +-----------------+------------+-----------+----------+-----------+-----------+
 *
 * 1. dma_buf 的内存分两种：
 *      cached（system）           ：CPU 读写走缓存，快；但交给硬件前后要 sync，把缓存里的数据刷到内存。
 *      uncached（system-uncached）：CPU 读写不走缓存，不用 sync；代价是 CPU 读写慢（实测 memset 慢约 1.9～2.7 倍）
 * 2. sync 本身花不花时间，要看这块内存有没有交给过硬件：只给 CPU 自己用时，cached 的 sync 也几乎是 0
 *    （推断：上游 5.10 system_heap.c 只对已经 attach 的设备刷缓存，本板 Rockchip 内核源码没核对）
 * 3. 以后 MPP 申请 buffer 会遇到这个选择：
 *    摄像头 → 编码器，CPU 几乎不碰 → 用 uncached，省掉 sync；
 *    CPU 要改图像（画框、叠字）→ 用 cached，前后记得 sync。
 *    MPP 自己对 uncached buffer 也直接跳过 sync（mpp/base/mpp_buffer.cpp 第 293 行）
 */

#include "m3_sync.h"


#include "common/log.h"
#include "dma_heap/dma_heap_buffer.h"
#include <cerrno>
#include <chrono>
#include <cstring>
#include <linux/dma-buf.h>
#include <string>
#include <sys/ioctl.h>
static const char* kTag              = "M3";
static const int kSyncRepeatCount    = 10;
static const char* kCachedHeapPath   = "/dev/dma_heap/system";
static const char* kUncachedHeapPath = "/dev/dma_heap/system-uncached";

struct SyncTiming {
    double syncStartMs = 0; // sync (START|WRITE) 花的时间
    double memsetMs    = 0; // memset
    double syncEndMs   = 0; // memset
};

/**
 * @brief 取当前时间，单位毫秒
 * @return 从某个固定起点开始的毫秒数，只能拿两次的结果相减，单独一个值没有意义
 * @note 用 steady_clock：它只会往前走。system_clock 是墙上时间，可能被 NTP 或手动改时间调回去，量时间差会出错
 */
static double get_now_ms() {
    using namespace std::chrono;
    // duration<double, std::milli>：把时间换算成"带小数的毫秒"，这样 0.123 ms 也能表示
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

static bool sync_dma_buf(int dma_buf_fd, int flags) {
    dma_buf_sync synsArgs = {};
    synsArgs.flags        = flags;
    if (ioctl(dma_buf_fd, DMA_BUF_IOCTL_SYNC, &synsArgs) != 0) {
        LOGE(kTag, "    DMA_BUF_IOCTL_SYNC flags 0x%llx: %s", static_cast<unsigned long long>(flags), strerror(errno));
        return false;
    }
    return true;
}
/**
 * @brief 把一种 heap 的平均耗时打印成一行，后面附上合计和 memset 的写入速度
 * @param[in] heapPath heap 的设备文件，打印在行首
 * @param[in] timing   measure_sync_timing() 传出的平均耗时
 * @return 无
 */
static void print_sync_timing(const char* heapPath, const SyncTiming& timing) {
    const double totalMs = timing.syncStartMs + timing.memsetMs + timing.syncEndMs;
    // 写入速度 = 写了多少 MB ÷ 花了多少秒。1 MB = 1024 × 1024 字节，1 秒 = 1000 毫秒
    const double memsetMegabytesPerSecond =
        (static_cast<double>(kHeapBufSizeBytes) / (1024 * 1024)) / (timing.memsetMs / 1000);
    LOGI(kTag, "%-30s sync_start %.3f ms | memset %.3f ms | sync_end %.3f ms | 合计 %.3f ms | 写入 %.0f MB/s", heapPath,
        timing.syncStartMs, timing.memsetMs, timing.syncEndMs, totalMs, memsetMegabytesPerSecond);
}


static bool measure_sync_timing(const char* heapPath, SyncTiming& averageTiming) {
    DmaHeapBuffer heap_buffer;
    if (!alloc_dma_heap(heapPath, kHeapBufSizeBytes, heap_buffer)) {
        return false;
    }
    memset(heap_buffer.mmapAddress, 0, heap_buffer.sizeBytes);
    SyncTiming totalTiming;
    bool isAllSyncOk = true;
    for (int roundIndex = 0; roundIndex < kSyncRepeatCount; ++roundIndex) {
        // step1: 开始告诉内核，cpu开始写着块
        double beforeSyncStartMs = get_now_ms();
        isAllSyncOk &= sync_dma_buf(heap_buffer.dmaBufFd, DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE);
        double afterSyncStartMs = get_now_ms();

        // step2: cpu 写满整块
        memset(heap_buffer.mmapAddress, roundIndex, heap_buffer.sizeBytes);
        double afterMemsetMs = get_now_ms();

        // step 3: 结束，告诉内核，cpu写完了
        isAllSyncOk &= sync_dma_buf(heap_buffer.dmaBufFd, DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE);
        double afterSyncEndMs = get_now_ms();

        totalTiming.syncStartMs += afterSyncStartMs - beforeSyncStartMs;
        totalTiming.memsetMs += afterMemsetMs - afterSyncStartMs;
        totalTiming.syncEndMs += afterSyncEndMs - afterMemsetMs;
    }
    free_dma_heap(heap_buffer);
    if (!isAllSyncOk) {
        return false;
    }
    averageTiming.syncStartMs = totalTiming.syncStartMs / kSyncRepeatCount;
    averageTiming.memsetMs    = totalTiming.memsetMs / kSyncRepeatCount;
    averageTiming.syncEndMs   = totalTiming.syncEndMs / kSyncRepeatCount;
    return true;
}


int run_m3_sync() {
    LOGD(kTag, "run_m3_sync()");
    LOGD(kTag, "===== M3：自己调 DMA_BUF_IOCTL_SYNC（每种 heap %d 轮取平均，每块 %zu 字节）=====", kSyncRepeatCount,
        kHeapBufSizeBytes);

    // step 1: cached heap
    SyncTiming cachedTiming;
    if (!measure_sync_timing(kCachedHeapPath, cachedTiming)) {
        return 1;
    }

    // step 2: uncached heap
    SyncTiming uncachedTiming;
    if (!measure_sync_timing(kUncachedHeapPath, uncachedTiming)) {
        return 1;
    }
    // step 3: print them fof comparing them.
    print_sync_timing(kCachedHeapPath, cachedTiming);
    print_sync_timing(kUncachedHeapPath, uncachedTiming);
    return 0;
}
