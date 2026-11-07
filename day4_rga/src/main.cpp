//
// day4_rga：RGA 缩放 + MPP 双码流
//
// 用法：./day4_rga m1 | m2 | m3（不写参数默认 m3）
//   m1  从文件读一帧 1080p，RGA 缩成 720p，写成 NV12 文件      → app/m1_resize_one_frame.cpp
//   m2  文件双码流：1080p 主码流 + 720p 子码流                 → app/m2_dual_stream_from_file.cpp
//   m3  摄像头双码流：输入换成摄像头的 buffer（零拷贝）          → app/m3_dual_stream_from_camera.cpp
//
#include <cstring>

#include "app/milestones.h"
#include "common/log.h"

static const char *const kTag = "Main";

int main(int argc, char **argv) {
    const char *experimentName = argc > 1 ? argv[1] : "m3";
    if (strcmp(experimentName, "m1") == 0) {
        return run_m1_resize_one_frame();
    }
    if (strcmp(experimentName, "m2") == 0) {
        return run_m2_dual_stream_from_file();
    }
    if (strcmp(experimentName, "m3") == 0) {
        return run_m3_dual_stream_from_camera();
    }
    LOGE(kTag, "用法：%s m1 | m2 | m3", argv[0]);
    return 1;
}
