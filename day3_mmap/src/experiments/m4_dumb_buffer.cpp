//
// Created by DEV on 2026/11/2.
//

#include "m4_dumb_buffer.h"

#include "common/log.h"
#include "common/maps_printer.h"
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
static const char* kTag               = "M4";
static const char* kDrmDevicePath     = "/dev/dri/card0";
static const uint32_t kRowValueFactor = 10000;
static const uint32_t kCheckRowIndex  = 10;

/**
 * @brief 释放 create_and_map_dumb() 申请的 dumb buffer：先 munmap，再 DESTROY_DUMB
 * @param[in]     drmFd   申请时用的那个 drmFd
 * @param[in,out] dumbBuf 要释放的 buffer，释放后 mmapAddress 置 nullptr、handle 置 0
 * @return 无
 * @note 对只申请了、还没映射的 buffer 也能调（mmapAddress 是 nullptr 就跳过 munmap）
 */
void unmap_and_destroy_dumb(int drmFd, DumbBuffer& dumbBuf) {
    if (dumbBuf.mmapAddress != nullptr) {
        // munmap(地址, 长度)：长度要和 mmap 时一样
        munmap(dumbBuf.mmapAddress, dumbBuf.sizeBytes);
        dumbBuf.mmapAddress = nullptr;
    }
    if (dumbBuf.handle != 0) {
        // DESTROY_DUMB：告诉驱动"这个 handle 我不用了"。
        //   显存要等所有使用者都放手才真正释放：比如导出的 dma_buf 还有人拿着，就先不释放（引用计数，推断和 M2 一样）
        drm_mode_destroy_dumb destroyDumb = {};
        destroyDumb.handle                = dumbBuf.handle;
        ioctl(drmFd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroyDumb);
        dumbBuf.handle = 0;
    }
}

/**
 * @brief 申请一块 dumb buffer，并 mmap 到本进程
 * @param[in]  drmFd   open("/dev/dri/card0") 得到的 fd
 * @param[in]  width   宽，单位像素
 * @param[in]  height  高，单位像素
 * @param[out] dumbBuf 成功时填好 handle、pitchBytes、sizeBytes、mmapOffset、mmapAddress
 * @return true = 成功；false = 任何一步失败（已经申请到的显存在函数里释放掉）
 * @note 用完要调 unmap_and_destroy_dumb() 释放
 */
