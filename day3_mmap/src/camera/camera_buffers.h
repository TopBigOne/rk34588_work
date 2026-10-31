/*
 * camera_buffers.h：打开摄像头，申请 4 块 MMAP buffer，每块都 mmap 到本进程（M1 第 2 步、M2 用）
 *
 *   open_camera_and_map_buffers()  open → S_FMT → REQBUFS → 每块 QUERYBUF + mmap
 *   close_camera()                 每块 munmap → close(videoFd)
 *
 * 只申请、映射，不 STREAMON：这两个实验只看内存，不需要真的拍
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <linux/videodev2.h>

// 在 Linux 里设备也是文件：/dev/video22 是 rkisp 主通路（mainpath）的视频节点，open 它就能和摄像头驱动说话。
// inline constexpr：C++17 写法，常量放在头文件里，被多个 .cpp 包含也只算一份，不会"重复定义"
inline constexpr const char* kVideoDevicePath = "/dev/video22";
inline constexpr int kWidth                   = 1920;
inline constexpr int kHeight                  = 1080;
inline constexpr int kBufCount                = 4;
/**
 * buffer 类型（v4l2_buf_type）：告诉驱动操作的是哪个方向、哪种数据的队列，以及用哪套结构体描述 buffer。
 * S_FMT、REQBUFS、QUERYBUF、EXPBUF 都要填这同一个值，填错返回 EINVAL。
 *  - VIDEO   ：视频画面（不是 VBI、SDR、META 这些别的数据）
 *  - CAPTURE ：数据从设备流向应用（摄像头往 buffer 里写，程序读）
 *  - MPLANE  ：用多平面那套结构体描述 buffer（format.fmt.pix_mp、v4l2Buf.m.planes）；
 *              不代表图像真有多个平面，这里 NV12 只有 1 个平面（num_planes = 1）。
 *              video22 只支持这套接口（doc/3 实测）
 */
inline constexpr v4l2_buf_type kBufType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

// open_camera_and_map_buffers() 每一步拿到的结果都存在这里，交给实验代码用（doc/5 第 5 节）
struct CameraBuffers {
    // open 返回的 fd。-1 表示还没打开（合法的 fd 都 >= 0）
    int videoFd = -1;
    // QUERYBUF 写回的 planes[0].m.mem_offset：mmap(videoFd) 时用来说"要哪一块"的取件号，不是地址
    uint32_t memOffsets[kBufCount] = {};
    // QUERYBUF 写回的 planes[0].length：每块多少字节（1920x1080 NV12 实测 3133440）
    size_t lengths[kBufCount] = {};
    // mmap(videoFd, memOffsets[i]) 的返回值：本进程里能直接读写第 i 块的地址。nullptr 表示还没映射
    void* mmapAddresses[kBufCount] = {};
};

/**
 * @brief 打开摄像头，设成 1920x1080 NV12，申请 kBufCount 块 MMAP buffer，每块都 mmap 到本进程
 * @param[out] camera 填好 videoFd 和每块的 memOffsets、lengths、mmapAddresses
 * @return 全部成功返回 true；任何一步失败返回 false
 * @note 失败时不在这里清理，已经打开的 fd、已经 mmap 的块都留在 camera 里，
 *       调用方不管成功失败都要调 close_camera()
 * @note 流程：open → S_FMT → REQBUFS → 每块 QUERYBUF + mmap（还没有 STREAMON，buffer 里没有画面）
 */
bool open_camera_and_map_buffers(CameraBuffers& camera);

/**
 * @brief 释放 open_camera_and_map_buffers() 拿到的东西：每块 munmap，再 close(videoFd)
 * @param[in,out] camera 释放后 mmapAddresses 全部置 nullptr、videoFd 置 -1
 * @return 无
 * @note 只 munmap 不是 nullptr 的块：mmap 失败时 MAP_FAILED 不会存进数组，没映射的块一直是 nullptr
 * @note close(videoFd) 时驱动会自动释放 REQBUFS 申请的 buffer，不用再 REQBUFS count = 0
 */
void close_camera(CameraBuffers& camera);
