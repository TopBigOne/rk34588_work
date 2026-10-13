#include "sink/write_yuv.h"

#include <cstdint> // uint8_t
#include <cstdio>  // fopen / fwrite / printf

bool WriteYuv::open(const char* path) {
    file_.reset(fopen(path, "wb"));
    if (!file_) {
        printf("open %s failed\n", path);
        return false;
    }
    return true;
}

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
