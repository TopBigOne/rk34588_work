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
    // NV12 一帧的大小：Y 平面 horStride × verStride 字节，UV 平面是它的一半
    const size_t frameSizeBytes = static_cast<size_t>(picture.horStride) * picture.verStride * 3 / 2;
    picture.mppBuf = nullptr;
    // mpp_buffer_get 是宏，展开成 mpp_buffer_get_with_tag(组, &buffer, 大小, 模块名, 函数名)
    if (mpp_buffer_get(bufferGroup, &picture.mppBuf, frameSizeBytes) != MPP_OK) {
        LOGE(kTag, "mpp_buffer_get %zu 字节失败", frameSizeBytes);
        picture.mppBuf = nullptr;
        return nullptr;
    }
    // 交给 MppBufferPtr 管：它离开作用域时调用 mpp_buffer_put（见 mpp_utils.h 的 MppBufferDeleter）
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
    // 第 1 步 查格式。fmt 的高位可能带别的标志（比如 FBC 压缩），先 & MPP_FRAME_FMT_MASK 只留低位的"像素格式"
    const MppFrameFormat frameFormat = mpp_frame_get_fmt(decodedFrame);
    const int pixelFormat = frameFormat & MPP_FRAME_FMT_MASK;
    if (pixelFormat != MPP_FMT_YUV420SP) {
        LOGE(kTag, "解码帧不是 NV12：fmt = 0x%x", static_cast<unsigned>(frameFormat));
        return false;
    }

    // 第 2 步 取 buffer：解码器内存池里的一块，像素就在里面
    MppBuffer frameBuffer = mpp_frame_get_buffer(decodedFrame);
    if (!frameBuffer) {
        LOGE(kTag, "解码帧没有 buffer（只带 EOS 的空帧不该走到这里）");
        return false;
    }

    // 第 3 步 取宽高和 stride。stride 由解码器决定：1080p 的 H.264 是 1920 × 1088
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
    // 第 1 步 mpp_buffer_get_ptr：MPP 内部用 mmap 把这块 buffer 映射到我们的进程，返回 CPU 能用的地址
    uint8_t *frameStart = static_cast<uint8_t *>(mpp_buffer_get_ptr(picture.mppBuf));
    if (!frameStart) {
        LOGE(kTag, "mpp_buffer_get_ptr 失败");
        return false;
    }
    // CPU 写之前 sync_begin、写完 sync_end：buffer 带 cache 的话，sync_end 把 CPU cache 里的新内容写回内存，
    // 之后 RGA、编码器从内存里读到的才是黑色
    mpp_buffer_sync_begin(picture.mppBuf);

    // 第 2 步 Y 平面：horStride × verStride 字节
    const size_t yPlaneSizeBytes = static_cast<size_t>(picture.horStride) * picture.verStride;
    memset(frameStart, 16, yPlaneSizeBytes);

    // 第 3 步 UV 平面：紧跟在 Y 平面后面，大小是 Y 平面的一半
    memset(frameStart + yPlaneSizeBytes, 128, yPlaneSizeBytes / 2);

    mpp_buffer_sync_end(picture.mppBuf);
    return true;
}
