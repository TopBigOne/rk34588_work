#include "experiments/m2_two_pointers.h"

#include "camera/camera_buffers.h"
#include "common/log.h"
#include "common/maps_printer.h"
#include <cerrno> // errno
#include <cstring> // strerror、memset、strcpy、strcmp
#include <fcntl.h> // O_RDWR、O_CLOEXEC
#include <sys/ioctl.h> // ioctl
#include <sys/mman.h> // mmap、munmap、MAP_FAILED
#include <unistd.h> // close

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
    //   为什么要转：void* 只说"这是个地址"，不说里面放的是什么，C++ 不允许对它做 +n 运算，也不能按下标取字节。
    //   char 正好 1 个字节，转成 char* 以后，pointerA + n 就是往后数 n 个字节。地址本身不变
    char* pointerA              = static_cast<char*>(camera.mmapAddresses[0]);
    const size_t bufLengthBytes = camera.lengths[0];
    // step 2: expbuf 第0块，拿到dma_buf fd
    // 注意flags 要带O_RDWR；
    //   EXPBUF = 把驱动里的一块 buffer 导出成一个 dma_buf，并给本进程一个新的 fd 指向它。
    //   dma_buf 是 Linux 在不同驱动之间共享内存的统一做法：拿着这个 fd，编码器、RGA、显示都能用同一块内存，不用拷贝
    v4l2_exportbuffer exportBuf = {}; // 整张表清零，fd 这一格现在是 0
    exportBuf.type  = kBufType; // buffer 类型（采集、多平面），内核用它找到对应的队列，必须和 REQBUFS 时一样
    exportBuf.index = 0; // 导出第几块（0 ～ kBufCount - 1）
    exportBuf.plane = 0; // 导出这块的第几个平面；NV12 只有 1 个平面，填 0
    // flags 是新 fd 的打开方式，内核只接受 O_CLOEXEC 和读写方式（4.4 videobuf2-core.c 1867）。
    //   O_RDWR：不带的话新 fd 是只读的，下面 mmap 要 PROT_WRITE + MAP_SHARED 会失败，返回 EACCES（doc/2 实测）
    exportBuf.flags = O_RDWR | O_CLOEXEC;
    // VIDIOC_EXPBUF 也是"填表 → 写回"：上面填输入，内核把新 fd 写进 exportBuf.fd。
    //   每调一次都新建一个 dma_buf、给一个新 fd（doc/2 实测）
    if (ioctl(camera.videoFd, VIDIOC_EXPBUF, &exportBuf) != 0) {
        LOGE(kTag, "      VIDIOC_EXPBUF: %s", strerror(errno));
        close_camera(camera);
        return 1;
    }
    // 内核写回的新 fd：0、1、2 是标准输入输出错误，3 被 videoFd 占了，所以实测是 4
    const int dmaBufFd = exportBuf.fd;

    // step 3: 用dma_buf fd 再mmap 一次，偏移是0（一个dma_buf fd 只代表这一块）
    //   和 videoFd 的 mmap 比，变的只有最后两个参数：fd 换成 dmaBufFd，偏移从"取件号"变成"这一块里从哪开始"。
    //   这次也不分配新内存：dma_buf 背后就是第 0 块那批物理页，所以 pointerA、pointerB 指向同一批物理页
    //   （doc/2 用 pagemap 实测 765/765 页一样），只是本进程里的虚拟地址不同
    char* pointerB = static_cast<char*>(mmap(nullptr, bufLengthBytes, PROT_READ | PROT_WRITE, MAP_SHARED, dmaBufFd, 0));
    if (pointerB == MAP_FAILED) { // mmap 失败返回 MAP_FAILED（(void*)-1），不是 nullptr
        LOGE(kTag, "      mmap dmaBufFd %d: %s", dmaBufFd, strerror(errno));
        close(dmaBufFd);
        close_camera(camera);
        return 1;
    }
    // 第 4 步：两个地址不一样，各自在 maps 里是哪一行
    //   A 那一行的文件名是 /dev/video22，B 那一行是 /dmabuf:...（doc/2 第 3 节）
    LOGI(kTag, "      pointerA = %p  （mmap(videoFd %d, mem_offset 0x%08x)）", pointerA, camera.videoFd,
        camera.memOffsets[0]);
    LOGI(kTag, "      pointerB = %p  （mmap(dmaBufFd %d, 0)）", pointerB, dmaBufFd);
    print_maps_line_containing("      pointerA", pointerA);
    print_maps_line_containing("      pointerB", pointerB);

    // 第5步，从A写，从B读
    //   这里没调 DMA_BUF_IOCTL_SYNC：A、B 都是 CPU 在读写，实测两个方向都一致。
    //   推断原因：同一个物理地址在 CPU 缓存里只有一份，CPU 看得到自己写的东西（两个映射的缓存属性没核对过）。
    //   SYNC 主要用在 CPU 和硬件（摄像头、编码器）之间，M3 再实测
    const char* msgFromA = "丝袜A";
    // 先清零 64 字节：没 STREAMON，buffer 里原来是什么不确定；清零后 strcpy 写进去的字符串后面一定是 '\0'
    memset(pointerA, 0, 64);
    // strcpy 连同结尾的 '\0' 一起拷：UTF-8 里一个汉字 3 字节，"丝袜A" = 3 + 3 + 1 = 7 字节，加 '\0' 共 8 字节，没超过
    // 64
    strcpy(pointerA, msgFromA);
    LOGI(kTag, "      A pointer 写入：%s", msgFromA);
    LOGI(kTag, "      B pointer 读取：%s", pointerB);
    const bool isAToBSame = strcmp(pointerB, msgFromA) == 0; // strcmp 两个字符串一样时返回 0
    LOGI(kTag, "     isAToBSame :%d", isAToBSame);
    // 第6步，从B写，A 读
    const char* msgFromB = "丝袜B at the end";

    // tailOfB 指向这块 buffer 最后 64 个字节的开头：pointerB 是开头，+ bufLengthBytes 到结尾，再往回退 64 字节。
    // 用实测的数字算一遍（bufLengthBytes = 3133440，第一行是相对 pointerB 的字节偏移）：
    //
    //   0                                3133376      3133440
    //   |--------------------------------|---- 64 ----|
    //   ^                                ^            ^
    //   pointerB                         tailOfB      pointerB + bufLengthBytes
    //
    //   pointerB                  ：第 1 个字节
    //   tailOfB                   ：最后 64 字节的开头
    //   pointerB + bufLengthBytes ：结尾，是最后一个字节的下一个位置，已经不属于这块 buffer，往这里写就出界了
    //
    // 为什么是 64：只要比写进去的字符串长就行。"丝袜B at the end" = 6 + 12 + 1（'\0'）= 19 字节，64 放得下；
    //   下面 memset(tailOfB, 0, 64) 正好清零到最后一个字节，不超出 buffer
    // 为什么写在结尾：第 5 步只测了开头（第 0 页）。偏移 3133376 ÷ 4096 ≈ 764.98，落在第 764 页，也就是最后一页，
    //   头尾都对得上，才说明整块都是同一批物理页
    // tailOfA 也这样算：A、B 虚拟地址不同，但都指向这块 buffer 的开头，各自 + bufLengthBytes - 64 就是同一个位置
    char* tailOfB = pointerB + bufLengthBytes - 64;
    char* tailOfA = pointerA + bufLengthBytes - 64;
    memset(tailOfB, 0, 64);
    strcpy(tailOfB, msgFromB);
    LOGI(kTag, "      B pointer尾部 写入：%s", msgFromB);
    LOGI(kTag, "      A pointer尾部 读取：%s", tailOfA);

    const bool isBToASame = strcmp(tailOfA, msgFromB) == 0;
    LOGI(kTag, "     isBToASame :%d", isBToASame);
    // 第7步：先 munmap B、关 dmaBufFd，再关摄像头
    //   按"后拿到的先放"的顺序释放。其实顺序反了也不会出错：dma_buf 有引用计数，
    //   B 的映射还在时，关掉摄像头那块内存也不会被释放（doc/2 实测），只是这样写更清楚
    munmap(pointerB, bufLengthBytes);
    close(dmaBufFd);
    close_camera(camera);
    return (isAToBSame && isBToASame) ? 0 : 1;
}
