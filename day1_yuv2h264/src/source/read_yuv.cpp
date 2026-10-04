/*
 * ReadYUV 的实现，说明见 read_yuv.h
 */
#include "source/read_yuv.h"

#include <cstdio> // fopen / fread / fseek / ftell / printf

// 这个文件里的函数都返回 bool：MPP 调用失败就打印并 return false
#define CHECK(expr) MPP_CHECK(expr, false)

bool ReadYUV::open(const char* path, RK_S32 width, RK_S32 height) {
    nv12InputFile_.reset(fopen(path, "rb"));
    if (!nv12InputFile_) {
        printf("open %s failed\n", path);
        return false;
    }
    width_  = width;
    height_ = height;

    // 输入文件里一帧的大小（紧密排列，没有 stride）。NV12：Y 占 1 份，UV 一共占 0.5 份
    const long nv12FileFrameSize = static_cast<long>(width) * height * 3 / 2;
    fseek(nv12InputFile_.get(), 0, SEEK_END);
    const long nv12FileSize = ftell(nv12InputFile_.get());
    rewind(nv12InputFile_.get()); // 回到文件开头，后面从第 0 帧开始读
    if (nv12FileSize % nv12FileFrameSize != 0) {
        printf("warning: 输入文件 %ld 字节，不是一帧 %ld 字节（%dx%d NV12）的整数倍，-w / -h 写对了吗？\n",
            nv12FileSize, nv12FileFrameSize, width, height);
    }
    return true;
}

bool ReadYUV::prepare(RK_S32 horStride, RK_S32 verStride) {
    horStride_ = horStride;
    verStride_ = verStride;

    // 创建内存池：
    //   MPP_BUFFER_TYPE_DRM        : 通过 DRM 分配，硬件编码器能通过 DMA 直接读写（malloc 的内存硬件访问不了）
    //   MPP_BUFFER_FLAGS_CACHABLE  : CPU 读写走缓存（快），代价是 CPU 写完要 mpp_buffer_sync_end 把缓存刷下去
    // MPP 的函数通过输出参数返回句柄，所以先用一个临时变量接住，成功后再交给 RAII 句柄
    MppBufferGroup rawGroup = nullptr;
    CHECK(mpp_buffer_group_get_internal(&rawGroup, MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE));
    drmBufferGroup_.reset(rawGroup);

    // 因为是nv12，所以才：3 / 2（Y 占 1 份，UV 一共占 0.5 份）
    // 硬件缓冲区再按 64 对齐一次，和官方 mpi_enc_test.c 的 test_ctx_init 一样：1920 × 1088 × 3/2 = 3,133,440
    const size_t mppFrameBufSize = ALIGN(horStride, 64) * ALIGN(verStride, 64) * 3 / 2;
    MppBuffer rawBuffer          = nullptr;
    CHECK(mpp_buffer_get(drmBufferGroup_.get(), &rawBuffer, mppFrameBufSize));
    nv12FrameBuffer_.reset(rawBuffer);
    return true;
}

bool ReadYUV::read_frame(MppBuffer& frameBuffer) {
    frameBuffer = nv12FrameBuffer_.get();
    // 硬件缓冲区的 CPU 地址。mpp_buffer_get_ptr 返回 void*，转成 uint8_t* 才能按字节加偏移
    auto* nv12FrameCpuAddr = static_cast<uint8_t*>(mpp_buffer_get_ptr(frameBuffer));

    // sync_begin / sync_end 包住 CPU 的写操作：
    // 缓冲区是 CACHABLE 的，CPU 写的数据可能还停在缓存里，sync_end 把它刷到内存，硬件才看得到。
    // 不调 sync_end：硬件读到旧数据，画面花屏、残影
    mpp_buffer_sync_begin(frameBuffer);
    const bool ok = read_nv12_rows(nv12FrameCpuAddr);
    mpp_buffer_sync_end(frameBuffer);
    return ok;
}

bool ReadYUV::read_nv12_rows(uint8_t* dst) {
    FILE* fp                = nv12InputFile_.get();
    const RK_S32 hor_stride = horStride_;
    const RK_S32 ver_stride = verStride_;
    // ⚠️️️⚠️️️⚠️️️： 以下是核心，也是我恐惧的地方
    // case 1: 读取Y：height行，每行读取width 字节，写到row*hor_stride 的位置
    //         （每行末尾 hor_stride - width 个字节是填充，不读）
    for (int row = 0; row < height_; row++) {
        // C++17 的 if 初始化写法：先读，再判断读到的字节数够不够一行
        if (const size_t readSize = fread(dst + row * hor_stride, 1, width_, fp);
            readSize != static_cast<size_t>(width_)) {
            return false;
        }
    }
    // case 2 :读取UV，从ver_stride（1088，不是1080） 行开始
    //          只有height/2 行，但是每行还是width字节（U，V交错，UVUVUVUV..）
    uint8_t* dst_uv = dst + hor_stride * ver_stride;
    for (int row = 0; row < height_ / 2; row++) {
        if (const size_t readSize = fread(dst_uv + row * hor_stride, 1, width_, fp);
            readSize != static_cast<size_t>(width_)) {
            return false;
        }
    }
    return true;
}
