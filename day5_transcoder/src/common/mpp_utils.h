//
// Created by DEV on 2026/11/9.
//

#pragma once


#include <cstdio>
#include <memory>
#include <rockchip/rk_mpi.h>
#include "common/log.h"
// 把 x 向上对齐到 a 的倍数（a 必须是 2 的幂）。例：ALIGN(1080, 16) = 1088
#define ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))

inline const char *const kMppCheckTag = "MPP_CHECK";
// 执行一个返回 MPP_RET 的调用；不是 MPP_OK 就打印是哪一句、返回码多少、第几行，然后 return failValue
#define MPP_CHECK(expr, failValue)                                                                                     \
    do {                                                                                                               \
        MPP_RET mppRet = (expr);                                                                                       \
        if (mppRet != MPP_OK) {                                                                                        \
            LOGE(kMppCheckTag, "[FAIL] %s ret=%d (line %d)", #expr, static_cast<int>(mppRet), __LINE__);               \
            return failValue;                                                                                          \
        }                                                                                                              \
    } while (0)


struct FileCloser {
    void operator()(FILE *file) const { fclose(file); }
};

using FilePtr = std::unique_ptr<FILE, FileCloser>;


/**
* 第 1 步  bufferGroupPtr 析构 → 调用 unique_ptr 的析构函数 ~unique_ptr()

第 2 步  ~unique_ptr() 里（标准库写好的代码，简化）：
             if (storedPointer != nullptr)
                 deleter(storedPointer);      ← 用 () 去“调用”删除器对象

第 3 步  编译器把 deleter(storedPointer) 翻译成
             deleter.operator()(storedPointer);
         也就是调用你写的那个名叫 operator() 的成员函数

第 4 步  operator() 的函数体执行：
             mpp_buffer_group_put(storedPointer);

第 5 步  unique_ptr 的成员 deleter 跟着析构（空结构体，什么都不做）
 *
 */
struct MppBufferGroupDeleter {
    void operator()(MppBufferGroup mppBufferGroup) const { mpp_buffer_group_put(mppBufferGroup); }
};

using MppBufferGroupPtr = std::unique_ptr<void, MppBufferGroupDeleter>;


struct MppBufferDeleter {
    void operator()(MppBuffer mppBuffer) const { mpp_buffer_put(mppBuffer); }
};
using MppBufferPtr = std::unique_ptr<void, MppBufferDeleter>;


struct MppFrameDeleter {
    void operator()(MppFrame mppFrame) const { mpp_frame_deinit(&mppFrame); }
};
using MppFramePtr = std::unique_ptr<void, MppFrameDeleter>;


struct MppPacketDeleter {
    void operator()(MppPacket mppPacket) const { mpp_packet_deinit(&mppPacket); }
};
using MppPacketPtr = std::unique_ptr<void, MppPacketDeleter>;
