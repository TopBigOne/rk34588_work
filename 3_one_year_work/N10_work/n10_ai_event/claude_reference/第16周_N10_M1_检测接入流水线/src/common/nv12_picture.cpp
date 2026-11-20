// 来源：3_one_year_work/N0_work/n0_nvr_mosaic/claude_reference/第02周_N0_Day6的M4和Day8上板/src/common/nv12_picture.cpp
#include "common/nv12_picture.h"

#include <cstdint> // uint8_t
#include <cstring> // memset

#include "common/log.h"

static const char *const kTag = "Nv12";

/**
 * @brief 描述一帧宽高对齐到 16 的 NV12 画面（还没有 buffer）
 * @param[in] width   画面宽，单位像素
 * @param[in] height  画面高，单位像素
 * @return mppBuf 为空、stride 对齐到 16 的 Nv12Picture
 * @note 对齐到 16：MPP 编码器要求 stride 是 16 的倍数（H.264/H.265 的宏块、CTU 按 16 起步）
 */
Nv12Picture makeAlignedNv12Picture(int width, int height) {
    Nv12Picture picture;
    picture.mppBuf = nullptr;
    picture.width = width;
    picture.height = height;
    picture.horStride = ALIGN(width, 16);
    picture.verStride = ALIGN(height, 16);
    return picture;
}

/**
 * @brief 从 buffer 组申请一块 NV12 buffer
 * @param[in]     bufferGroup  从哪个组申请
 * @param[in,out] picture      [in] 宽高和 stride；[out] mppBuf
 * @return 管理这块 buffer 的 MppBufferPtr；失败返回空
 * @note 失败时把 picture.mppBuf 清成 nullptr，调用方不会拿到一个"半成品"
 */
MppBufferPtr allocNv12Buffer(MppBufferGroup bufferGroup, Nv12Picture &picture) {
    const size_t frameSizeBytes = static_cast<size_t>(picture.horStride) * picture.verStride * 3 / 2;
    picture.mppBuf = nullptr;
    if (mpp_buffer_get(bufferGroup, &picture.mppBuf, frameSizeBytes) != MPP_OK) {
        LOGE(kTag, "mpp_buffer_get %zu 字节失败", frameSizeBytes);
        picture.mppBuf = nullptr;
        return nullptr;
    }
    return MppBufferPtr(picture.mppBuf);
}

/**
 * @brief 把解码帧描述成 Nv12Picture
 * @param[in]  decodedFrame  解码器输出的帧
 * @param[out] picture       描述结果
 * @return true = 成功；false = 格式不对或没有 buffer
 * @note 第 1 步 查格式；第 2 步 取 buffer；第 3 步 取宽高和 stride
 */
bool describeDecodedFrame(MppFrame decodedFrame, Nv12Picture &picture) {
    const MppFrameFormat frameFormat = mpp_frame_get_fmt(decodedFrame);
    const int pixelFormat = frameFormat & MPP_FRAME_FMT_MASK;
    if (pixelFormat != MPP_FMT_YUV420SP) {
        LOGE(kTag, "解码帧不是 NV12：fmt = 0x%x", static_cast<unsigned>(frameFormat));
        return false;
    }

    MppBuffer frameBuffer = mpp_frame_get_buffer(decodedFrame);
    if (!frameBuffer) {
        LOGE(kTag, "解码帧没有 buffer（只带 EOS 的空帧不该走到这里）");
        return false;
    }

    picture.mppBuf = frameBuffer;
    picture.width = static_cast<int>(mpp_frame_get_width(decodedFrame));
    picture.height = static_cast<int>(mpp_frame_get_height(decodedFrame));
    picture.horStride = static_cast<int>(mpp_frame_get_hor_stride(decodedFrame));
    picture.verStride = static_cast<int>(mpp_frame_get_ver_stride(decodedFrame));
    return true;
}

/**
 * @brief 用 CPU 把整张画面涂黑
 * @param[in] picture  要涂黑的画面
 * @return true = 成功；false = 失败
 * @note 第 1 步 拿到 CPU 地址；第 2 步 写 Y 平面；第 3 步 写 UV 平面。
 *       视频里的黑色不是 Y = 0：8 bit 视频的亮度只用 16～235 这一段（"有限范围"），16 就是黑；
 *       UV 表示颜色偏离灰色多少，128 = 不偏，也就是没有颜色
 */
bool fillNv12PictureBlack(const Nv12Picture &picture) {
    uint8_t *frameStart = static_cast<uint8_t *>(mpp_buffer_get_ptr(picture.mppBuf));
    if (!frameStart) {
        LOGE(kTag, "mpp_buffer_get_ptr 失败");
        return false;
    }
    mpp_buffer_sync_begin(picture.mppBuf);

    const size_t yPlaneSizeBytes = static_cast<size_t>(picture.horStride) * picture.verStride;
    memset(frameStart, 16, yPlaneSizeBytes);

    memset(frameStart + yPlaneSizeBytes, 128, yPlaneSizeBytes / 2);

    mpp_buffer_sync_end(picture.mppBuf);
    return true;
}
