/*
 * ReadYUV：FrameSource 的实现 —— 从 NV12 原始图像文件里一帧一帧读图像，按 stride 摆进 MPP 的硬件缓冲区
 *
 * 用法：
 *   ReadYUV nv12Reader;
 *   if (!nv12Reader.open(path, 1920, 1080)) return -1;
 *   nv12Reader.prepare(horStride, verStride);              // 申请 DRM 缓冲区（EncodePipeline 会调）
 *   while (nv12Reader.read_frame(frameBuffer)) { ... }     // 返回 false = 读完了
 *   // 不用手动释放：nv12Reader 离开作用域时，缓冲区、内存池、文件按 RAII 自动释放
 */
#pragma once

#include "common/mpp_utils.h" // FilePtr、MppBufferGroupPtr、MppBufferPtr
#include "source/frame_source.h"
#include <cstdint> // uint8_t
#include <rockchip/rk_mpi.h>

class ReadYUV : public FrameSource {
public:
    /**
     * 打开 NV12 文件，并检查文件大小是不是"一帧大小"的整数倍
     *
     * NV12 文件里没有记录宽高，-w / -h 写错了程序也会照常跑完、结果却是错的。
     * 所以打开后检查一下文件大小，不是整数倍就打印 warning（只是提醒，不算失败）
     * （局限：恰好能整除时查不出来，比如 1280x720 去读 1080p 文件）
     *
     * @param path    NV12 文件路径
     * @param width   有效宽度（1920）
     * @param height  有效高度（1080）
     * @return  打开成功返回 true；打不开（比如文件不存在）打印原因，返回 false
     */
    bool open(const char* path, RK_S32 width, RK_S32 height);

    /**
     * 申请 DRM 内存池和一块装一帧 NV12 的硬件缓冲区，整个循环反复使用这一块
     * @param horStride  缓冲区里一行占多少字节（1920）
     * @param verStride  缓冲区里 Y 平面占多少行（1088）
     * @return  成功返回 true；MPP 申请失败打印出错的那一句，返回 false
     */
    bool prepare(RK_S32 horStride, RK_S32 verStride) override;

    /**
     * 读一帧到硬件缓冲区：sync_begin → 逐行读 → sync_end
     * @param frameBuffer  输出参数：装着这一帧的硬件缓冲区（就是 prepare 申请的那一块）
     * @return  读满一帧返回 true，读到文件尾部返回 false
     */
    bool read_frame(MppBuffer& frameBuffer) override;

private:
    /**
     * 按 stride 逐行读一帧 NV12，写进 MPP 的硬件缓冲区（照抄 MPP utils/utils.c 里 read_image 的 NV12 分支）
     *
     * 文件里是紧密排列的（没有填充）；硬件缓冲区里每行占 hor_stride 字节、Y 平面占 ver_stride 行（有填充），
     * 所以不能整帧一次 fread，只能一行一行读，每行放到对应的位置。
     * 1920x1080 时：文件里 UV 从第 1080 行开始，缓冲区里 UV 从第 1088 行开始（详见 nv12中yuv分布效果图和读取方式.md）
     * 读完一帧后文件位置正好停在下一帧开头，所以不需要 fseek
     *
     * @param dst  硬件缓冲区的 CPU 地址（mpp_buffer_get_ptr 拿到的）
     * @return  读满一帧返回 true，读到文件尾部返回false
     */
    bool read_nv12_rows(uint8_t* dst);

    // ⚠️ 声明顺序 = 析构顺序的反过来：缓冲区（最后声明）先 put，再销毁内存池，最后关文件
    FilePtr nv12InputFile_;            // 输入文件（NV12），析构时自动 fclose
    MppBufferGroupPtr drmBufferGroup_; // DRM 内存池：硬件能访问的内存都从这里申请
    MppBufferPtr nv12FrameBuffer_;     // 装一帧原始图像（NV12），整个循环反复使用这一块
    RK_S32 width_     = 0;             // 有效宽度
    RK_S32 height_    = 0;             // 有效高度
    RK_S32 horStride_ = 0;             // 缓冲区里一行占多少字节
    RK_S32 verStride_ = 0;             // 缓冲区里 Y 平面占多少行
};
