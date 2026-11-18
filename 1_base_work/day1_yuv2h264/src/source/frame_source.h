/*
 * FrameSource：输入接口 ——"一帧一帧提供原始图像"
 *
 * 策略模式：EncodePipeline 只认这个接口，不关心图像从哪来。
 *   - 现在：ReadYUV（从 NV12 文件读）
 *   - Day 3：V4L2Camera（从摄像头取 DMA-BUF），实现同样的接口，EncodePipeline 和 MppEncoder 不用改
 *
 * 输入缓冲区由 FrameSource 自己管理：文件输入要自己申请 DRM 内存，摄像头输入直接用摄像头给的 DMA-BUF，
 * 两者差别很大，所以"申请缓冲区"放在实现类里，不放在编码器或 main 里
 */
#pragma once

#include <rockchip/rk_mpi.h>

class FrameSource {
public:
    // 接口类的析构函数必须是 virtual：通过 FrameSource& 销毁对象时，才会调用到子类的析构函数
    virtual ~FrameSource() = default;

    /**
     * 按编码器要求的 stride 准备输入缓冲区。在第一次 read_frame 之前调用一次
     * @param horStride  缓冲区里一行占多少字节（MppEncoder::hor_stride()）
     * @param verStride  缓冲区里 Y 平面占多少行（MppEncoder::ver_stride()）
     * @return  成功返回 true；申请失败返回 false
     */
    virtual bool prepare(RK_S32 horStride, RK_S32 verStride) = 0;

    /**
     * 取下一帧图像
     * @param frameBuffer  输出参数：装着这一帧的硬件缓冲区（CPU 写完已经 sync_end，硬件能直接读）
     * @return  取到一帧返回 true；没有更多帧了（文件读完）返回 false
     */
    virtual bool read_frame(MppBuffer& frameBuffer) = 0;
};
