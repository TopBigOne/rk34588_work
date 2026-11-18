//
// Nv12Picture：描述一帧 NV12 画面（在哪块 MppBuffer、宽高、stride），以及申请、读写它的函数
// 内存怎么排见 doc/0_base_NV12内存排布.md
//
#pragma once

#include <cstdio>
#include <rockchip/rk_mpi.h>

#include "common/mpp_utils.h"

/**
 * @brief 一帧 NV12 画面：画面在哪块 MppBuffer 里，以及 RGA、编码器、读写文件时需要的宽高和 stride
 * @note 只是"描述"，不负责释放 mppBuf。谁申请的谁释放（alloc_nv12_buffer 返回的 MppBufferPtr，或者 CameraCapture）
 */
struct Nv12Picture {
    MppBuffer mppBuf = nullptr; // 画面所在的 MppBuffer
    int width = 0;              // 画面宽，单位像素
    int height = 0;             // 画面高，单位像素
    int horStride = 0;          // 一行在内存里占多少字节（≥ width）
    int verStride = 0;          // Y 平面在内存里占多少行（≥ height）；UV 平面从第 verStride 行开始
};

/**
 * @brief 按"宽高都对齐到 16"的规则，描述一帧自己申请的 NV12 画面（还没有 buffer）
 * @param[in] width   画面宽，单位像素，例如 1920
 * @param[in] height  画面高，单位像素，例如 1080
 * @return mppBuf 为空的 Nv12Picture；stride = ALIGN(width, 16) × ALIGN(height, 16)，1080p 是 1920 × 1088
 * @note 摄像头的 buffer 不用这个函数：它的 stride 由驱动决定（1920 × 1080），要用 CameraCapture 返回的值
 */
Nv12Picture make_aligned_nv12_picture(int width, int height);

/**
 * @brief 从 MPP 的 buffer group 申请一块能放下 picture 的 buffer，并把它填进 picture.mppBuf
 * @param[in]     bufferGroup  从哪个 group 申请（DRM 类型，RGA、编码器都能用它的 fd）
 * @param[in,out] picture      [in] 宽高和 stride；[out] mppBuf 指向新申请的 buffer
 * @return 管理这块 buffer 的 MppBufferPtr，离开作用域时自动 mpp_buffer_put；申请失败返回空（已打印原因）
 * @note 大小 = horStride × verStride × 1.5。返回值要接住，丢掉的话 buffer 会马上被释放
 */
MppBufferPtr alloc_nv12_buffer(MppBufferGroup bufferGroup, Nv12Picture &picture);

/**
 * @brief 从文件读一帧 NV12（文件里每行紧挨着存，没有填充）到 picture 的 buffer（有 stride 填充）
 * @param[in] inputFile  NV12 原始数据文件，读完一帧，文件位置往后移一帧
 * @param[in] picture    目标 buffer 和它的宽高、stride
 * @return true = 读满一整帧；false = 文件读完了或读失败
 * @note 一行一行读：每行读 width 字节，写到 rowIndex × horStride；UV 从第 verStride 行开始，不是第 height 行。
 *       CPU 写 cached buffer，前后要 sync_begin / sync_end，保证 RGA、编码器读到的是 CPU 刚写的
 */
bool read_nv12_frame(FILE *inputFile, const Nv12Picture &picture);

/**
 * @brief 把 picture 的 buffer 里的一帧 NV12 写进文件，去掉 stride 填充（文件里每行紧挨着存）
 * @param[in] outputFile  输出文件，追加写一帧 width × height × 1.5 字节
 * @param[in] picture     源 buffer 和它的宽高、stride
 * @return true = 写满一整帧；false = 写失败（比如磁盘满了）
 * @note CPU 读 cached buffer 前要 sync_begin，让 CPU 看到 RGA 写进内存的新数据
 */
bool write_nv12_frame(FILE *outputFile, const Nv12Picture &picture);
