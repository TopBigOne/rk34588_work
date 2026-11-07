//
// M1～M3 共用的常量：画面尺寸、板子上的文件路径
//
#pragma once

// inline constexpr：常量写在头文件里，多个 .cpp include 也只有一份
// 主码流 / 子码流的画面尺寸
inline constexpr int kMainWidth = 1920;
inline constexpr int kMainHeight = 1080;
inline constexpr int kSubWidth = 1280;
inline constexpr int kSubHeight = 720;

// 输入：Day 1 用过的 60 帧 1080p NV12 文件（每帧紧挨着存，没有填充）
inline constexpr const char *kInputNv12FilePath = "/userdata/av/in_1080p_60f.nv12";

// M1 的输出
inline constexpr const char *kResizedFilePath = "/userdata/av/out_720p.nv12";

// M2 的输出
inline constexpr const char *kMainH264StreamPath = "/userdata/av/main_1080p.h264";
inline constexpr const char *kSubH264StreamPath = "/userdata/av/sub_720p.h264";

// M3：摄像头双码流
inline constexpr const char *kCameraDevicePath = "/dev/video22"; // ISP0 的 mainpath（Day 3 用 CameraFinder 找到的）
inline constexpr const char *kCameraMainStreamPath = "/userdata/av/cam_main_1080p.h264";
inline constexpr const char *kCameraSubStreamPath = "/userdata/av/cam_sub_720p.h264";
inline constexpr int kSkipFrameCount = 30;    // 开头丢掉的帧数（等自动曝光稳定，约 1 秒）
inline constexpr int kRecordFrameCount = 300; // 录多少帧：30 fps 下 10 秒
