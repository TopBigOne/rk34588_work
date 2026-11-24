// 来源：3_one_year_work/N0_work/n0_nvr_mosaic/claude_reference/第02周_N0_M4和M5上板/src/common/mpp_utils.h
#pragma once

#include <cstdio> // FILE、fclose
#include <memory> // std::unique_ptr
#include <rockchip/rk_mpi.h>

#include "common/log.h"

#define ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))

inline const char *const kMppCheckTag = "MPP_CHECK";

#define MPP_CHECK(expr, failValue)                                                                                     \
    do {                                                                                                               \
        MPP_RET mppRet = (expr);                                                                                       \
        if (mppRet != MPP_OK) {                                                                                        \
            LOGE(kMppCheckTag, "[FAIL] %s ret=%d (line %d)", #expr, static_cast<int>(mppRet), __LINE__);               \
            return failValue;                                                                                          \
        }                                                                                                              \
    } while (0)

/** FilePtr 的删除器：unique_ptr 析构时调用 operator()，关闭文件 */
struct FileCloser {
    /**
     * @brief 关闭文件
     * @param[in] file  fopen 返回的文件指针（unique_ptr 保证不为空才调用）
     * @return 无
     * @note 由 unique_ptr 在析构或 reset 时自动调用，不要手动调用
     */
    void operator()(FILE *file) const { fclose(file); }
};
using FilePtr = std::unique_ptr<FILE, FileCloser>;

/** MppBufferGroupPtr 的删除器：析构时把整个 buffer 组还给 MPP */
struct MppBufferGroupDeleter {
    /**
     * @brief 释放 buffer 组
     * @param[in] bufferGroup  mpp_buffer_group_get_internal 得到的组
     * @return 无
     * @note 组里的 buffer 要先都还回来（MppBufferPtr 先析构），再释放组
     */
    void operator()(MppBufferGroup bufferGroup) const { mpp_buffer_group_put(bufferGroup); }
};
using MppBufferGroupPtr = std::unique_ptr<void, MppBufferGroupDeleter>;

/** MppBufferPtr 的删除器：析构时把 buffer 还给它所在的组 */
struct MppBufferDeleter {
    /**
     * @brief 归还一块 buffer
     * @param[in] buffer  mpp_buffer_get 得到的 buffer
     * @return 无
     * @note mpp_buffer_put 是"引用计数减 1"，减到 0 才真正回到组里
     */
    void operator()(MppBuffer buffer) const { mpp_buffer_put(buffer); }
};
using MppBufferPtr = std::unique_ptr<void, MppBufferDeleter>;

/** MppFramePtr 的删除器：析构时释放 MppFrame */
struct MppFrameDeleter {
    /**
     * @brief 释放一个 MppFrame
     * @param[in] frame  decode_get_frame 或 mpp_frame_init 得到的帧
     * @return 无
     * @note mpp_frame_deinit 要的是 MppFrame*（地址），所以写 &frame。
     *       解码器给的帧带着解码器内存池里的一块 buffer，deinit 时这块 buffer 回到池子，解码器才能拿它解后面的帧
     */
    void operator()(MppFrame frame) const { mpp_frame_deinit(&frame); }
};
using MppFramePtr = std::unique_ptr<void, MppFrameDeleter>;

/** MppPacketPtr 的删除器：析构时释放 MppPacket */
struct MppPacketDeleter {
    /**
     * @brief 释放一个 MppPacket
     * @param[in] packet  mpp_packet_init 或 encode_get_packet 得到的包
     * @return 无
     * @note mpp_packet_deinit 要的也是地址，写 &packet
     */
    void operator()(MppPacket packet) const { mpp_packet_deinit(&packet); }
};
using MppPacketPtr = std::unique_ptr<void, MppPacketDeleter>;
