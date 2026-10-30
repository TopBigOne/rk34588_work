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

inline constexpr const char* kVideoDevicePath = "/dev/video22";
inline constexpr int kWidth                   = 1920;
inline constexpr int kHeight                  = 1080;
inline constexpr int kBufCount                = 4;
/**
 * 多平面视频采集
 */
inline constexpr v4l2_buf_type kBufType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

struct CameraBuffers {
    int videoFd                    = -1;
    uint32_t memOffsets[kBufCount] = {};
    size_t lengths[kBufCount]      = {};
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
