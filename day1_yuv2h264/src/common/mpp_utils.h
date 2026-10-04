/*
 * mpp_utils.h：各个模块共用的小工具（encoder、source、sink 都会用到）
 *
 *   - ALIGN       向上对齐
 *   - MPP_CHECK   检查 MPP_RET，失败就打印并 return
 *   - XxxPtr      RAII 句柄：把 FILE* / MppBufferGroup / MppBuffer / MppFrame / MppPacket
 *                 包进 std::unique_ptr，离开作用域时自动 fclose / put / deinit
 *
 * 为什么要 RAII：
 *   原来的 main() 用 goto CLEANUP 统一释放，所有变量都得挤在第一个 CHECK 之前声明，
 *   CLEANUP 里还要逐个 if 判断。换成 RAII 以后，"谁申请、谁负责释放"写在类型里，
 *   任何地方 return 都不会漏释放，也不会重复释放
 */
#pragma once

#include <cstdio> // FILE / fclose / printf
#include <memory> // std::unique_ptr
#include <rockchip/rk_mpi.h>

// 向上对齐到 a 的整数倍（a 必须是 2 的幂）。例：ALIGN(1080, 16) = 1088
// MPP 自己的 MPP_ALIGN 在内部头文件 osal/inc/mpp_common.h 里，不对外提供，所以自己定义一个
#define ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))

// 调用一个返回 MPP_RET 的函数，不等于 MPP_OK 就打印出错的那一句和返回值，然后 return failValue。
// MPP_RET：MPP_OK = 0 表示成功；负数表示失败，比如 MPP_NOK = -1、MPP_ERR_NULL_PTR = -3、MPP_ERR_VALUE = -6
// 直接 return 是安全的：已经申请的资源都在 RAII 句柄里，函数返回时会自动释放（不再需要 goto CLEANUP）
#define MPP_CHECK(expr, failValue)                                             \
    do {                                                                       \
        MPP_RET _r = (expr);                                                   \
        if (_r != MPP_OK) {                                                    \
            printf("[FAIL] %s ret=%d (line %d)\n", #expr, (int) _r, __LINE__); \
            return failValue;                                                  \
        }                                                                      \
    } while (0)

// ==================== RAII 句柄 ====================
// std::unique_ptr<T, Deleter>：独占一个指针，析构时调用 Deleter 释放它；不能拷贝，只能 std::move
// MppBufferGroup / MppBuffer / MppFrame / MppPacket 都是 void*（见 guide/07），所以 T 写 void
// unique_ptr 只有在指针不为空时才会调用 Deleter，Deleter 里不用再判断 nullptr

// FILE*：析构时 fclose（fclose 会把缓冲区里还没写下去的数据写进文件）
struct FileCloser {
    void operator()(FILE* fp) const { fclose(fp); }
};
using FilePtr = std::unique_ptr<FILE, FileCloser>;

// MppBufferGroup（内存池）：析构时 mpp_buffer_group_put 销毁内存池
// ⚠️ 必须在从它申请的 MppBuffer 都 put 之后再销毁，所以声明时要放在 MppBufferPtr 前面（先声明的后析构）
struct MppBufferGroupDeleter {
    void operator()(MppBufferGroup group) const { mpp_buffer_group_put(group); }
};
using MppBufferGroupPtr = std::unique_ptr<void, MppBufferGroupDeleter>;

// MppBuffer：析构时 mpp_buffer_put，引用计数 -1，减到 0 才真正还给内存池
// （mpp_buffer_put 是宏，没法直接当函数指针传给 unique_ptr，所以包一层）
struct MppBufferDeleter {
    void operator()(MppBuffer buffer) const { mpp_buffer_put(buffer); }
};
using MppBufferPtr = std::unique_ptr<void, MppBufferDeleter>;

// MppFrame：析构时 mpp_frame_deinit。deinit 的参数是 MppFrame*（要把它置成 NULL），所以传局部变量的地址
struct MppFrameDeleter {
    void operator()(MppFrame frame) const { mpp_frame_deinit(&frame); }
};
using MppFramePtr = std::unique_ptr<void, MppFrameDeleter>;

// MppPacket：析构时 mpp_packet_deinit（用 buffer 包出来的 packet，deinit 只是让 buffer 的引用计数 -1）
struct MppPacketDeleter {
    void operator()(MppPacket packet) const { mpp_packet_deinit(&packet); }
};
using MppPacketPtr = std::unique_ptr<void, MppPacketDeleter>;
