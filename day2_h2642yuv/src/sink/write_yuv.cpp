// 【扫一眼】逐行 fwrite，M3 里自己写过
#include "sink/write_yuv.h"

#include <cstdint> // uint8_t
#include <cstdio>  // fopen / fwrite / printf

/**
 * @brief 创建输出文件（"wb"：已存在就清空重写）
 * @param path  文件路径
 * @return 成功返回 true；打开失败打印原因，返回 false
 * @note 【扫一眼】
 */
bool WriteYuv::open(const char* path) {
    file_.reset(fopen(path, "wb"));
    if (!file_) {
        printf("open %s failed\n", path);
        return false;
    }
    return true;
}

/**
 * @brief 写一帧 NV12：Y 平面 height 行 + UV 平面 height/2 行，每行只写 width 字节（跳过 stride 填充）
 * @param frame  正常的图像帧（有 buffer）。只借用，不保存
 * @return 全部写成功返回 true；没有 buffer 或 fwrite 写不满返回 false
 * @note 【扫一眼】M3 里自己写过的 write_nv12_frame
 */
bool WriteYuv::write(MppFrame frame) {
    const RK_U32 width     = mpp_frame_get_width(frame);      // 1920
    const RK_U32 height    = mpp_frame_get_height(frame);     // 1080
    const RK_U32 horStride = mpp_frame_get_hor_stride(frame); // 1920
    const RK_U32 verStride = mpp_frame_get_ver_stride(frame); // 1088
    MppBuffer buffer       = mpp_frame_get_buffer(frame);
    if (!buffer) {
        return false;
    }
    FILE* fp = file_.get();

    // void* 转成 uint8_t*，才能按字节做指针运算
    const auto* base = static_cast<const uint8_t*>(mpp_buffer_get_ptr(buffer));
    for (RK_U32 row = 0; row < height; row++) { // Y：1080 行
        if (fwrite(base + row * horStride, 1, width, fp) != width) {
            return false;
        }
    }
    const uint8_t* uv = base + horStride * verStride; // UV 从第 1088 行开始
    for (RK_U32 row = 0; row < height / 2; row++) {   // UV：540 行
        if (fwrite(uv + row * horStride, 1, width, fp) != width) {
            return false;
        }
    }
    return true;
}
