#include "experiments/m2_two_pointers.h"

#include "camera/camera_buffers.h"
#include "common/log.h"
#include "common/maps_printer.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static const char* kTag = "M2";

int run_m2_two_pointers() {
    LOGD(kTag, "run_m2_two_pointers()");
    LOGD(kTag, "===== M2：同一块内存，两个指针 =====");
    CameraBuffers camera;
    // step1: 打开摄像头，4块都用mmap：指针A就是第0块mmapAddress
    if (!open_camera_and_map_buffers(camera)) {
        close_camera(camera);
        return 1;
    }
    // note: 将void 类型的指针，强转为char类的的指针
    char* pointerA              = static_cast<char*>(camera.mmapAddresses[0]);
    const size_t bufLengthBytes = camera.lengths[0];
    // step 2: expbuf 第0块，拿到dma_buf fd
    // 注意flags 要带O_RDWR；
    v4l2_exportbuffer exportBuf = {};
    exportBuf.type              = kBufType;
    exportBuf.index             = 0;
    exportBuf.plane             = 0;
    exportBuf.flags             = O_RDWR | O_CLOEXEC;
    if (ioctl(camera.videoFd, VIDIOC_EXPBUF, &exportBuf) != 0) {
        LOGE(kTag, "      VIDIOC_EXPBUF: %s", strerror(errno));
        close_camera(camera);
        return 1;
    }
    const int dmaBufFd = exportBuf.fd;

    // step 3: 用dma_buf fd 再mmap 一次，偏移是0（一个dma_buf fd 只代表这一块）
    char* pointerB = static_cast<char*>(mmap(nullptr, bufLengthBytes, PROT_READ | PROT_WRITE, MAP_SHARED, dmaBufFd, 0));
    if (pointerB == MAP_FAILED) {
        LOGE(kTag, "      mmap dmaBufFd %d: %s", dmaBufFd, strerror(errno));
        close(dmaBufFd);
        close_camera(camera);
        return 1;
    }
    // 第 4 步：两个地址不一样，各自在 maps 里是哪一行
    LOGI(kTag, "      pointerA = %p  （mmap(videoFd %d, mem_offset 0x%08x)）", pointerA, camera.videoFd,
        camera.memOffsets[0]);
    LOGI(kTag, "      pointerB = %p  （mmap(dmaBufFd %d, 0)）", pointerB, dmaBufFd);
    print_maps_line_containing("      pointerA", pointerA);
    print_maps_line_containing("      pointerB", pointerB);

    // 第5步，从A写，从B读
    const char* msgFromA = "丝袜A";
    memset(pointerA, 0, 64);
    strcpy(pointerA, msgFromA);
    LOGI(kTag, "      A pointer 写入：%s", msgFromA);
    LOGI(kTag, "      B pointer 读取：%s", pointerB);
    const bool isAToBSame = strcmp(pointerB, msgFromA) == 0;
    LOGI(kTag, "     isAToBSame :%d", isAToBSame);
    // 第6步，从B写，A 读
    const char* msgFromB = "丝袜B at the end";
    char* tailOfB        = pointerB + bufLengthBytes - 64;
    char* tailOfA        = pointerA + bufLengthBytes - 64;
    memset(tailOfB, 0, 64);
    strcpy(tailOfB, msgFromB);
    LOGI(kTag, "      B pointer尾部 写入：%s", msgFromB);
    LOGI(kTag, "      A pointer尾部 读取：%s", tailOfA);

    const bool isBToASame = strcmp(tailOfA, msgFromB) == 0;
    LOGI(kTag, "     isBToASame :%d", isBToASame);
    // 第7步：先 munmap B、关 dmaBufFd，再关摄像头
    munmap(pointerB, bufLengthBytes);
    close(dmaBufFd);
    close_camera(camera);
    return (isAToBSame && isBToASame) ? 0 : 1;
}
