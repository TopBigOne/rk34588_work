#include "sink/nv12_file_writer.h"

#include <cerrno>  // errno
#include <cstdint> // uint8_t
#include <cstring> // strerror

#include "common/log.h"

static const char *const kTag = "Nv12Writer";

/**
 * @brief 创建输出文件
 * @param[in] filePath  文件路径
 * @return true = 成功；false = 失败
 * @note "wb"：只写、二进制，文件存在就清空
 */
bool Nv12FileWriter::open(const char *filePath) {
    filePtr.reset(fopen(filePath, "wb"));
    if (!filePtr) {
        LOGE(kTag, "打开 %s 失败：%s", filePath, strerror(errno));
        return false;
    }
    return true;
}

/**
 * @brief 写一帧
 * @param[in] picture  要写的画面
 * @return true = 成功；false = 失败
 * @note 第 1 步 拿到 CPU 能用的地址；第 2 步 写 Y 平面；第 3 步 写 UV 平面。一行一行写：每行只写 width 字节，
 *       跳过行尾的 stride 填充；UV 从第 verStride 行开始，不是第 height 行
 */
bool Nv12FileWriter::writePicture(const Nv12Picture &picture) {
    // 第 1 步 mpp_buffer_get_ptr：MPP 内部用 mmap 把这块 DRM buffer 映射到我们的进程，返回 CPU 能用的地址
    const uint8_t *frameStart = static_cast<const uint8_t *>(mpp_buffer_get_ptr(picture.mppBuf));
    if (!frameStart) {
        LOGE(kTag, "mpp_buffer_get_ptr 失败");
        return false;
    }
    // sync_begin：CPU 开始访问前调用。buffer 是带 cache 的话，丢掉 CPU cache 里的旧内容，读到 RGA 写进内存的新数据
    mpp_buffer_sync_begin(picture.mppBuf);

    // 第 2 步 Y 平面：height 行，每行 width 字节
    bool isWriteOk = true;
    for (int rowIndex = 0; rowIndex < picture.height && isWriteOk; ++rowIndex) {
        const uint8_t *rowStart = frameStart + static_cast<size_t>(rowIndex) * picture.horStride;
        isWriteOk = fwrite(rowStart, 1, picture.width, filePtr.get()) == static_cast<size_t>(picture.width);
    }

    // 第 3 步 UV 平面：从第 verStride 行开始，height / 2 行，每行 width 字节（U、V 交替）
    const uint8_t *uvStart = frameStart + static_cast<size_t>(picture.horStride) * picture.verStride;
    for (int rowIndex = 0; rowIndex < picture.height / 2 && isWriteOk; ++rowIndex) {
        const uint8_t *rowStart = uvStart + static_cast<size_t>(rowIndex) * picture.horStride;
        isWriteOk = fwrite(rowStart, 1, picture.width, filePtr.get()) == static_cast<size_t>(picture.width);
    }

    mpp_buffer_sync_end(picture.mppBuf); // CPU 访问结束，和 sync_begin 成对调用
    if (!isWriteOk) {
        LOGE(kTag, "fwrite 没写完：%s", strerror(errno));
        return false;
    }
    writtenFrameCount++;
    return true;
}
