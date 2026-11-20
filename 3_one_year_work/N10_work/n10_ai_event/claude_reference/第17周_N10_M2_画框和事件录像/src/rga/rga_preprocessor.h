#pragma once

#include <cstddef>
#include <cstdint>

#include <rga/im2d.h>
#include <rockchip/rk_mpi.h>

#include "common/mpp_utils.h"
#include "npu/letterbox.h"

// 解码出来的 NV12 帧 → 模型输入大小的 RGB888，一次 RGA 调用同时完成缩放和颜色转换。
// 目标 buffer 是 init 时申请的一块 DRM MppBuffer（RGA 要 fd），CPU 通过 mpp_buffer_get_ptr 读给 rknn_inputs_set。
// letterbox 的灰边：RK3588 上 RGA 填色走不通（Day 6 imfill 失败），所以几何变化时 CPU 整块 memset 成 padValue，
// 之后 RGA 每帧只写中间那块矩形，灰边一直保留。
class RgaPreprocessor {
public:
    RgaPreprocessor() = default;
    ~RgaPreprocessor();
    RgaPreprocessor(const RgaPreprocessor &) = delete;
    RgaPreprocessor &operator=(const RgaPreprocessor &) = delete;

    bool init(int requestedModelWidth, int requestedModelHeight, uint8_t requestedPadValue);
    bool convertToModelInput(MppFrame decodedFrame, bool isLetterboxEnabled, LetterboxInfo &letterboxInfo);
    const uint8_t *getRgbData() const;  // 下一次 convertToModelInput 之前有效
    size_t getRgbSizeBytes() const { return rgbSizeBytes; }
    bool saveRgbToFile(const char *filePath) const;  // 存裸 RGB，Mac 上 ffmpeg -f rawvideo -pix_fmt rgb24 -s 640x640 转 PNG

private:
    bool fillWholeRgbBuffer();

    int modelWidth = 0;
    int modelHeight = 0;
    uint8_t padValue = 114;
    size_t rgbSizeBytes = 0;
    MppBufferGroupPtr rgbGroupPtr;
    MppBufferPtr rgbBufferPtr;
    uint8_t *rgbAddress = nullptr;      // mpp_buffer_get_ptr 返回的 CPU 地址
    rga_buffer_handle_t rgbHandle = 0;  // importbuffer_fd 得到的 RGA 句柄，init 时导入一次，析构时释放
    LetterboxInfo lastLetterboxInfo;    // 上一帧的几何；变了就要重新铺灰边
    bool hasLastLetterboxInfo = false;
};
