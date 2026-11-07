//
// Nv12Picture 的申请和读写。从 main.cpp 搬过来，逻辑没改
//
#include "common/nv12_picture.h"

#include "common/log.h"

static const char *kTag = "Nv12";

Nv12Picture make_aligned_nv12_picture(int width, int height) {
    return {.mppBuf = nullptr,
            .width = width,
            .height = height,
            .horStride = ALIGN(width, 16),
            .verStride = ALIGN(height, 16)};
}

MppBufferPtr alloc_nv12_buffer(MppBufferGroup bufferGroup, Nv12Picture &picture) {
    // NV12 一帧的大小：Y 占 horStride × verStride，UV 占它的一半
    const size_t frameSizeBytes = static_cast<size_t>(picture.horStride) * picture.verStride * 3 / 2;
    picture.mppBuf = nullptr;
    if (mpp_buffer_get(bufferGroup, &picture.mppBuf, frameSizeBytes) != MPP_OK) {
        LOGE(kTag, "mpp_buffer_get %zu 字节失败", frameSizeBytes);
        picture.mppBuf = nullptr;
        return nullptr;
    }
    // 交给 MppBufferPtr 管：它离开作用域时调用 mpp_buffer_put（见 mpp_utils.h 的 MppBufferDeleter）
    return MppBufferPtr(picture.mppBuf);
}

/*
 * Q1：每一圈的 frameStart 一样吗？→ 一样
 * Q2：写新内容，能把上一次的数据全部擦掉吗？→ 画面部分全部覆盖，填充部分不碰也不需要碰；
 *     真正可能让上一帧"残留"的是 CPU cache，靠 sync_end 写回 DRAM 来解决
 */
bool read_nv12_frame(FILE *inputFile, const Nv12Picture &picture) {
    uint8_t *frameStart = static_cast<uint8_t *>(mpp_buffer_get_ptr(picture.mppBuf));
    // CPU 要写 cached 的 buffer：写之前 sync_begin，写之后 sync_end，保证 RGA、编码器读到的是 CPU 刚写的
    mpp_buffer_sync_begin(picture.mppBuf);
    bool isReadOk = true;
    // Y 平面：height 行
    for (int rowIndex = 0; rowIndex < picture.height && isReadOk; ++rowIndex) {
        isReadOk = fread(frameStart + rowIndex * picture.horStride, 1, picture.width, inputFile) ==
                   static_cast<size_t>(picture.width);
    }
    // UV 平面：从第 verStride 行开始，height / 2 行
    uint8_t *uvStart = frameStart + picture.horStride * picture.verStride;
    for (int rowIndex = 0; rowIndex < picture.height / 2 && isReadOk; ++rowIndex) {
        isReadOk = fread(uvStart + rowIndex * picture.horStride, 1, picture.width, inputFile) ==
                   static_cast<size_t>(picture.width);
    }
    mpp_buffer_sync_end(picture.mppBuf);
    return isReadOk;
}

bool write_nv12_frame(FILE *outputFile, const Nv12Picture &picture) {
    const uint8_t *frameStart = static_cast<const uint8_t *>(mpp_buffer_get_ptr(picture.mppBuf));
    mpp_buffer_sync_begin(picture.mppBuf);
    bool isWriteOk = true;
    for (int rowIndex = 0; rowIndex < picture.height && isWriteOk; ++rowIndex) {
        isWriteOk = fwrite(frameStart + rowIndex * picture.horStride, 1, picture.width, outputFile) ==
                    static_cast<size_t>(picture.width);
    }
    const uint8_t *uvStart = frameStart + picture.horStride * picture.verStride;
    for (int rowIndex = 0; rowIndex < picture.height / 2 && isWriteOk; ++rowIndex) {
        isWriteOk = fwrite(uvStart + rowIndex * picture.horStride, 1, picture.width, outputFile) ==
                    static_cast<size_t>(picture.width);
    }
    mpp_buffer_sync_end(picture.mppBuf);
    return isWriteOk;
}
