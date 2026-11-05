//
// Created by DEV on 2026/11/4.
//

#include "rga_resizer.h"
#include <rga/im2d.h>
#include <rga/rga.h>
#include "../common/log.h"
#include "cstring"
static const char *kTag = "RGA";

/**
 * 把一块MppBuffer 导入RGA ,得到RGA自己的handle
 * @param mppBuf
 * @return
 */
static rga_buffer_handle_t import_mpp_buffer_to_rga(MppBuffer mppBuf) {
    int dmaFd = mpp_buffer_get_fd(mppBuf);
    //  mpp_buffer_get_size：这块 buffer 一共多少字节
    const int sizeBytes = static_cast<int>(mpp_buffer_get_size(mppBuf));
    // importbuffer_fd：告诉 RGA 驱动"以后用这块内存"，返回一个编号（handle）。后面 RGA 的操作都认 handle
    return importbuffer_fd(dmaFd, sizeBytes);
}

bool resize_nv12_by_rga(const Nv12Picture &srcPicture, const Nv12Picture &dstPicture) {
    // step1: 导入
    rga_buffer_handle_t handleSrc = import_mpp_buffer_to_rga(srcPicture.mppBuf);
    rga_buffer_handle_t handleDst = import_mpp_buffer_to_rga(dstPicture.mppBuf);

    if (handleSrc == 0 || handleDst == 0) {
        LOGE(kTag, "importbuffer_fd 失败：src handle %u，dst handle %u", handleSrc, handleDst);
        if (handleDst != 0) {
            releasebuffer_handle(handleDst);
        }
        if (handleSrc) {
            releasebuffer_handle(handleSrc);
        }
        return false;
    }

    // step2 : 描述画面
    rga_buffer_t imageSrc = wrapbuffer_handle(handleSrc, srcPicture.width, srcPicture.height, RK_FORMAT_YCbCr_420_SP,
                                              srcPicture.horStride, srcPicture.verStride);
    rga_buffer_t imageDst = wrapbuffer_handle(handleDst, dstPicture.width, dstPicture.height, RK_FORMAT_YCbCr_420_SP,
                                              dstPicture.horStride, dstPicture.verStride);
    // step3： 通过imcheck() 参数
    bool isResizeOK = false;
    const IM_STATUS checkRet = static_cast<IM_STATUS>(imcheck(imageSrc, imageDst, {}, {}));
    if (checkRet != IM_STATUS_NOERROR) {
        LOGE(kTag, "imcheck: %s", imStrError(checkRet));
        releasebuffer_handle(handleSrc);
        releasebuffer_handle(handleDst);
        return false;
    }
    // step4: 开始缩放
    IM_STATUS resizeRet = imresize(imageSrc, imageDst);
    isResizeOK = resizeRet == IM_STATUS_SUCCESS;
    if (!isResizeOK) {
        LOGE(kTag, "imresize: %s", imStrError(resizeRet));
    }

    // step 5: release
    releasebuffer_handle(handleSrc);
    releasebuffer_handle(handleDst);

    return isResizeOK;
}
