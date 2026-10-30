/*
 * dma_heap_buffer.h：从 /dev/dma_heap/xxx 申请一块 dma_buf，并 mmap 到本进程（M1 第 3 步、M3 用）
 *
 *   alloc_dma_heap()  open(heap) → DMA_HEAP_IOCTL_ALLOC → close(heapFd) → mmap(dmaBufFd)
 *   free_dma_heap()   munmap → close(dmaBufFd)
 */
#pragma once

#include <cstddef>

inline constexpr size_t kHeapBufSizeBytes = 3 * 1024 * 1024;

struct DmaHeapBuffer {
    int dmaBufFd      = -1;
    void* mmapAddress = nullptr;
    size_t sizeBytes  = 0;
};

/**
 * @brief 从 dma heap 申请一块 dma buffer，并 mmap 到本进程
 * @param[in]  heapPath  heap 设备路径，例如 "/dev/dma_heap/system"
 * @param[in]  sizeBytes 要申请多少字节
 * @param[out] heapBuf   成功时填好 dmaBufFd、mmapAddress、sizeBytes
 * @return 成功返回 true；失败返回 false，heapBuf 不动
 * @note 申请完就关掉 heapFd：heapFd 只是"找 heap 下单"用的，buffer 归 dmaBufFd 管
 * @note 用完要调 free_dma_heap() 释放
 */
bool alloc_dma_heap(const char* heapPath, size_t sizeBytes, DmaHeapBuffer& heapBuf);

/**
 * @brief 释放 alloc_dma_heap() 申请的 buffer：先 munmap，再 close(dmaBufFd)
 * @param[in,out] heapBuf 要释放的 buffer，释放后 mmapAddress 置 nullptr、dmaBufFd 置 -1
 * @return 无
 * @note 可以重复调用，也可以对没申请成功的 heapBuf 调用（字段是初始值就跳过）
 */
void free_dma_heap(DmaHeapBuffer& heapBuf);
