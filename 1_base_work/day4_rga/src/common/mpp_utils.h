//
// Created by DEV on 2026/11/3.
//

#pragma once
#include <cstdio>
#include <memory>
#include <rockchip/rk_mpi.h>
#include "log.h"
#define ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))
inline const char *TAG_MPP_CHECK = "MPP_CHECK";

#define MPP_CHECK(expr, failValue)                                                                                     \
    do {                                                                                                               \
        MPP_RET _r = (expr);                                                                                           \
        if (_r != MPP_OK) {                                                                                            \
            LOGE(TAG_MPP_CHECK, "[FAIL] %s ret=%d (line %d)", #expr, (int) _r, __LINE__);                            \
            return failValue;                                                                                          \
        }                                                                                                              \
    } while (0)


/**
 * fclose 会把缓冲区里还没写下去的数据写进文件
 */
struct FileCloser {
    void operator()(FILE *fp) const { fclose(fp); }
};
using FilePtr = std::unique_ptr<FILE, FileCloser>;

struct MppBufferGroupDeleter {
    void operator()(MppBufferGroup group) const { mpp_buffer_group_put(group); }
};
using MppBufferGroupPtr = std::unique_ptr<void, MppBufferGroupDeleter>;

struct MppBufferDeleter {
    void operator()(MppBuffer buffer) const { mpp_buffer_put(buffer); }
};
using MppBufferPtr = std::unique_ptr<void, MppBufferDeleter>;


struct MppFrameDeleter {
    void operator()(MppFrame frame) { mpp_frame_deinit(&frame); }
};

using MppFramePtr = std::unique_ptr<void, MppFrameDeleter>;

struct MppPacketDeleter {
    void operator()(MppPacket packet) const { mpp_packet_deinit(&packet); }
};
using MppPacketPtr = std::unique_ptr<void, MppPacketDeleter>;