bool create_and_map_dumb(int drmFd, uint32_t width, uint32_t height, DumbBuffer& dumbBuf) {
    // 第 1 步 申请：drm_mode_create_dumb 是 CREATE_DUMB 用的表。
    //   "dumb" = 笨的：只是一块能放像素的内存，GPU 不会在上面做加速运算，CPU 写好后交给显示控制器显示
    //   应用填 width、height、bpp；驱动写回 handle、pitch、size
    drm_mode_create_dumb createDumb = {};
    createDumb.width                = width;
    createDumb.height               = height;
    // bpp（bits per pixel）：每个像素多少位。32 位 = 4 字节，对应 XRGB8888（X 不用、R、G、B 各 8 位）
    createDumb.bpp = 32;
    // DRM_IOCTL_MODE_CREATE_DUMB = DRM_IOWR(0xB2, struct drm_mode_create_dumb)：WR，应用填、内核写回。
    //   这一步驱动就把显存分配好了，和摄像头 REQBUFS 一样：内存有了，但本进程还没有地址
    if (ioctl(drmFd, DRM_IOCTL_MODE_CREATE_DUMB, &createDumb) != 0) {
        LOGE(kTag, "DRM_IOCTL_MODE_CREATE_DUMB %ux%u: %s", width, height, strerror(errno));
        return false;
    }
    dumbBuf.handle     = createDumb.handle;
    dumbBuf.pitchBytes = createDumb.pitch;
    dumbBuf.sizeBytes  = createDumb.size;

    // 第 2 步 问 offset：和摄像头 QUERYBUF 写回 mem_offset 是一回事。
    //   应用填 handle（要哪一块），驱动写回 offset：mmap 时拿它告诉驱动"我要映射的是这一块"
    drm_mode_map_dumb mapDumb = {};
    mapDumb.handle            = dumbBuf.handle;
    // DRM_IOCTL_MODE_MAP_DUMB = DRM_IOWR(0xB3, struct drm_mode_map_dumb)：只是查一个编号，不分配内存
    if (ioctl(drmFd, DRM_IOCTL_MODE_MAP_DUMB, &mapDumb) != 0) {
        LOGE(kTag, "DRM_IOCTL_MODE_MAP_DUMB: %s", strerror(errno));
        unmap_and_destroy_dumb(drmFd, dumbBuf);
        return false;
    }
    dumbBuf.mmapOffset = mapDumb.offset;

    // 第 3 步 映射：和摄像头一样是 mmap(设备 fd, 驱动给的 offset)，6 个参数的意思见 camera_buffers.cpp。
    //   参数 2 用 CREATE_DUMB 写回的 size，不是 宽 × 高 × 4：pitch 比 宽 × 4 大时，size 也跟着大
    //   参数 5 drmFd：内核凭它找到 DRM 驱动；参数 6 mmapOffset：驱动凭它找到是哪一块
    void* mmapAddress = mmap(nullptr, dumbBuf.sizeBytes, PROT_READ | PROT_WRITE, MAP_SHARED, drmFd, dumbBuf.mmapOffset);
    // 失败返回 MAP_FAILED（就是 (void*)-1），不是 nullptr
    if (mmapAddress == MAP_FAILED) {
        LOGE(kTag, "mmap dumb buffer: %s", strerror(errno));
        unmap_and_destroy_dumb(drmFd, dumbBuf);
        return false;
    }
    dumbBuf.mmapAddress = mmapAddress;
    return true;
}

/**
 * @brief 按 pitch 一行一行写满像素，每个像素写 行号 × kRowValueFactor + 列号
 * @param[in] dumbBuf 已经 mmap 好的 dumb buffer
 * @param[in] width   宽，单位像素（每行只写这么多个像素，行末的填充不写）
 * @param[in] height  高，单位像素
 * @return 无
 */
void fill_pixels_by_pitch(const DumbBuffer& dumbBuf, uint32_t width, uint32_t height) {
    // 用 uint8_t*（按字节走）算每行的起点：pitchBytes 的单位是字节。
    //   如果直接用 uint32_t* 加 rowIndex * pitchBytes，指针会走 4 倍远（指针加 1 = 走一个元素的大小）
    uint8_t* frameStart = static_cast<uint8_t*>(dumbBuf.mmapAddress);
    for (uint32_t rowIndex = 0; rowIndex < height; ++rowIndex) {
        // 第 rowIndex 行的起点 = 整块起点 + 行号 × pitch。这就是 M4 要记住的那一行：乘 pitch，不是乘 宽 × 4
        uint32_t* rowPixels = reinterpret_cast<uint32_t*>(frameStart + rowIndex * dumbBuf.pitchBytes);
        for (uint32_t columnIndex = 0; columnIndex < width; ++columnIndex) {
            rowPixels[columnIndex] = rowIndex * kRowValueFactor + columnIndex;
        }
    }
}

/**
 * @brief 找第 rowIndex 行的第一个像素：按 pitch 找一次、按 宽 × 4 找一次，打印两次读到的是第几行第几列
 * @param[in] dumbBuf  已经用 fill_pixels_by_pitch() 写满的 dumb buffer
 * @param[in] width    宽，单位像素
 * @param[in] rowIndex 要找第几行（从 0 数）
 * @return 无
 * @note pitch 等于 宽 × 4 时两次结果一样；pitch 更大时，按 宽 × 4 找会落到前面某一行的中间
 */
