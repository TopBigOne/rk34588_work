#include "camera/camera_buffers.h"

#include "common/log.h"
#include <cerrno> // errno：系统调用失败时，内核把错误码放在这个变量里
#include <cstring> // strerror：把 errno 的数字翻译成文字，例如 16 → "Device or resource busy"
#include <fcntl.h> // open、O_RDWR、O_CLOEXEC
#include <sys/ioctl.h> // ioctl
#include <sys/mman.h> // mmap、munmap、PROT_READ、PROT_WRITE、MAP_SHARED、MAP_FAILED
#include <unistd.h> // close

static const char* kTag = "Camera";

bool open_camera_and_map_buffers(CameraBuffers& camera) {
    LOGD(kTag, "open_camera_and_map_buffers()");
    // open：打开设备文件，返回一个 fd（文件描述符）。
    //   fd 是本进程"打开文件表"里的格子编号，之后对摄像头做的每件事（ioctl、mmap）都要带上它。
    //   内核总是给当前没被占用的最小编号：0、1、2 是标准输入、输出、错误，所以这里一般拿到 3。
    //   O_RDWR    ：可读可写打开。后面要 mmap 出可写的 buffer，只读打开不行
    //   O_CLOEXEC ：如果本进程以后 exec 去运行别的程序，这个 fd 自动关掉，不会被那个程序继承
    //   失败返回 -1，原因在 errno 里
    camera.videoFd = open(kVideoDevicePath, O_RDWR | O_CLOEXEC);
    if (camera.videoFd < 0) {
        LOGE(kTag, "     open %s: %s", kVideoDevicePath, strerror(errno));
        return false;
    }
    // v4l2_format：VIDIOC_S_FMT 用的"表"（doc/3）。
    //   = {} 把整个结构体清零：没填的字段（包括保留字段 reserved）必须是 0，否则驱动可能拒绝或者误读
    v4l2_format format       = {};
    format.type              = kBufType; // buffer 类型，决定下面用 union 里的哪个成员：MPLANE → fmt.pix_mp
    format.fmt.pix_mp.height = kHeight;
    format.fmt.pix_mp.width  = kWidth;
    // 像素格式，用 4 个字符的编号（FourCC）表示：V4L2_PIX_FMT_NV12 就是 'N','V','1','2'。
    //   NV12：先放整幅的 Y（亮度），后面紧跟 U、V 交错存放的色度，Y 和 UV 在同一块内存里
    format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    // 这个格式实际用了几个平面（几块独立内存）。
    // 由 pixelformat 决定：NV12 为 1，NV12M 为 2，YUV420M 为 3。
    format.fmt.pix_mp.num_planes = 1;
    // ioctl(fd, 命令, 表的地址)：对设备做"open/read/write 之外"的操作，都用 ioctl。
    //   VIDIOC_S_FMT = _IOWR('V', 5, struct v4l2_format)：W = 应用写进表、R = 内核写回表，两个方向都有。
    //   驱动不一定照单全收：不支持的宽高、格式，它会改成自己能做的，再写回 format（doc/3）
    //   成功返回 0，失败返回 -1、原因在 errno
    if (ioctl(camera.videoFd, VIDIOC_S_FMT, &format) != 0) { // 不等于0 ，就是失败了
        LOGE(kTag, "     VIDIOC_S_FMT: %s（相机 App 或者别的程序占着摄像头？）", strerror(errno));
        return false;
    }
    // v4l2_requestbuffers：VIDIOC_REQBUFS 用的表（doc/4）。REQBUFS = 请驱动准备 count 块 buffer
    v4l2_requestbuffers requestBufs = {};
    requestBufs.count               = kBufCount; // 输入：想要几块；输出：驱动实际给了几块
    requestBufs.type                = kBufType; // buffer 类型，必须和 S_FMT 的 type 一样（内核用它找队列）
    // V4L2_MEMORY_MMAP：buffer 由驱动在内核里分配，应用之后用 mmap 映射过来读写。
    //   另外两种：USERPTR（应用自己 malloc，把地址交给驱动）、DMABUF（应用把别处来的 dma_buf fd 交给驱动）
    requestBufs.memory = V4L2_MEMORY_MMAP; // 缓冲区内存的来源/管理方式
    // 这一步做完，4 块内存已经在内核里分配好了，每块也定好了 mem_offset（本板实测 = index × 3133440），
    // 但本进程还没有任何地址能碰到它们：地址要等下面 mmap 才有
    if (ioctl(camera.videoFd, VIDIOC_REQBUFS, &requestBufs) != 0) {
        LOGE(kTag, "     VIDIOC_REQBUFS: %s", strerror(errno));
        return false;
    }
    // count 是"填 + 读回"两头看：驱动可能给得比要的少（doc/4 第 3 节），
    // 少了的话下面 QUERYBUF 那几块会报 EINVAL，不如在这里就说清楚
    if (requestBufs.count < kBufCount) {
        LOGE(kTag, "     VIDIOC_REQBUFS: 要 %d 块，驱动只给了 %u 块", kBufCount, requestBufs.count);
        return false;
    }

    // 概述：给 REQBUFS 分好的 4 块 buffer 逐块"开门"。每一圈处理一块，拿到它在本进程里的地址，存进 camera。
    //
    // 为什么要循环：REQBUFS 之后，4 块内存已经在内核里了，但本进程还碰不到。
    //   要想读写某一块，必须对它单独做两件事，4 块就做 4 遍：
    //   1. QUERYBUF（问）：告诉内核"我要第 bufIndex 块"，内核写回这块的长度和 mem_offset（取件号）
    //   2. mmap（开门）：拿 videoFd + 这个 mem_offset 去 mmap，内核把这一块接到本进程，返回地址
    //   每一圈只有 bufIndex 在变，所以每圈问到的 mem_offset 不同（0、0x2fd000、0x5fa000、0x8f7000），
    //   mmap 出来的是不同的 4 块。
    //
    // 结果：循环结束后，camera.memOffsets / lengths / mmapAddresses 各有 4 项，第 i 项对应 buf i。
    //   M2 里的 pointerA 就是 camera.mmapAddresses[0]。
    //   任何一圈失败就直接 return false；已经映射好的那几块留在 camera 里，由调用方的 close_camera() 释放
    for (int bufIndex = 0; bufIndex < kBufCount; ++bufIndex) {
        // 多平面（MPLANE）的 buffer，每个平面的信息放在单独的 v4l2_plane 里。
        //   NV12 只有 1 个平面，所以数组只要 1 个元素；内核会把这块的长度、mem_offset 写进 planes[0]
        v4l2_plane planes[1] = {};
        v4l2_buffer v4l2Buf  = {};
        v4l2Buf.type         = kBufType; // 缓冲区类型
        v4l2Buf.memory       = V4L2_MEMORY_MMAP; // memory 决定用哪种内存方式，
        v4l2Buf.index        = bufIndex; // Note : 每次循环，这是变化的地方，所以 planes[0].m.mem_offset 值是不一样
        // m.planes：告诉内核"平面信息写到我这个数组里"。这里传的是 planes 数组的地址
        v4l2Buf.m.planes = planes;
        // MPLANE 时 length 的意思是"planes 数组有几个元素"，不是字节数（单平面时 length 才是字节数）
        v4l2Buf.length = 1;
        // QUERYBUF：查第 index 块 buffer 的信息。只是查，不分配内存（内存在 REQBUFS 时已经分配好了）。
        //   内核写回：planes[0].length（这块多少字节）、planes[0].m.mem_offset（mmap 要用的取件号）
        if (ioctl(camera.videoFd, VIDIOC_QUERYBUF, &v4l2Buf) != 0) {
            LOGE(kTag, "     VIDIOC_QUERYBUF %d: %s", bufIndex, strerror(errno));
            return false;
        }
        LOGI(kTag, "       |--------------------------------------------------↓");
        LOGI(kTag, "        -videoFd     ：%d", camera.videoFd);
        LOGI(kTag, "        -bufIndex    ：%d", bufIndex);
        LOGI(kTag, "        -length      ：%d", planes[0].length);
        LOGI(kTag, "        -mem_offset  ：0x%x", planes[0].m.mem_offset);
        LOGI(kTag, "       |--------------------------------------------------↑");
        // mmap：请"fd 背后的驱动"把一段内存接到本进程的地址空间里，返回本进程能直接读写的地址。
        //   参数 1 nullptr                ：映射到哪个地址由内核挑
        //   参数 2 planes[0].length       ：映射多少字节（QUERYBUF 刚写回的）
        //   参数 3 PROT_READ | PROT_WRITE：这段地址可读可写（fd 必须是 O_RDWR 打开的）
        //   参数 4 MAP_SHARED             ：和别人共用同一批物理页，本进程写进去，驱动和硬件都看得到。
        //                                   V4L2 只接受 MAP_SHARED，填 MAP_PRIVATE 会返回 EINVAL（doc/2 实测）
        //   参数 5 camera.videoFd         ：open("/dev/video22") 得到的 fd。内核凭它找到这个设备节点背后的
        //                                   摄像头驱动（rkisp），再由驱动交出 REQBUFS 分好的内存
        //   参数 6 mem_offset             ：要 4 块里的哪一块。它是取件号，不是地址；必须是 4096（一页）的倍数
        //   失败返回 MAP_FAILED（就是 (void*)-1），不是 nullptr，所以下面要和 MAP_FAILED 比
        //   这里不分配新内存：物理页还是 REQBUFS 时驱动分配的那批，mmap 只是给它们在本进程里安一个地址
        void* mmapAddress =
            mmap(nullptr, planes[0].length, PROT_READ | PROT_WRITE, MAP_SHARED, camera.videoFd, planes[0].m.mem_offset);
        if (mmapAddress == MAP_FAILED) {
            LOGE(kTag, "     mmap camera buffer %d: %s", bufIndex, strerror(errno));
            return false;
        }
        camera.memOffsets[bufIndex]    = planes[0].m.mem_offset;
        camera.lengths[bufIndex]       = planes[0].length;
        camera.mmapAddresses[bufIndex] = mmapAddress;
    }
    return true;
}

void close_camera(CameraBuffers& camera) {
    for (int bufIndex = 0; bufIndex < kBufCount; ++bufIndex) {
        if (camera.mmapAddresses[bufIndex] != nullptr) {
            // munmap(地址, 长度)：拆掉 mmap 安的地址，长度要和 mmap 时一样。
            //   拆掉以后再用这个地址读写会段错误（SIGSEGV）；物理页要等所有使用者都放手才真正释放
            munmap(camera.mmapAddresses[bufIndex], camera.lengths[bufIndex]);
            camera.mmapAddresses[bufIndex] = nullptr;
        }
    }
    if (camera.videoFd >= 0) {
        // close：把 fd 这个格子还给进程，驱动在这时释放 REQBUFS 分配的 buffer。
        //   如果某块还被 mmap 着、或者导出的 dma_buf 还有人拿着，那块要等它们也放手才真正释放（引用计数，doc/2 实测）
        close(camera.videoFd);
        camera.videoFd = -1;
    }
}
