#include "video/rga_color_converter.h"

#include <cstring> // imcheck 是宏，展开后用到 memset，RGA 的头文件自己没 include
#include <rga/im2d.h>
#include <rga/rga.h>
#include <utility> // std::move

#include "common/log.h"

static const char *const kTag = "RgaColorConverter";

/**
 * @brief NV12 → RGBX + 缩放
 * @param[in]  nv12Frame      解码帧
 * @param[in]  outputWidth    输出宽
 * @param[in]  outputHeight   输出高
 * @param[out] outputPicture  输出画面
 * @return true = 成功；false = 失败
 * @note 第 1 步 申请输出内存；第 2 步 两块内存导入 RGA；第 3 步 包成 RGA 图像；第 4 步 检查参数；
 *       第 5 步 转换加缩放；第 6 步 释放 handle
 */
bool convertNv12FrameToRgbxByRga(MppFrame nv12Frame, int outputWidth, int outputHeight, RgbxPicture &outputPicture) {
    MppBuffer frameBuffer = mpp_frame_get_buffer(nv12Frame); // 这一帧画面所在的 buffer（属于解码器内存池）
    if (!frameBuffer) {
        LOGE(kTag, "帧上没有 buffer");
        return false;
    }

    // 第 1 步 申请输出内存：RGBX 每个像素 4 字节，一行 outputWidth × 4 字节，不留填充
    const int outputStrideBytes = outputWidth * 4;
    auto pixelBufferPtr = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(outputStrideBytes) * outputHeight);

    // 第 2 步 导入：让 RGA 认识这两块内存，得到 handle（只是登记，不复制数据）
    //   源：importbuffer_fd(dma-buf fd, 字节数)，解码器内存池是 DRM 类型，每块都有 fd
    //   目标：importbuffer_virtualaddr(虚拟地址, 字节数)，普通的 new / malloc 出来的内存，RGA 通过 IOMMU 访问
    const int frameBufferFd = mpp_buffer_get_fd(frameBuffer);
    const int frameBufferSizeBytes = static_cast<int>(mpp_buffer_get_size(frameBuffer));
    const rga_buffer_handle_t srcHandle = importbuffer_fd(frameBufferFd, frameBufferSizeBytes);
    const rga_buffer_handle_t dstHandle =
            importbuffer_virtualaddr(pixelBufferPtr->data(), static_cast<int>(pixelBufferPtr->size()));
    if (srcHandle == 0 || dstHandle == 0) {
        LOGE(kTag, "导入 buffer 失败：src handle %u，dst handle %u", srcHandle, dstHandle);
        if (srcHandle != 0) {
            releasebuffer_handle(srcHandle);
        }
        if (dstHandle != 0) {
            releasebuffer_handle(dstHandle);
        }
        return false;
    }

    // 第 3 步 包成 RGA 图像：wrapbuffer_handle(handle, 宽, 高, 格式, 一行多少像素, 一共多少行)。
    //   源用解码器给的 stride（H.264 1080p 是 1920×1088：高度按 16 对齐多出 8 行），RGA 靠 verStride 找到 UV 平面
    //   目标的一行就是 outputWidth 个像素，没有多余的行
    const int frameWidth = static_cast<int>(mpp_frame_get_width(nv12Frame));
    const int frameHeight = static_cast<int>(mpp_frame_get_height(nv12Frame));
    const int frameHorStride = static_cast<int>(mpp_frame_get_hor_stride(nv12Frame));
    const int frameVerStride = static_cast<int>(mpp_frame_get_ver_stride(nv12Frame));
    rga_buffer_t srcImage = wrapbuffer_handle(srcHandle, frameWidth, frameHeight, RK_FORMAT_YCbCr_420_SP,
                                              frameHorStride, frameVerStride);
    //   目标格式用 RGBA_8888，不用 RGBX_8888：两者每个像素都是 R、G、B、第 4 字节，内存排列一样；
    //   但 RK3588 的两个 RGA3 核心输出格式里只有 RGBA 没有 RGBX（内核 drivers/video/rockchip/rga3/rga_hw_config.c
    //   第 37 行 rga3_output_raster_format），写 RGBX 只能交给 RGA2，而 RGA2 只能访问 4G 以内的内存，
    //   我们 new 出来的目标内存在 16G 的板子上常在 4G 以上 → 驱动报 "RGA2 only support under 4G memory!"、
    //   "invalid function policy"，每帧都失败（2026-11-19 上板实测，开 /sys/kernel/debug/rkrga/debug 的 msg 日志看到）。
    //   第 4 字节 RGA 写什么都无所谓：界面那边 QImage 用 Format_RGBX8888，不看第 4 字节
    rga_buffer_t dstImage =
            wrapbuffer_handle(dstHandle, outputWidth, outputHeight, RK_FORMAT_RGBA_8888, outputWidth, outputHeight);
    // color_space_mode：YUV 转 RGB 用哪套公式。BT.709 LIMIT = 高清视频的标准（Y 的范围 16～235）。
    // 不设的话默认按 BT.601（标清的标准）算，颜色会稍微偏。imcvtcolor 内部也是这样设在目标图像上的
    // （linux-rga/im2d_api/src/im2d.cpp 第 885 行）
    dstImage.color_space_mode = IM_YUV_TO_RGB_BT709_LIMIT;
    const im_rect srcRect = {0, 0, frameWidth, frameHeight};    // 源：整张画面
    const im_rect dstRect = {0, 0, outputWidth, outputHeight};  // 目标：整张输出

    // 第 4 步 检查参数：格式、对齐、缩放倍数这台 RGA 支不支持。不支持时 imcheck 返回错误，不会去动硬件
    bool isConvertOk = false;
    const IM_STATUS checkStatus = imcheck(srcImage, dstImage, srcRect, dstRect);
    if (checkStatus != IM_STATUS_NOERROR) {
        LOGE(kTag, "imcheck：%s（输出 %dx%d）", imStrError(checkStatus), outputWidth, outputHeight);
    } else {
        // 第 5 步 improcess(源, 目标, 第三张图, 源矩形, 目标矩形, 第三张图的矩形, usage)：
        //   格式不一样 → RGA 做颜色转换；矩形大小不一样 → RGA 同时缩放。两件事一次做完；
        //   第三张图（混合用）不用，传空的 {}；IM_SYNC：等 RGA 做完才返回
        const IM_STATUS processStatus = improcess(srcImage, dstImage, {}, srcRect, dstRect, {}, IM_SYNC);
        if (processStatus == IM_STATUS_SUCCESS) {
            isConvertOk = true;
        } else {
            LOGE(kTag, "improcess：%s", imStrError(processStatus));
        }
    }

    // 第 6 步 释放 handle：只是让 RGA 忘掉这两块内存，内存本身还归原来的主人
    releasebuffer_handle(srcHandle);
    releasebuffer_handle(dstHandle);
    if (!isConvertOk) {
        return false;
    }

    outputPicture.pixelBufferPtr = std::move(pixelBufferPtr);
    outputPicture.width = outputWidth;
    outputPicture.height = outputHeight;
    outputPicture.strideBytes = outputStrideBytes;
    outputPicture.ptsUs = mpp_frame_get_pts(nv12Frame);
    return true;
}
