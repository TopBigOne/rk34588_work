// 来源：3_one_year_work/N0_work/n0_nvr_mosaic/claude_reference/第02周_N0_Day6的M4和Day8上板/src/common/nv12_picture.h
#pragma once

#include <rockchip/rk_mpi.h>

#include "common/mpp_utils.h"

/**
 * @brief 一帧 NV12 画面：画面在哪块 MppBuffer 里，以及 RGA、编码器、写文件时需要的宽高和 stride
 * @note 只是"描述"，不负责释放 mppBuf。谁申请的谁释放（allocNv12Buffer 返回的 MppBufferPtr，或者解码器的内存池）
 */
struct Nv12Picture {
    MppBuffer mppBuf = nullptr; // 画面所在的 MppBuffer
    int width = 0;              // 画面宽，单位像素
    int height = 0;             // 画面高，单位像素
    int horStride = 0;          // 一行在内存里占多少字节（≥ width）
    int verStride = 0;          // Y 平面在内存里占多少行（≥ height）；UV 平面从第 verStride 行开始
};

/**
 * @brief 画面里的一个矩形，单位像素，左上角是 (0, 0)
 * @note 给 RGA 用时，NV12 的 x、y、width、height 都必须是偶数（UV 是 2×2 个像素共用一组）
 */
struct PictureRect {
    int x = 0;      // 左上角横坐标
    int y = 0;      // 左上角纵坐标
    int width = 0;  // 宽
    int height = 0; // 高
};

/**
 * @brief 按"宽高都对齐到 16"的规则，描述一帧自己申请的 NV12 画面（还没有 buffer）
 * @param[in] width   画面宽，单位像素，例如 1920
 * @param[in] height  画面高，单位像素，例如 1080
 * @return mppBuf 为空的 Nv12Picture；stride = ALIGN(width, 16) × ALIGN(height, 16)，1080p 是 1920 × 1088
 * @note 解码器给的帧不用这个函数，它的 stride 由解码器决定，用 describeDecodedFrame 取
 */
Nv12Picture makeAlignedNv12Picture(int width, int height);

/**
 * @brief 从 MPP 的 buffer 组申请一块能放下 picture 的 buffer，并把它填进 picture.mppBuf
 * @param[in]     bufferGroup  从哪个组申请（DRM 类型，RGA、编码器都能用它的 dma-buf fd）
 * @param[in,out] picture      [in] 宽高和 stride；[out] mppBuf 指向新申请的 buffer
 * @return 管理这块 buffer 的 MppBufferPtr，离开作用域时自动 mpp_buffer_put；申请失败返回空（已打印原因）
 * @note 大小 = horStride × verStride × 3 / 2。返回值要接住，丢掉的话 buffer 马上就被释放
 */
MppBufferPtr allocNv12Buffer(MppBufferGroup bufferGroup, Nv12Picture &picture);

/**
 * @brief 把解码器输出的一帧描述成 Nv12Picture：取出 buffer、宽高、stride，不复制像素
 * @param[in]  decodedFrame  decode_get_frame 得到的帧，必须带 buffer（不能是只带 EOS 的空帧）
 * @param[out] picture       描述结果；mppBuf 指向解码器内存池里的 buffer，归解码器管，不要释放
 * @return true = 成功；false = 不是 NV12，或者没有 buffer（已打印原因）
 * @note picture 只在 decodedFrame 还活着的时候有效：帧释放后 buffer 回到解码器内存池，会被后面的帧覆盖
 */
bool describeDecodedFrame(MppFrame decodedFrame, Nv12Picture &picture);

/**
 * @brief 用 CPU 把整张画面涂黑：Y 平面全写 16，UV 平面全写 128
 * @param[in] picture  要涂黑的画面，buffer 必须已经申请好
 * @return true = 成功；false = 拿不到 CPU 地址（已打印原因）
 * @note 连 stride 填充的部分一起写，整块 buffer 都是确定的值。
 *       没用 RGA 的 imfill：RK3588 上填色只有 RGA2 核能做，而 RGA2 只能访问 4GB 以内的内存，
 *       板子有 16GB，MPP 申请的 buffer 不一定在 4GB 以内（2026-11-17 实测 imfill 失败，见 RGA FAQ Q4.5）
 */
bool fillNv12PictureBlack(const Nv12Picture &picture);