void print_row_start_by_two_strides(const DumbBuffer& dumbBuf, uint32_t width, uint32_t rowIndex) {
    const uint8_t* frameStart = static_cast<const uint8_t*>(dumbBuf.mmapAddress);
    // 对：按驱动给的 pitch 算这一行的起点
    const uint32_t byPitchValue = *reinterpret_cast<const uint32_t*>(frameStart + rowIndex * dumbBuf.pitchBytes);
    // 错：以为每行正好 宽 × 4 字节、行和行之间没有填充
    const uint32_t byWidthValue = *reinterpret_cast<const uint32_t*>(frameStart + rowIndex * width * 4);
    // 读到的值 ÷ kRowValueFactor = 行号，% kRowValueFactor = 列号（fill_pixels_by_pitch 就是这样写进去的）
    LOGI(kTag,
        "    找第 %u 行第 0 列：按 pitch（%u × %u）读到第 %u 行第 %u 列；按 宽 × 4（%u × %u）读到第 %u 行第 %u 列",
        rowIndex, rowIndex, dumbBuf.pitchBytes, byPitchValue / kRowValueFactor, byPitchValue % kRowValueFactor,
        rowIndex, width * 4, byWidthValue / kRowValueFactor, byWidthValue % kRowValueFactor);
}

/**
 * @brief 把 dumb buffer 导出成 dma_buf fd（PRIME），再 mmap 一次，验证两个指针指向同一块显存
 * @param[in] drmFd   申请 dumb buffer 时用的 drmFd
 * @param[in] dumbBuf 已经 mmap 好的 dumb buffer（pointerA 就是它的 mmapAddress）
 * @return true = 导出、映射成功，并且两边互相看得到对方写的值；false = 任何一步失败或看不到
 * @note 函数里 mmap 的 pointerB 和导出的 dmaBufFd 都在函数结束前释放
 */
bool check_export_to_dma_buf(int drmFd, const DumbBuffer& dumbBuf) {
    // PRIME：DRM 里"handle ↔ dma_buf fd"互相转换的机制。
    //   handle 只在本 drmFd 里有意义；转成 dma_buf fd 以后，就能交给别的驱动（编码器、RGA）或者别的进程
    drm_prime_handle primeHandle = {};
    primeHandle.handle           = dumbBuf.handle; // 输入：要导出哪一块
    // flags：新 fd 的属性。DRM_CLOEXEC = exec 时自动关掉（和 O_CLOEXEC 一样）；
    //   DRM_RDWR = 导出的 dma_buf 可读可写。不加就是只读，下面 PROT_WRITE 的 mmap 会失败（推断和 M2 只读 EXPBUF 一样是
    //   EACCES）
    primeHandle.flags = DRM_CLOEXEC | DRM_RDWR;
    // DRM_IOCTL_PRIME_HANDLE_TO_FD = DRM_IOWR(0x2d, struct drm_prime_handle)：驱动写回 primeHandle.fd。
    //   和摄像头 VIDIOC_EXPBUF 是同一件事：把驱动自己的 buffer 包成 dma_buf，给一个 fd
    if (ioctl(drmFd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &primeHandle) != 0) {
        LOGE(kTag, "DRM_IOCTL_PRIME_HANDLE_TO_FD: %s", strerror(errno));
        return false;
    }
    const int dmaBufFd = primeHandle.fd;
    LOGI(kTag, "    PRIME 导出：handle %u -> dmaBufFd %d", dumbBuf.handle, dmaBufFd);

    // mmap(dmaBufFd, 0)：dma_buf 的 offset 永远填 0（一个 fd 就是一整块，不用再选哪一块），和 M2 的 pointerB 一样
    void* mmapAddress = mmap(nullptr, dumbBuf.sizeBytes, PROT_READ | PROT_WRITE, MAP_SHARED, dmaBufFd, 0);
    if (mmapAddress == MAP_FAILED) {
        LOGE(kTag, "mmap dmaBufFd: %s", strerror(errno));
        close(dmaBufFd);
        return false;
    }
    uint32_t* pointerA = static_cast<uint32_t*>(dumbBuf.mmapAddress); // mmap(drmFd, mmapOffset) 得到的
    uint32_t* pointerB = static_cast<uint32_t*>(mmapAddress); // mmap(dmaBufFd, 0) 得到的
    print_maps_line_containing("    pointerA", pointerA);
    print_maps_line_containing("    pointerB", pointerB);

    // 两个方向各写一次：A 写、B 读；B 写、A 读。都对上才说明是同一块物理内存
    pointerA[0]             = 0x12345678;
    pointerB[1]             = 0x9abcdef0;
    const bool isSameMemory = pointerB[0] == 0x12345678 && pointerA[1] == 0x9abcdef0;
    LOGI(kTag, "    A 写 0x12345678，B 读到 0x%08x；B 写 0x9abcdef0，A 读到 0x%08x -> %s", pointerB[0], pointerA[1],
        isSameMemory ? "同一块内存" : "不是同一块");

    munmap(mmapAddress, dumbBuf.sizeBytes);
    // close(dmaBufFd)：只是还掉这个 fd，显存还被 drmFd 的 handle 拿着，不会释放
    close(dmaBufFd);
    return isSameMemory;
}

