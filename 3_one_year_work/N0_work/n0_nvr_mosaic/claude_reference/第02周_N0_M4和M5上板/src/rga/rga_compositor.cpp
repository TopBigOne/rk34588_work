#include "rga/rga_compositor.h"

#include <cstring> // imcheck 是宏，展开后用到 memset，RGA 的头文件自己没 include
#include <rga/im2d.h>
#include <rga/rga.h>

#include "common/log.h"

static const char *const kTag = "RgaCompositor";

/**
 * @brief 把一块 MppBuffer 导入 RGA，得到 RGA 认识的 handle
 * @param[in] mppBuf  要导入的 MppBuffer（DRM 类型，有 dma-buf fd）
 * @return RGA handle；0 = 导入失败
 * @note 只是让 RGA 认识这块内存（通过 dma-buf fd），不复制数据。用完要 releasebuffer_handle
 */
static rga_buffer_handle_t importMppBufferToRga(MppBuffer mppBuf) {
    const int dmaBufFd = mpp_buffer_get_fd(mppBuf);                         // 这块内存的 dma-buf fd
    const int bufferSizeBytes = static_cast<int>(mpp_buffer_get_size(mppBuf)); // 这块内存一共多少字节
    return importbuffer_fd(dmaBufFd, bufferSizeBytes);
}

/**
 * @brief 把 Nv12Picture 包成 RGA 的图像描述
 * @param[in] handle   importMppBufferToRga 得到的 handle
 * @param[in] picture  画面的宽高和 stride
 * @return RGA 的图像描述（rga_buffer_t）
 * @note wrapbuffer_handle(handle, 宽, 高, 格式, 一行多少像素, 一共多少行)：最后两个就是 horStride、verStride。
 *       RGA 靠 verStride 算出 UV 平面从哪里开始，填错了颜色就错位
 */
static rga_buffer_t wrapNv12Picture(rga_buffer_handle_t handle, const Nv12Picture &picture) {
    return wrapbuffer_handle(handle, picture.width, picture.height, RK_FORMAT_YCbCr_420_SP, picture.horStride,
                             picture.verStride);
}

/**
 * @brief 把 PictureRect 换成 RGA 的 im_rect
 * @param[in] rect  我们自己的矩形
 * @return RGA 的矩形，字段一一对应
 * @note 不让 pipeline 层直接用 RGA 的类型，换 RGA 版本或者换成别的硬件时只改这个文件
 */
static im_rect toImRect(const PictureRect &rect) {
    im_rect imRect;
    imRect.x = rect.x;
    imRect.y = rect.y;
    imRect.width = rect.width;
    imRect.height = rect.height;
    return imRect;
}

/**
 * @brief 缩放后贴进格子
 * @param[in] srcPicture  源画面
 * @param[in] dstPicture  目标大图
 * @param[in] dstRect     目标格子
 * @return true = 成功；false = 失败
 * @note 第 1 步 导入两块 buffer；第 2 步 包成 RGA 图像；第 3 步 检查参数；第 4 步 缩放贴图；第 5 步 释放 handle
 */
bool resizeNv12IntoRectByRga(const Nv12Picture &srcPicture, const Nv12Picture &dstPicture, const PictureRect &dstRect) {
    // 第 1 步 导入两块 buffer
    const rga_buffer_handle_t srcHandle = importMppBufferToRga(srcPicture.mppBuf);
    const rga_buffer_handle_t dstHandle = importMppBufferToRga(dstPicture.mppBuf);
    if (srcHandle == 0 || dstHandle == 0) {
        LOGE(kTag, "importbuffer_fd 失败：src handle %u，dst handle %u", srcHandle, dstHandle);
        if (srcHandle != 0) {
            releasebuffer_handle(srcHandle);
        }
        if (dstHandle != 0) {
            releasebuffer_handle(dstHandle);
        }
        return false;
    }

    // 第 2 步 包成 RGA 图像。源画面用它自己的 stride：H.264 1080p 是 1920×1088，H.265 1080p 是 2304×1080
    const rga_buffer_t srcImage = wrapNv12Picture(srcHandle, srcPicture);
    const rga_buffer_t dstImage = wrapNv12Picture(dstHandle, dstPicture);
    const im_rect srcImRect = toImRect({0, 0, srcPicture.width, srcPicture.height}); // 源：整张画面
    const im_rect dstImRect = toImRect(dstRect);                                    // 目标：大图里的一个格子

    // 第 3 步 检查参数：格式、对齐、缩放倍数这台 RGA 支不支持。不支持时 imcheck 返回错误，不会去动硬件
    bool isComposeOk = false;
    const IM_STATUS checkStatus = imcheck(srcImage, dstImage, srcImRect, dstImRect);
    if (checkStatus != IM_STATUS_NOERROR) {
        LOGE(kTag, "imcheck：%s", imStrError(checkStatus));
    } else {
        // 第 4 步 缩放贴图。improcess(源, 目标, 第三张图, 源矩形, 目标矩形, 第三张图的矩形, usage)：
        //   第三张图（pat）用来做混合，这里不用，传空的 {}；
        //   源矩形和目标矩形大小不一样时，RGA 自动缩放；目标矩形以外的像素不动；
        //   IM_SYNC：等 RGA 做完才返回
        const IM_STATUS processStatus = improcess(srcImage, dstImage, {}, srcImRect, dstImRect, {}, IM_SYNC);
        if (processStatus == IM_STATUS_SUCCESS) {
            isComposeOk = true;
        } else {
            LOGE(kTag, "improcess：%s", imStrError(processStatus));
        }
    }

    // 第 5 步 释放 handle：只是让 RGA 忘掉这两块内存，内存本身还归原来的主人
    releasebuffer_handle(srcHandle);
    releasebuffer_handle(dstHandle);
    return isComposeOk;
}

