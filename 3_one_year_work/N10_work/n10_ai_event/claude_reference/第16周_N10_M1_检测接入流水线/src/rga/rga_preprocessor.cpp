#include "rga/rga_preprocessor.h"

#include <cstdio>
#include <cstring>  // imcheck 宏展开要 memset

#include <rga/rga.h>

#include "common/log.h"
#include "common/nv12_picture.h"

static const char *const kTag = "RgaPreprocessor";

RgaPreprocessor::~RgaPreprocessor() {
    if (rgbHandle != 0) {
        releasebuffer_handle(rgbHandle);
        rgbHandle = 0;
    }
    // rgbBufferPtr 比 rgbGroupPtr 后声明、先析构：buffer 先还给组，组再释放
}

bool RgaPreprocessor::init(int requestedModelWidth, int requestedModelHeight, uint8_t requestedPadValue) {
    modelWidth = requestedModelWidth;
    modelHeight = requestedModelHeight;
    padValue = requestedPadValue;
    rgbSizeBytes = static_cast<size_t>(modelWidth) * modelHeight * 3;

    MppBufferGroup rawGroup = nullptr;
    MPP_CHECK(mpp_buffer_group_get_internal(&rawGroup, MPP_BUFFER_TYPE_DRM), false);
    rgbGroupPtr.reset(rawGroup);
    MppBuffer rawBuffer = nullptr;
    MPP_CHECK(mpp_buffer_get(rawGroup, &rawBuffer, rgbSizeBytes), false);
    rgbBufferPtr.reset(rawBuffer);

    rgbAddress = static_cast<uint8_t *>(mpp_buffer_get_ptr(rawBuffer));
    if (rgbAddress == nullptr) {
        LOGE(kTag, "mpp_buffer_get_ptr 失败");
        return false;
    }
    rgbHandle = importbuffer_fd(mpp_buffer_get_fd(rawBuffer), static_cast<int>(rgbSizeBytes));
    if (rgbHandle == 0) {
        LOGE(kTag, "importbuffer_fd（RGB 目标）失败");
        return false;
    }
    hasLastLetterboxInfo = false;
    return fillWholeRgbBuffer();
}

bool RgaPreprocessor::fillWholeRgbBuffer() {
    // CPU 写之前 sync_begin、写完 sync_end：sync_end 把 cache 里的新内容写回内存，RGA 才看得到（N0 fillNv12PictureBlack 同样做法）
    mpp_buffer_sync_begin(rgbBufferPtr.get());
    memset(rgbAddress, padValue, rgbSizeBytes);
    mpp_buffer_sync_end(rgbBufferPtr.get());
    return true;
}

static bool isSameGeometry(const LetterboxInfo &first, const LetterboxInfo &second) {
    return first.resizedWidth == second.resizedWidth && first.resizedHeight == second.resizedHeight &&
           first.padLeftPixels == second.padLeftPixels && first.padTopPixels == second.padTopPixels;
}

bool RgaPreprocessor::convertToModelInput(MppFrame decodedFrame, bool isLetterboxEnabled, LetterboxInfo &letterboxInfo) {
    Nv12Picture sourcePicture;
    if (!describeDecodedFrame(decodedFrame, sourcePicture)) {
        return false;
    }
    letterboxInfo = computeLetterboxInfo(sourcePicture.width, sourcePicture.height, modelWidth, modelHeight,
                                         isLetterboxEnabled);
    // 几何变了（第一帧、分辨率变了、拉伸和 letterbox 切换）：灰边位置变了，整块重新铺灰
    if (!hasLastLetterboxInfo || !isSameGeometry(letterboxInfo, lastLetterboxInfo)) {
        fillWholeRgbBuffer();
        lastLetterboxInfo = letterboxInfo;
        hasLastLetterboxInfo = true;
    }

    // 解码器的 buffer 是轮流用的，每帧导入一次、用完释放（导入只是登记 fd，不拷贝数据）
    rga_buffer_handle_t sourceHandle = importbuffer_fd(mpp_buffer_get_fd(sourcePicture.mppBuf),
                                                       static_cast<int>(mpp_buffer_get_size(sourcePicture.mppBuf)));
    if (sourceHandle == 0) {
        LOGE(kTag, "importbuffer_fd（解码帧）失败");
        return false;
    }
    // 源按 stride 描述（1080p H.264 是 1920×1088），只取左上角 width×height 的有效画面
    rga_buffer_t sourceImage = wrapbuffer_handle(sourceHandle, sourcePicture.width, sourcePicture.height,
                                                 RK_FORMAT_YCbCr_420_SP, sourcePicture.horStride,
                                                 sourcePicture.verStride);
    rga_buffer_t rgbImage = wrapbuffer_handle(rgbHandle, modelWidth, modelHeight, RK_FORMAT_RGB_888, modelWidth,
                                              modelHeight);
    im_rect sourceRect = {0, 0, sourcePicture.width, sourcePicture.height};
    im_rect rgbRect = {letterboxInfo.padLeftPixels, letterboxInfo.padTopPixels, letterboxInfo.resizedWidth,
                       letterboxInfo.resizedHeight};

    bool isConvertOk = false;
    IM_STATUS checkStatus = imcheck(sourceImage, rgbImage, sourceRect, rgbRect);
    if (checkStatus != IM_STATUS_NOERROR) {
        LOGE(kTag, "imcheck：%s", imStrError(checkStatus));
    } else {
        // 格式不同（NV12 → RGB888）时 RGA 自动做颜色空间转换；IM_SYNC = 等 RGA 做完再返回
        IM_STATUS processStatus = improcess(sourceImage, rgbImage, {}, sourceRect, rgbRect, {}, IM_SYNC);
        if (processStatus == IM_STATUS_SUCCESS) {
            isConvertOk = true;
        } else {
            LOGE(kTag, "improcess：%s", imStrError(processStatus));
        }
    }
    releasebuffer_handle(sourceHandle);
    if (isConvertOk) {
        // RGA 写完、CPU 要读：sync_begin 让 CPU cache 里的旧内容作废，之后读到的是 RGA 写的新数据。
        // 马上 sync_end：CPU 只读不写，没有要写回的内容（推断：这样配对对只读场景足够，上板看 .rgb 确认）
        mpp_buffer_sync_begin(rgbBufferPtr.get());
        mpp_buffer_sync_end(rgbBufferPtr.get());
    }
    return isConvertOk;
}

const uint8_t *RgaPreprocessor::getRgbData() const {
    return rgbAddress;
}

bool RgaPreprocessor::saveRgbToFile(const char *filePath) const {
    FilePtr filePtr(fopen(filePath, "wb"));
    if (!filePtr) {
        LOGE(kTag, "打开 %s 失败", filePath);
        return false;
    }
    return fwrite(rgbAddress, 1, rgbSizeBytes, filePtr.get()) == rgbSizeBytes;
}
