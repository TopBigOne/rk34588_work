//
// Created by DEV on 2026/10/31.
//

#pragma once
/**
 * @bref cached (system) 和uncached（system-uncached） 两种 dma heap，各测试一下sync和memset的耗时
 * @return
 * @note： 只测试 “CPU 写 这个方向，”（flags 带dma_buf_sync_write),不涉及硬件，所以能看出sync本身花了多长时间
 */
int run_m3_sync();
