#include "overlay/nv12_frame_overlay.h"

#include <cstdio>

#include "common/log.h"
#include "common/mpp_utils.h"
#include "common/nv12_picture.h"

static const char *const kTag = "Nv12Overlay";

static bool describeFramePlanes(MppFrame decodedFrame, Nv12Picture &picture, Nv12PlaneView &planeView) {
    if (!describeDecodedFrame(decodedFrame, picture)) {
        return false;
    }
    uint8_t *frameStart = static_cast<uint8_t *>(mpp_buffer_get_ptr(picture.mppBuf));
    if (frameStart == nullptr) {
        LOGE(kTag, "mpp_buffer_get_ptr 失败");
        return false;
    }
    planeView.yPlane = frameStart;
    planeView.uvPlane = frameStart + static_cast<size_t>(picture.horStride) * picture.verStride;  // UV 从第 verStride 行开始
    planeView.width = picture.width;
    planeView.height = picture.height;
    planeView.horStride = picture.horStride;
    return true;
}

int drawDetectionBoxesOnFrame(MppFrame decodedFrame, const std::vector<DetectedObject> &objects, int onlyClassIndex,
                              const Nv12Color &color, int lineWidthPixels) {
    Nv12Picture picture;
    Nv12PlaneView planeView;
    if (!describeFramePlanes(decodedFrame, picture, planeView)) {
        return 0;
    }
    int drawnBoxCount = 0;
    // sync_begin：先让 CPU cache 里的旧内容作废（解码器刚用硬件写过）；sync_end：把 CPU 改的写回内存，别的硬件才看得到
    mpp_buffer_sync_begin(picture.mppBuf);
    for (const DetectedObject &object : objects) {
        if (onlyClassIndex >= 0 && object.classIndex != onlyClassIndex) {
            continue;
        }
        drawNv12Rectangle(planeView, object.box, color, lineWidthPixels);
        drawnBoxCount++;
    }
    mpp_buffer_sync_end(picture.mppBuf);
    return drawnBoxCount;
}

bool saveFrameAsCompactNv12(MppFrame decodedFrame, const char *filePath) {
    Nv12Picture picture;
    Nv12PlaneView planeView;
    if (!describeFramePlanes(decodedFrame, picture, planeView)) {
        return false;
    }
    FilePtr filePtr(fopen(filePath, "wb"));
    if (!filePtr) {
        LOGE(kTag, "打开 %s 失败", filePath);
        return false;
    }
    mpp_buffer_sync_begin(picture.mppBuf);
    bool isWriteOk = true;
    for (int row = 0; row < picture.height && isWriteOk; row++) {
        isWriteOk = fwrite(planeView.yPlane + static_cast<size_t>(row) * picture.horStride, 1, picture.width,
                           filePtr.get()) == static_cast<size_t>(picture.width);
    }
    for (int uvRow = 0; uvRow < picture.height / 2 && isWriteOk; uvRow++) {
        isWriteOk = fwrite(planeView.uvPlane + static_cast<size_t>(uvRow) * picture.horStride, 1, picture.width,
                           filePtr.get()) == static_cast<size_t>(picture.width);
    }
    mpp_buffer_sync_end(picture.mppBuf);
    return isWriteOk;
}