/**
 * @brief 对一种宽高做完整的一轮：申请映射 → 打印 pitch → 写满 → 两种方式找第 kCheckRowIndex 行 → PRIME 导出 → 释放
 * @param[in] drmFd  open("/dev/dri/card0") 得到的 fd
 * @param[in] width  宽，单位像素
 * @param[in] height 高，单位像素，必须大于 kCheckRowIndex
 * @return true = 每一步都成功；false = 任何一步失败
 */
bool check_dumb_buffer_of_size(int drmFd, uint32_t width, uint32_t height) {
    LOGI(kTag, "----- %u x %u -----", width, height);
    DumbBuffer dumbBuf;
    if (!create_and_map_dumb(drmFd, width, height, dumbBuf)) {
        return false;
    }
    // 第 1 步 看 pitch：驱动给的每行字节数，和 宽 × 4 比一比
    LOGI(kTag, "    handle %u | pitch %u 字节（宽 × 4 = %u，每行多 %u）| size %llu 字节", dumbBuf.handle,
        dumbBuf.pitchBytes, width * 4, dumbBuf.pitchBytes - width * 4,
        static_cast<unsigned long long>(dumbBuf.sizeBytes));
    LOGI(kTag, "    MAP_DUMB offset 0x%llx -> mmapAddress %p", static_cast<unsigned long long>(dumbBuf.mmapOffset),
        dumbBuf.mmapAddress);

    // 第 2 步 写满，再用两种方式找同一行，看错位
    fill_pixels_by_pitch(dumbBuf, width, height);
    print_row_start_by_two_strides(dumbBuf, width, kCheckRowIndex);

    // 第 3 步 导出成 dma_buf，再映射一次
    const bool isExportOk = check_export_to_dma_buf(drmFd, dumbBuf);

    unmap_and_destroy_dumb(drmFd, dumbBuf);
    return isExportOk;
}

int run_m4_dumb_buffer() {
    LOGD(kTag, "run_m4_dumb_buffer()");
    LOGD(kTag, "===== M4：DRM dumb buffer，看 pitch 和 宽 × 4 =====");
    const int drmFd = open(kDrmDevicePath, O_RDWR | O_CLOEXEC);
    if (drmFd < 0) {
        LOGE(kTag, "open %s: %s", kDrmDevicePath, strerror(errno));
        return 1;
    }
    const bool isWideOk = check_dumb_buffer_of_size(drmFd, 1920, 1080);
    const bool isOddOk  = check_dumb_buffer_of_size(drmFd, 1000, 1000);
    close(drmFd);
    return isWideOk && isOddOk ? 0 : 1;
}
