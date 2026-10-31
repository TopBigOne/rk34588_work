#include "dma_heap/dma_heap_buffer.h"

#include "common/log.h"
#include <cerrno> // errno
#include <cstring> // strerror
#include <fcntl.h> // open、O_RDWR、O_CLOEXEC
#include <linux/dma-heap.h> // dma_heap_allocation_data、DMA_HEAP_IOCTL_ALLOC
#include <sys/ioctl.h> // ioctl
#include <sys/mman.h> // mmap、munmap、MAP_FAILED
#include <unistd.h> // close

static const char* kTag = "DmaHeap";

bool alloc_dma_heap(const char* heapPath, size_t sizeBytes, DmaHeapBuffer& heapBuf) {
    // dma heap：内核里专门分配"能给硬件用的内存"的地方，每种 heap 是 /dev/dma_heap/ 下的一个文件。
    //   例如 system（普通物理页，不一定连续）、cma（物理连续）。open 它只是为了下单申请内存
    const int heapFd = open(heapPath, O_RDWR | O_CLOEXEC);
    if (heapFd < 0) {
        LOGE(kTag, " open %s: %s", heapPath, strerror(errno));
        return false;
    }
    // DMA_HEAP_IOCTL_ALLOC 用的表，4 个字段（linux/dma-heap.h）：
    //   len（输入，要多少字节）、fd（输出，内核写回新 dma_buf 的 fd）、fd_flags（输入，新 fd 的打开方式）、
    //   heap_flags（输入，现在没有定义任何值，必须是 0，= {} 已经清零）
    dma_heap_allocation_data allocationData = {};

    allocationData.len = sizeBytes;
    // 新 fd 的打开方式：只接受 O_CLOEXEC 和读写方式。要可写的 mmap，就得 O_RDWR
    allocationData.fd_flags = O_RDWR | O_CLOEXEC;
    // 和 V4L2 的 ioctl 一样是"填表 → 写回"：成功时内核分配好内存，包成一个 dma_buf，把它的 fd 写进 allocationData.fd
    const int ret = ioctl(heapFd, DMA_HEAP_IOCTL_ALLOC, &allocationData);
    // heapFd 用完马上关：内存已经归 allocationData.fd 这个 dma_buf 管，和 heapFd 没关系了
    close(heapFd);
    if (ret != 0) {
        LOGE(kTag, " DMA_HEAP_IOCTL_ALLOC %s: %s", heapPath, strerror(errno));
        return false;
    }
    // 和 M2 的 pointerB 一样：mmap 一个 dma_buf fd，偏移填 0（从这块的开头映射）
    void* mmapAddress = mmap(nullptr, sizeBytes, PROT_READ | PROT_WRITE, MAP_SHARED, allocationData.fd, 0);
    if (mmapAddress == MAP_FAILED) { // mmap 失败返回 MAP_FAILED，不是 nullptr
        LOGE(kTag, " mmap dma_buf: %s", strerror(errno));
        close(allocationData.fd);
        return false;
    }
    heapBuf.dmaBufFd    = allocationData.fd;
    heapBuf.mmapAddress = mmapAddress;
    heapBuf.sizeBytes   = sizeBytes;
    LOGI(kTag, "alloc dma heap in success. ✅✅✅✅");
    return true;
}

void free_dma_heap(DmaHeapBuffer& heapBuf) {
    if (heapBuf.mmapAddress != nullptr) {
        munmap(heapBuf.mmapAddress, heapBuf.sizeBytes); // 长度要和 mmap 时一样
        heapBuf.mmapAddress = nullptr;
    }
    // fd 可能是 0（0 号 fd 被关过以后会被重新分配），所以判断 >= 0，不是 > 0
    if (heapBuf.dmaBufFd >= 0) {
        // 映射拆了、fd 也关了，没人再引用这个 dma_buf，内核才把内存还给 heap
        close(heapBuf.dmaBufFd);
        heapBuf.dmaBufFd = -1;
    }
}
