//
// Created by DEV on 2026/11/2.
//
#pragma once
#include <cstdint> // uint32_t、uint64_t

// 一块 dumb buffer 的信息：CREATE_DUMB、MAP_DUMB、mmap 三步写回的东西放在一起。
//   dumb = "笨的"：最普通的一块线性显存，一行接一行排，CPU 能直接写像素，所有 DRM 驱动都支持，不做 GPU 加速
struct DumbBuffer {
    // CREATE_DUMB 写回的编号（GEM handle）。它不是 fd，只在申请它的那个 drmFd 里有意义；
    //   要交给别的驱动（编码器、RGA），得先用 PRIME 把它导出成 dma_buf fd。0 表示没申请到
    uint32_t handle = 0;
    // CREATE_DUMB 写回的每行字节数（pitch，也叫 stride）。驱动可能给得比 宽 × 4 大，多出来的是每行末尾的填充，
    //   第 n 行的起点 = mmapAddress + n × pitchBytes（2026-10-31 实测：1000 宽时是 4032，不是 4000）
    uint32_t pitchBytes = 0;
    // CREATE_DUMB 写回的整块字节数 = pitchBytes × 高。mmap 和 munmap 的长度都用它，不用 宽 × 高 × 4
    uint64_t sizeBytes = 0;
    // MAP_DUMB 写回的 offset：给 mmap 第 6 个参数用的编号，告诉驱动"要映射的是哪一块"，不是地址
    //   （实测 0x100000000，和摄像头 QUERYBUF 写回的 mem_offset 作用一样）
    uint64_t mmapOffset = 0;
    // mmap(drmFd, mmapOffset) 的返回值：本进程里能直接读写这块显存的地址。nullptr 表示还没映射或者已经 munmap
    void* mmapAddress = nullptr;
};

void unmap_and_destroy_dumb(int drmFd, DumbBuffer& dumbBuffer);

bool create_and_map_dumb(int drmFd, uint32_t width, uint32_t height, DumbBuffer& dumbBuf);

void fill_pixels_by_pitch(const DumbBuffer& dumbBuf, uint32_t width, uint32_t height);

void print_row_start_by_two_strides(const DumbBuffer& dumbBuf, uint32_t width, uint32_t rowIndex);

bool check_export_to_dma_buf(int drmFd, const DumbBuffer& dumbBuf);
bool check_dumb_buffer_of_size(int drmFd, uint32_t width, uint32_t height);


int run_m4_dumb_buffer();
