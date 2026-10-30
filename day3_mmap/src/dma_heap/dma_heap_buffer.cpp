#include "dma_heap/dma_heap_buffer.h"

#include "common/log.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/dma-heap.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static const char* kTag = "DmaHeap";

bool alloc_dma_heap(const char* heapPath, size_t sizeBytes, DmaHeapBuffer& heapBuf) {
    const int heapFd = open(heapPath, O_RDWR | O_CLOEXEC);
    if (heapFd < 0) {
        LOGE(kTag, " open %s: %s", heapPath, strerror(errno));
        return false;
    }
    dma_heap_allocation_data allocationData = {};

    allocationData.len      = sizeBytes;
    allocationData.fd_flags = O_RDWR | O_CLOEXEC;
    const int ret           = ioctl(heapFd, DMA_HEAP_IOCTL_ALLOC, &allocationData);
    close(heapFd);
    if (ret != 0) {
        LOGE(kTag, " DMA_HEAP_IOCTL_ALLOC %s: %s", heapPath, strerror(errno));
        return false;
    }
    void* mmapAddress = mmap(nullptr, sizeBytes, PROT_READ | PROT_WRITE, MAP_SHARED, allocationData.fd, 0);
    if (mmapAddress == MAP_FAILED) {
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
        munmap(heapBuf.mmapAddress, heapBuf.sizeBytes);
        heapBuf.mmapAddress = nullptr;
    }
    // fd 可能是 0（0 号 fd 被关过以后会被重新分配），所以判断 >= 0，不是 > 0
    if (heapBuf.dmaBufFd >= 0) {
        close(heapBuf.dmaBufFd);
        heapBuf.dmaBufFd = -1;
    }
}
