//
// Created by DEV on 2026/11/14.
//

#include "rga_resizer.h"
#include <rga/im2d.h>
#include <rga/rga.h>
#include <cstring>   // imcheck 是宏，展开后用到 memset，RGA 头文件自己没 include

/**
 * 主要函数：
 * 1. importbuffer_fd();
 * -------------------------
 * 2.wrapbuffer_handle();
 * -------------------------
 * 3.imcheck();
 * -------------------------
 * 4.imresize();
 * -------------------------
 * 5.releasebuffer_handle()l
 * -------------------------
 */
static const char *const kTag = "RGA";
static rga_buffer_handle_t importMppBufferToRga(MppBuffer mppBuf) {
    const int dmaFd = mpp_buffer_get_fd(mppBuf);
    int bufferSize = static_cast<int>(mpp_buffer_get_size(mppBuf));
    return importbuffer_fd(dmaFd, bufferSize);
}

bool resizeNv12ByRga(const Nv12Picture &src, Nv12Picture &dst) {
    // step 1:
    rga_buffer_handle_t srcHandle = importMppBufferToRga(src.mppBuf);
    rga_buffer_handle_t dstHandle = importMppBufferToRga(dst.mppBuf);
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

    // step 2:
    const rga_buffer_t srcImage =
            wrapbuffer_handle(srcHandle, src.width, src.height, RK_FORMAT_YCbCr_420_SP, src.horStride, src.verStride);
    const rga_buffer_t dstImage =
            wrapbuffer_handle(dstHandle, dst.width, dst.height, RK_FORMAT_YCbCr_420_SP, dst.horStride, dst.verStride);

    // step 3:
    bool isResizeOk = false;
    const IM_STATUS checkStatus = static_cast<IM_STATUS>(imcheck(srcImage, dstImage, {}, {}));
    if (checkStatus != IM_STATUS_NOERROR) {
        LOGE(kTag, "imcheck：%s", imStrError(checkStatus));
    } else {
        IM_STATUS imStatus = imresize(srcImage, dstImage);
        if (imStatus == IM_STATUS_SUCCESS) {
            isResizeOk = true;
        } else {
            LOGE(kTag, "imresize：%s", imStrError(imStatus));
        }
    }
    releasebuffer_handle(srcHandle);
    releasebuffer_handle(dstHandle);
    return isResizeOk;
}
