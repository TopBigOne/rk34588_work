//
// Created by DEV on 2026/11/9.
//

#include "common/nv12_picture.h"
#include "common/log.h"

static const char *const kTag = "Nv12";


Nv12Picture makeAlignedNv12Picture(const int width, const int height) {
    Nv12Picture nv12Picture;
    nv12Picture.mppBuf = nullptr;
    nv12Picture.width = width;
    nv12Picture.height = height;
    nv12Picture.horStride = ALIGN(width, 16);
    nv12Picture.verStride = ALIGN(height, 16);
    return nv12Picture;
}

MppBufferPtr allocNv12Buffer(MppBufferGroup bufferGroup, Nv12Picture &picture) {
    const size_t frameSizeBytes = static_cast<size_t>(picture.horStride) * picture.verStride * 3 / 2;
    picture.mppBuf = nullptr;
    if (mpp_buffer_get(bufferGroup, &picture.mppBuf, frameSizeBytes) != MPP_OK) {
        picture.mppBuf = nullptr;
        return nullptr;
    }
    return MppBufferPtr(picture.mppBuf);
}
/**
 * 将解码帧，描述成Nv12Picture
 * @param decodedFrame
 * @param picture
 * @return
 */
bool describeDecodedFrame(MppFrame decodedFrame, Nv12Picture &picture) {
    auto frameFormat = mpp_frame_get_fmt(decodedFrame);
    int pixelFormat = frameFormat & MPP_FRAME_FMT_MASK;
    if (pixelFormat != MPP_FMT_YUV420SP) {
        LOGE(kTag, " 解码帧不是 NV12：fmt = 0x%x", static_cast<unsigned>(frameFormat));
        return false;
    }
    MppBuffer frameBuffer = mpp_frame_get_buffer(decodedFrame);
    if (!frameBuffer) {
        LOGE(kTag, " 解码帧没有 buffer（只带 EOS 的空帧不该走到这里）");
        return false;
    }
    picture.mppBuf = frameBuffer;
    picture.width = static_cast<int>(mpp_frame_get_width(decodedFrame));
    picture.height = static_cast<int>(mpp_frame_get_height(decodedFrame));
    picture.horStride = static_cast<int>(mpp_frame_get_hor_stride(decodedFrame));
    picture.verStride = static_cast<int>(mpp_frame_get_ver_stride(decodedFrame));
    return true;
}
