/*
 * FrameSink：输出接口 ——"接收解码出来的图像"
 *
 * 策略模式：DecodePipeline 只认这个接口，不关心图像送到哪。
 *   - 现在：WriteYuv（写进 NV12 文件）
 *   - 以后：送显示（DRM plane）、送 RGA 缩放、送编码器转码，实现同样的接口，DecodePipeline 不用改
 */
#pragma once

#include <rockchip/rk_mpi.h>

class FrameSink {
public:
    // 接口类的析构函数必须是 virtual：通过 FrameSink& 销毁对象时，才会调用到子类的析构函数
    virtual ~FrameSink() = default;

    /**
     * 输出一帧图像
     * @param frame  正常的图像帧（有 buffer，没有 errinfo / discard）。
     *               只是借用：所有权还在调用方，write 返回后调用方会 mpp_frame_deinit，不要保存它
     * @return  成功返回 true；失败返回 false
     */
    virtual bool write(MppFrame frame) = 0;
};
